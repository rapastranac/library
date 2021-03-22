#pragma once
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "MPI_Mutex.hpp"
#include "Utils.hpp"

#include <cstring>
#include <random>
#include <stdlib.h> /* srand, rand */
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <string>
#include <sstream>
#include <stdio.h>
#include <time.h>
#include <thread>

namespace library
{

	class BranchHandler;

	class Scheduler
	{

	private:
		BranchHandler &_branchHandler;

	public:
		static Scheduler &getInstance(BranchHandler &branchHandler)
		{
			static Scheduler instance(branchHandler);
			return instance;
		}

		void setThreadsPerNode(size_t threadsPerNode)
		{
			this->threadsPerNode = threadsPerNode;
		}

		int initMPI(int *argc, char *argv[]);

		template <typename _ret, typename F, typename Holder, typename Serialize, typename Deserialize>
		void start(F &&f, Holder &holder, Serialize &&serialize, Deserialize &&deserialize);

		void finalize()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, before deallocate \n", world_rank);
#endif
			win_deallocate();
#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, after deallocate \n", world_rank);
#endif
			MPI_Finalize();
#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, after MPI_Finalize() \n", world_rank);
#endif
		}

		std::stringstream &retrieveResult()
		{
			int pos;
			for (int rank = 1; rank < world_size; rank++)
			{
				if (bestResults[rank].first == refValueGlobal[0])
				{
					int SIZE = bestResults[rank].second.str().size();
					fmt::print("Stream retrieved, size : {} \n", SIZE);
					pos = rank;
					break;
				}
			}

			return bestResults[pos].second;
		}

		void printStats()
		{
			fmt::print("\n \n \n");
			fmt::print("*****************************************************\n");
			fmt::print("Elapsed time : {:4.3f} \n", elapsedTime());
			fmt::print("Total number of requests : {} \n", totalRequests);
			fmt::print("Number of approved requests : {} \n", approvedRequests);
			fmt::print("Number of failed requests : {} \n", failedRequests);
			fmt::print("*****************************************************\n");
			fmt::print("\n \n \n");
		}

		double elapsedTime()
		{
			return end_time - start_time;
		}

		void allgather(void *recvbuf, void *sendbuf)
		{
			MPI_Allgather(sendbuf, 1, MPI_DOUBLE, recvbuf, 1, MPI_DOUBLE, world_Comm);
			MPI_Barrier(world_Comm);
		}

		int getWorldSize()
		{
			return world_size;
		}

	private:
		template <typename Holder, typename Serialize>
		void schedule2(Holder &holder, Serialize &&serialize)
		{
			sendSeed(holder, serialize);

			//updateNumAvNodes();
#ifdef DEBUG_COMMENTS
			fmt::print("numAvailableNodes up-to-date : {} \n", numAvailableNodes[0]);
#endif
			//BcastPut(numAvailableNodes, 1, MPI_INT, 0, win_NumNodes);
			refValueGlobal_old = refValueGlobal[0];
			numAvailableNodes_old = numAvailableNodes[0];
#ifdef DEBUG_COMMENTS
			fmt::print("*** Busy nodes: {} ***\n ", busyNodes[0]);
#endif
			fmt::print("Scheduler started!! \n");

			do
			{

				for (int rank = 1; rank < world_size; rank++)
				{
					updateNumAvNodes();			 // check list of available nodes
					if (push_request[rank] == 1) // if this cell changes, it means that a node has requested permission to push
					{
						if (numAvailableNodes[0] > 0) // if found, positive signal is sent back to requesting node
						{
							//int subs{-1};
							//accumulate_mpi(subs, 1, MPI_INT, 0, 0, MPI_SUM, win_NumNodes); // process-safe
							--numAvailableNodes[0]; // process-safe, only this rank can modify it
							//BcastNumAvNodes();
							BcastPut(numAvailableNodes, 1, MPI_INT, 0, win_NumNodes);

							int k = findAvailableNode(); // first available node in the list
							//int reset{0};
							//int r1 = put_mpi(&reset, 1, MPI_INT, 0, k, win_AvNodes);	   // k node no longer available
							//int r2 = put_mpi(&reset, 1, MPI_INT, 0, rank, win_phsRequest); // reset boolean to zero lest center node check it again, unless requested by nodes
							availableNodes[k] = false;						   // k node no longer available
							push_request[rank] = false;						   // reset boolean to zero lest center node check it again, unless requested by nodes
							int flag{1};									   // signal to be sent back to node 'rank'
							MPI_Ssend(&flag, 1, MPI_INT, rank, k, world_Comm); // returns signal to 'rank' that data can be received
							++approvedRequests;
						}
						else
						{
							/* No available nodes, return false signal so the handler could forward the information sequentially */
							int flag{0};
							//int r1 = put_mpi(&flag, 1, MPI_INT, 0, rank, win_phsRequest); // reset boolean to zero lest center node check it again, unless requested by nodes
							push_request[rank] = false;						   // this is safe due to MPI_THREAD_SERIALIZED reasons
							MPI_Ssend(&flag, 1, MPI_INT, rank, 0, world_Comm); //returns signal that data cannot be received
							++failedRequests;
						}
						++totalRequests;
					}
				}

				// ATTENTION !!!! WHATCH OUT *****
				//if (breakLoop(nodes, busy))
				//{
				//	MPI_Barrier(world_Comm); //This is synchronised within sendBestResultToCenter() in BranchHandler.hpp
				//	retrieveSolVoid();		 // non-waiting algorithms
				//	break;
				//}

				//fetchSolution<_ret>(); // waiting algorithms

				// refValueGlobal updated if changed
				if (refValueGlobal[0] != refValueGlobal_old)
				{
					BcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal); // broadcast the absolute global ref value
					refValueGlobal_old = refValueGlobal[0];
				}

			} while (true);
		}

		template <typename _ret, typename Holder, typename Serialize>
		void schedule(Holder &holder, Serialize &&serialize)
		{
			int nodes = 0; // number of available nodes -- private to this method
			int busy = 0;
			std::vector<int> aNodes(world_size);
			sync_availability(aNodes, nodes);
			MPI_Barrier(world_Comm); // synchronise process in world group
			sendSeed(aNodes, nodes, busy, holder, serialize);

			refValueGlobal_old = refValueGlobal[0];

			int rcv_availability = 0;

			while (true)
			{
				//fmt::print("nodes {}, busy {} \n", nodes, busy);
				MPI_Status status;
				int buffer;
				MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

				int src_rank = status.MPI_SOURCE;
				int TAG = status.MPI_TAG;

				/*
					TAG scenarios:
					TAG == 2 discarded
					TAG == 3 termination signal
					TAG == 4 availability report
					TAG == 5 push request
					TAG == 6 result request
					TAG == 7 no result from rank
					TAG == 8 reference value update, TAG == 9 for actual update
				*/
				if (TAG == 4)
				{
					++rcv_availability;
#ifdef DEBUG_COMMENTS
					fmt::print("rank {} availability received {} times\n", world_rank, rcv_availability);
#endif
					if (aNodes[src_rank] == 1)
						fmt::print("***********************ERROR************************** aNodes[{}] == 1\n ", src_rank);
					aNodes[src_rank] = 1;
					++nodes;
					--busy;
					BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes

					if (breakLoop(nodes, busy))
						break;

					continue;
				}
				if (TAG == 5)
				{
					if (nodes > 0)
					{
						int k = isAvailable(aNodes);
						if (aNodes[k] == 0)
							fmt::print("***********************ERROR************************** aNodes[{}] == 0\n ", k);
						aNodes[k] = 0;
						int flag = 1;
						--nodes;
						++busy;
						BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes);		   // Broadcast number of nodes
						MPI_Ssend(&flag, 1, MPI_INT, src_rank, k, world_Comm); // send positive signal
						++approvedRequests;
					}
					else
					{
						int flag = 0;
						BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes);		   // Broadcast number of nodes
						MPI_Ssend(&flag, 1, MPI_INT, src_rank, 0, world_Comm); // send negative signal
						++failedRequests;
					}
					++totalRequests;

					if (breakLoop(nodes, busy))
						break;

					continue;
				}
				if (TAG == 8)
				{
					// send ref value global
					MPI_Ssend(refValueGlobal, 1, MPI_INT, src_rank, 0, world_Comm);

					// receive either the update ref value global or just pass
					MPI_Recv(&buffer, 1, MPI_INT, src_rank, MPI_ANY_TAG, world_Comm, &status);
					int TAG2 = status.MPI_TAG;
					if (TAG2 == 9)
					{
						refValueGlobal[0] = buffer;
						BcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);
					}

					if (breakLoop(nodes, busy))
						break;

					continue;
				}
			}

			//terminate
			// TAG == 3 termination signal

			for (int rank = 1; rank < world_size; rank++)
			{
				char buffer = 'a';
				MPI_Ssend(&buffer, 1, MPI_CHAR, rank, 3, world_Comm); // send positive signal
			}
			MPI_Barrier(world_Comm);

			// receive solution from other processes
			fetchSolution();
		}

		// first synchronisation, thus rank 0 is aware of other availability
		// the purpose is to broadcast the total availability to other ranks
		void sync_availability(std::vector<int> &aNodes, int &nodes)
		{
			while (true)
			{
				MPI_Status status;
				int buffer;

				MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, 4, world_Comm, &status);

				int src_rank = status.MPI_SOURCE;
				/*
					TAG scenarios:
					TAG == 4 availability report
				*/

				aNodes[src_rank] = 1;
				++nodes;
				//BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes

				if (nodes == (world_size - 1))
					break;
			}
			BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // broadcast nodes
			MPI_Barrier(world_Comm);					   // synchronise process in world group
		}

		int isAvailable(std::vector<int> &aNodes)
		{
			int r = -1;
			for (int rank = 1; rank < world_size; rank++)
			{
				if (aNodes[rank] == 1)
				{
					r = rank;
					break;
				}
			}
			return r; // this should not happen
		}

		// this sends the termination signal
		bool breakLoop(const int &nodes, const int &busy)
		{
			//std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 4 testing
			//fmt::print("test, busyNodes = {}\n", busyNodes[0]);

			if (nodes == (world_size - 1) && busy == 0)
			{
				fmt::print("Termination achieved: nodes {}, busy {} \n", nodes, busy);
				return true; //termination .. TODO to guarantee it reaches 0 only once
			}
			return false;
		}

		//template <typename _ret, std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		void fetchSolution()
		{
			// order order order order
			//int nodes = world_size - 1;

			for (int rank = 1; rank < world_size; rank++)
			{

				MPI_Status status;
				int Bytes;
				// sender would not need to send data size before hand **********************************************
				MPI_Probe(rank, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &Bytes);		   // receives total number of datatype elements of the message
				//***************************************************************************************************

				char *buffer = new char[Bytes];
				MPI_Recv(buffer, Bytes, MPI_CHAR, rank, MPI_ANY_TAG, world_Comm, &status);

				int TAG = status.MPI_TAG;
#ifdef DEBUG_COMMENTS
				fmt::print("fetching result from rank {} \n", rank);
#endif
				char empty[] = "empty_buffer";
				bool isEmpty = false;
				int count = 1;
				for (int i = 0; i < 12; i++)
				{
					if (buffer[i] == empty[i])
					{
						count++;
						if (count == 12)
							isEmpty = true;
					}
				}

				if (isEmpty)
				{
					fmt::print("solution NOT received from rank {}\n", rank);
					delete[] buffer;
					continue;
				}
				fmt::print("Center received a best result from {}, Bytes : {}, refVal {} \n", rank, Bytes, status.MPI_TAG);
				std::stringstream ss;

				for (int i = 0; i < Bytes; i++)
				{
					ss << buffer[i];
				}

				bestResults[rank].first = TAG;			  // reference value corresponding to result
				bestResults[rank].second = std::move(ss); // best result so far from this rank

				delete[] buffer;
			}
		}

		//this method is usefull only when parallelizing waiting algorithms
		template <typename _ret, std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		void fetchSolution2()
		{
			if (termination[0])
			{
				MPI_Status status;
				int Bytes;

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &Bytes);					 // receives total number of datatype elements of the message

				char *buffer = new char[Bytes];
				MPI_Recv(buffer, Bytes, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

				int src = status.MPI_SOURCE;
				std::stringstream ss;

				for (int i = 0; i < Bytes; i++)
				{
					ss << buffer[i];
				}

				termination[0] = false; // this should happen only once, thus breakLoop() will end the execution
				delete[] buffer;

				bestResults[src].first = status.MPI_TAG; // reference value corresponding to result
				bestResults[src].second = std::move(ss); // best result so far from this rank
			}
		}

		template <typename Holder, typename Serialize>
		void sendSeed(std::vector<int> &aNodes, int &nodes, int &busy, Holder &holder, Serialize &&serialize)
		{
			// global syncrhonisation **********************
			--nodes;
			++busy;
			aNodes[1] = 0;
			BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes
			// *********************************************

			auto ss = std::apply(serialize, holder.getArgs());
			int count = ss.str().size(); // number of Bytes

			int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, 1, 0, world_Comm); // send buffer
			if (err != MPI_SUCCESS)
				fmt::print("buffer failed to send! \n");

			fmt::print("Seed sent \n");
		}

		//generic put blocking RMA
		void BcastPut(const void *origin_addr, int count,
					  MPI_Datatype mpi_datatype, MPI_Aint offset,
					  MPI_Win &window)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, window);									  // open epoch
				MPI_Put(origin_addr, count, mpi_datatype, rank, offset, count, mpi_datatype, window); // put date through window
				MPI_Win_flush(rank, window);														  // complete RMA operation
				MPI_Win_unlock(rank, window);														  // close epoch
			}
		}

		/* this is supposed to be invoked only when there are available nodes
		returns -1 if no available node [it sould not happen] */
		int findAvailableNode()
		{
			//std::vector<int> nodes; // testing
			//int availableNodeID = -1;
			for (int rank = 1; rank < world_size; rank++)
			{
				if (availableNodes[rank] == 1)
				{
					//availableNodes[rank] = false; // once found, it is changed back to false
					return rank; // rank id
				}

				//nodes.push_back(rank);
			}

			//std::random_device rd;	// Will be used to obtain a seed for the random number engine
			//std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
			//std::uniform_int_distribution<> distrib(0, nodes.size() - 1);
			//int rank = distrib(gen);				 // testing
			//availableNodeID = nodes[rank];			 // testing
			//availableNodes[availableNodeID] = false; // testing
			//return availableNodeID;
			fmt::print("This code section should never be reached !!\n");
			return -1; // this should not happen
		}

		/* if other nodes become idle, they will put "true" into their cell at availableNodes*/
		void updateNumAvNodes()
		{
			int count = 0;
			for (int rank = 1; rank < world_size; rank++)
			{
				if (availableNodes[rank] == 1)
					++count;
			}
			numAvailableNodes[0] = count;

			//#ifdef DEBUG_COMMENTS
			//			fmt::print("numAvailableNodes up-to-date : {} \n", numAvailableNodes[0]);
			//#endif

			if (count != numAvailableNodes_old)
			{
				numAvailableNodes_old = count;
				BcastPut(&count, 1, MPI_INT, 0, win_NumNodes); // this line broadcast to slave ranks
			}
		}

		void communicators()
		{
			MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library

			MPI_Comm_size(world_Comm, &this->world_size);
			MPI_Comm_rank(world_Comm, &this->world_rank);

			if (world_size < 2)
			{
				fmt::print("At least two processes required !!\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}

			MPI_Comm_group(world_Comm, &world_group); // world group, all ranks

			// a communicator to syncronise only process 0 and 1 ****************************
			const int second_group_ranks[2] = {0, 1}; // build a ranks group in second_group

			int err = MPI_Group_incl(world_group, 2, second_group_ranks, &second_group); // include ranks in group
			if (err != MPI_SUCCESS)
				fmt::print("MPI_Group_incl could not include ranks! \n");
			err = MPI_Comm_create_group(world_Comm, second_group, 0, &second_Comm); // creates the group
			if (err != MPI_SUCCESS)
				fmt::print("MPI_group could not be created! \n");
			// ******************************************************************************

			MPI_Comm_dup(world_Comm, &accumulator_Comm);
			MPI_Comm_dup(world_Comm, &avNodes_Comm);
			MPI_Comm_dup(world_Comm, &numNodes_Comm);
			MPI_Comm_dup(world_Comm, &pshRequest_Comm);
			MPI_Comm_dup(world_Comm, &resRequest_Comm);
			MPI_Comm_dup(world_Comm, &mutex_Comm);
		}

		void win_allocate()
		{
			// anything outside the condition is applicable for all the processes
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &numAvailableNodes, &win_NumNodes);
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, world_Comm, &refValueGlobal, &win_refValueGlobal);

			if (world_rank == 0)
			{
				MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);
				MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, avNodes_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, pshRequest_Comm, &push_request, &win_phsRequest);
				MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, resRequest_Comm, &result_request, &win_resRequest);

				// mpi mutex **********************************************************************************************
				MPI_Win_allocate(sizeof(bool), sizeof(bool), MPI_INFO_NULL, mutex_Comm, &mpi_mutex.mutex, &win_mutex);
				// ********************************************************************************************************
				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(sizeof(bool), sizeof(bool), MPI_INFO_NULL, second_Comm, &termination, &win_termination);
			}
			else
			{
				// it is not required to allocate buffer memory for the other processes

				MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);
				MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, avNodes_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, pshRequest_Comm, &push_request, &win_phsRequest);
				MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, resRequest_Comm, &result_request, &win_resRequest);

				// mpi mutex **********************************************************************************************
				MPI_Win_allocate(0, sizeof(bool), MPI_INFO_NULL, mutex_Comm, &mpi_mutex.mutex, &win_mutex);
				// ********************************************************************************************************

				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(0, sizeof(bool), MPI_INFO_NULL, second_Comm, &termination, &win_termination);
			}

			//fmt::print("Allocated\n");

			init();
			MPI_Barrier(world_Comm);
		}

		void win_deallocate()
		{
			// mpi mutex ********************
			MPI_Win_free(&win_mutex);
			// ******************************

			MPI_Win_free(&win_accumulator);
			MPI_Win_free(&win_AvNodes);
			MPI_Win_free(&win_phsRequest);
			MPI_Win_free(&win_resRequest);
			MPI_Win_free(&win_NumNodes);
			MPI_Win_free(&win_refValueGlobal);

			if (MPI_COMM_NULL != second_Comm)
				MPI_Win_free(&win_termination);

			MPI_Group_free(&second_group);
			MPI_Group_free(&world_group);

			if (MPI_COMM_NULL != second_Comm)
				MPI_Comm_free(&second_Comm);

			MPI_Comm_free(&accumulator_Comm);
			MPI_Comm_free(&avNodes_Comm);
			MPI_Comm_free(&numNodes_Comm);
			MPI_Comm_free(&pshRequest_Comm);
			MPI_Comm_free(&resRequest_Comm);
			MPI_Comm_free(&mutex_Comm);
		}

		void init()
		{
			if (world_rank == 0)
			{
				// mpi mutex ********************
				mpi_mutex.mutex[0] = false;
				// ******************************

				bestResults.resize(world_size);

				//numAvailableNodes[0] = world_size - 1;
				numAvailableNodes[0] = 0;
				busyNodes[0] = 0;
				termination[0] = false;
				for (int i = 0; i < world_size; i++)
				{
					push_request[i] = false;
					availableNodes[i] = false; // no node is available, each node is in charge of communicating its availability
					result_request[i] = false;
				}
			}
			else
			{
				numAvailableNodes[0] = 0;
				//numAvailableNodes[0] = world_size - 1;
			}
		}

		void accumulate_mpi(const int buffer, int origin_count, MPI_Datatype mpi_datatype, int target_rank, MPI_Aint offset, MPI_Op op, MPI_Win &window)
		{
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, MPI_MODE_NOCHECK, window);
			MPI_Accumulate(&buffer, origin_count, mpi_datatype, target_rank, offset, 1, mpi_datatype, op, window);
			MPI_Win_flush(target_rank, window);
			MPI_Win_unlock(target_rank, window);
		}

		int put_mpi(const void *origin_addr, int count, MPI_Datatype mpi_type, int target_rank, MPI_Aint offset, MPI_Win &window)
		{
			int result;
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, MPI_MODE_NOCHECK, window); // opens epoch
			MPI_Put(origin_addr, count, mpi_type, target_rank, offset, count, mpi_type, window);
			MPI_Win_flush(target_rank, window);

			// 4 testing, verifies if value successfully put
			MPI_Get(&result, count, mpi_type, target_rank, offset, count, mpi_type, window);
			MPI_Win_flush(target_rank, window);

			MPI_Win_unlock(target_rank, window); // closes epoch
			return result;
		}

	private:
		int argc;
		char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Mutex mpi_mutex;
		MPI_Comm mutex_Comm;
		MPI_Win win_mutex;

		MPI_Win win_accumulator;	// windows for the busyNodes variable
		MPI_Win win_AvNodes;		// window for the list of available nodes
		MPI_Win win_NumNodes;		// window for the number of available nodes
		MPI_Win win_phsRequest;		// window for pushing request from nodes
		MPI_Win win_termination;	// window to signal termination
		MPI_Win win_resRequest;		// window to request sending results
		MPI_Win win_refValueGlobal; // window to send reference value global

		MPI_Group world_group;	   // all ranks belong to this group
		MPI_Group second_group;	   // only rank 0 & 1 belong to this group
		MPI_Comm accumulator_Comm; // attached to win_accumulator
		MPI_Comm avNodes_Comm;	   // attached to win_AvNodes
		MPI_Comm numNodes_Comm;	   // attached to win_NumNodes
		MPI_Comm pshRequest_Comm;  // attached to win_phsRequest
		MPI_Comm resRequest_Comm;  // attached to win_resRequest
		MPI_Comm second_Comm;	   // used to synchronise ranks 0 & 1
		MPI_Comm world_Comm;	   // world communicator

		bool *termination = nullptr; // applicable only for waiting algorithms, it means that final result is ready

		int *availableNodes = nullptr; // list of available nodes [only center node has the list]
		int *push_request = nullptr;   // receives signal of a node attempting to put data [only center node has the list]
		int *result_request = nullptr; // a process is requesting to update the best result

		int *busyNodes = nullptr;		  // number of nodes working at the time
		int *numAvailableNodes = nullptr; // Number of available nodes	[every node is aware of this number]
		int numAvailableNodes_old;		  // this helps to avoid broadcasting if it hasn't changed
		int *refValueGlobal = nullptr;	  // reference value to chose a best result
		int refValueGlobal_old;			  // this helps to avoid broadcasting if it hasn't changed

		std::stringstream returnStream;
		std::stringstream returnStream2; // for testing only, recipient of best result
		std::vector<std::pair<int, std::stringstream>> bestResults;

		size_t threadsPerNode = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

		// statistics
		size_t totalRequests = 0;
		size_t approvedRequests = 0;
		size_t failedRequests = 0;
		double start_time = 0;
		double end_time = 0;

		/* singleton*/
		Scheduler(BranchHandler &branchHandler) : _branchHandler(branchHandler) {}
	};

} // namespace library

#endif