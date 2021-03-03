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
			printf("rank %d, before deallocate \n", world_rank);
#endif
			win_deallocate();
#ifdef DEBUG_COMMENTS
			printf("rank %d, after deallocate \n", world_rank);
#endif
			MPI_Finalize();
#ifdef DEBUG_COMMENTS
			printf("rank %d, after MPI_Finalize() \n", world_rank);
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
					printf("Stream retrieved, size : %d \n", SIZE);
					pos = rank;
					break;
				}
			}

			return bestResults[pos].second;
		}

		void printfStats()
		{
			printf("\n \n \n");
			printf("*****************************************************\n");
			printf("Elapsed time : %4.3f \n", elapsedTime());
			printf("Total number of requests : %zu \n", totalRequests);
			printf("Number of approved requests : %zu \n", approvedRequests);
			printf("Number of failed requests : %zu \n", failedRequests);
			printf("*****************************************************\n");
			printf("\n \n \n");
		}

		double elapsedTime()
		{
			return end_time - start_time;
		}

		void allgather(void *recvbuf, void *sendbuf)
		{
			MPI_Allgather(sendbuf, 1, MPI::DOUBLE, recvbuf, 1, MPI::DOUBLE, world_Comm);
			MPI_Barrier(world_Comm);
		}

		int getWorldSize()
		{
			return world_size;
		}

	private:
		template <typename Holder, typename Serialize>
		void schedule(Holder &holder, Serialize &&serialize)
		{
			sendSeed(holder, serialize);
			if (MPI_COMM_NULL != second_Comm)
				MPI_Barrier(second_Comm); // syncrhonises only process 0 and 1 - this guarantees ...
										  // ... that process 0 does not terminate the loop before process 1...
										  // ... receives the seed, then busyNodes will be != 0 for the first ...
										  // ... loop
			updateNumAvNodes();
			//BcastNumAvNodes(); // comunicate to all nodes the total number of available nodes
			BcastPut(numAvailableNodes, 1, MPI::INT, 0, win_NumNodes);
			refValueGlobal_old = refValueGlobal[0];
			numAvailableNodes_old = numAvailableNodes[0];
#ifdef DEBUG_COMMENTS
			printf("*** Busy nodes: %d ***\n ", busyNodes[0]);
#endif
			printf("Scheduler started!! \n");

			do
			{
				for (int rank = 1; rank < world_size; rank++)
				{
					updateNumAvNodes();				 // check list of available nodes
					if (inbox_boolean[rank] == true) // if this cell changes, then a node has requested permission to push
					{
						if (numAvailableNodes[0] > 0) // if found, positive signal is sent back to requesting node
						{
							--numAvailableNodes[0];
							//BcastNumAvNodes();
							//BcastPut(numAvailableNodes[0], 1, MPI::INT, 0, win_NumNodes);

							int k = findAvailableNode();						 // first available node in the list
							inbox_boolean[rank] = false;						 // reset boolean to zero lest center node check it again, unless requested by nodes
							int flag = true;									 // signal to be sent back to node 'rank'
							MPI_Ssend(&flag, 1, MPI::BOOL, rank, k, world_Comm); // returns signal to 'rank' that data can be received
							++approvedRequests;
						}
						else
						{
							/* No available nodes, return false signal so the handler could forward the information sequentially */
							int flag = false;
							inbox_boolean[rank] = false;						 // this is safe due to MPI_THREAD_SERIALIZED reasons
							MPI_Ssend(&flag, 1, MPI::BOOL, rank, 0, world_Comm); //returns signal that data cannot be received
							++failedRequests;
						}
						++totalRequests;
					}
				}

				if (breakLoop())
				{
					MPI_Barrier(world_Comm); //This is synchronised within sendBestResultToCenter() in BranchHandler.hpp
					receiveCurrentResult();	 // non-waiting algorithms
					break;
				}

				receiveResult(); // waiting algorithms

				// refValueGlobal updated if changed
				if (refValueGlobal[0] != refValueGlobal_old)
				{
					BcastPut(refValueGlobal, 1, MPI::INT, 0, win_refValueGlobal); // broadcast the absolute global ref value
					refValueGlobal_old = refValueGlobal[0];
				}

			} while (true);
		}

		// this sends the termination signal
		bool breakLoop()
		{
			//std::this_thread::sleep_for(std::chrono::seconds(1)); // 4 testing
			//printf("test, busyNodes = %d\n", busyNodes[0]);

			if (busyNodes[0] == 0 && (numAvailableNodes[0] == (world_size - 1)))
			{
				for (int dest = 1; dest < world_size; dest++)
				{
					char emptyBuffer{'a'};
					int tag = 3;
					MPI_Ssend(&emptyBuffer, 1, MPI::CHAR, dest, tag, world_Comm);
				}
				printf("BusyNodes = 0 achieved \n");
				return true;
			}
			return false;
		}

		//this method is usefull only when parallelizing waiting algorithms
		void receiveResult()
		{
			if (finalFlag[0])
			{
				MPI_Status status;
				int Bytes;

				MPI_Probe(MPI::ANY_SOURCE, MPI::ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI::CHAR, &Bytes);					   // receives total number of datatype elements of the message

				char *buffer = new char[Bytes];
				MPI_Recv(buffer, Bytes, MPI::CHAR, MPI::ANY_SOURCE, MPI::ANY_TAG, world_Comm, &status);

				int src = status.MPI_SOURCE;
				std::stringstream ss;

				for (int i = 0; i < Bytes; i++)
				{
					ss << buffer[i];
				}

				finalFlag[0] = false; // this should happen only once, thus breakLoop() will end the execution
				delete[] buffer;

				bestResults[src].first = status.MPI_TAG; // reference value corresponding to result
				bestResults[src].second = std::move(ss); // best result so far from this rank
			}
		}

		void receiveCurrentResult()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (inbox_bestResult[rank])
				{
					inbox_bestResult[rank] = false;

					MPI_Status status;
					int Bytes;
					// sender would not need to send data size before hand **********************************************
					MPI_Probe(rank, MPI::ANY_TAG, world_Comm, &status); // receives status before receiving the message
					MPI_Get_count(&status, MPI::CHAR, &Bytes);			// receives total number of datatype elements of the message
					//***************************************************************************************************

					char *buffer = new char[Bytes];
					MPI_Recv(buffer, Bytes, MPI::CHAR, rank, MPI::ANY_TAG, world_Comm, &status);

					printf("Center received a best result from %d, Bytes : %d, refVal %d \n", rank, Bytes, status.MPI_TAG);

					std::stringstream ss;

					for (int i = 0; i < Bytes; i++)
					{
						ss << buffer[i];
					}
					delete[] buffer;
					bestResults[rank].first = status.MPI_TAG; // reference value corresponding to result
					bestResults[rank].second = std::move(ss); // best result so far from this rank
				}
			}
		}

		template <typename Holder, typename Serialize>
		void sendSeed(Holder &holder, Serialize &&serialize)
		{
			auto ss = std::apply(serialize, holder.getArgs());

			int count = ss.str().size(); // number of Bytes
			char *buffer = new char[count];
			std::memcpy(buffer, ss.str().data(), count);

			int rcvrNode = 1;

			int err = MPI_Ssend(buffer, count, MPI::CHAR, 1, 0, world_Comm); // send buffer
			if (err != MPI::SUCCESS)
				printf("buffer failed to send! \n");

			availableNodes[rcvrNode] = false; // becomes unavailable until it finishes
			delete[] buffer;
		}

		//generic put blocking RMA
		void BcastPut(const void *origin_addr, int count,
					  MPI_Datatype mpi_datatype, MPI_Aint offset,
					  MPI_Win &window)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				MPI_Win_lock(MPI::LOCK_EXCLUSIVE, rank, 0, window);									  // open epoch
				MPI_Put(origin_addr, count, mpi_datatype, rank, offset, count, mpi_datatype, window); // put date through window
				MPI_Win_flush(rank, window);														  // complete RMA operation
				MPI_Win_unlock(rank, window);														  // close epoch
			}
		}

		/* this is supposed to be invoked only when there are available nodes
		returns -1 if no available node [it sould not happen] */
		auto findAvailableNode()
		{
			std::vector<int> nodes; // testing
			int availableNodeID = -1;
			for (int rank = 1; rank < world_size; rank++)
			{
				if (availableNodes[rank])
					nodes.push_back(rank);
			}

			std::random_device rd;	// Will be used to obtain a seed for the random number engine
			std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
			std::uniform_int_distribution<> distrib(0, nodes.size() - 1);

			int rank = distrib(gen);				 // testing
			availableNodeID = nodes[rank];			 // testing
			availableNodes[availableNodeID] = false; // testing

			return availableNodeID;
		}

		/* if other nodes become idle, they will put "true" into their cell at availableNodes*/
		void updateNumAvNodes()
		{
			int count = 0;
			for (int i = 1; i < world_size; i++)
			{
				if (availableNodes[i])
					++count;
			}
			numAvailableNodes[0] = count;

			//printf("numAvailableNodes up-to-date : %d \n", numAvailableNodes[0]);

			if (numAvailableNodes[0] != numAvailableNodes_old)
			{
				numAvailableNodes_old = numAvailableNodes[0];
				BcastPut(numAvailableNodes, 1, MPI::INT, 0, win_NumNodes); // this line broadcast to slave ranks
			}
		}

		void communicators()
		{
			MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library

			MPI_Comm_size(world_Comm, &this->world_size);
			MPI_Comm_rank(world_Comm, &this->world_rank);

			if (world_size < 2)
			{
				printf("At least two processes required !!\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}

			MPI_Comm_group(world_Comm, &world_group); // world group, all ranks

			// a communicator to syncronise only process 0 and 1 ****************************
			const int second_group_ranks[2] = {0, 1}; // build a ranks group in second_group

			int err = MPI_Group_incl(world_group, 2, second_group_ranks, &second_group); // include ranks in group
			if (err != MPI::SUCCESS)
				printf("MPI_Group_incl could not include ranks! \n");
			err = MPI_Comm_create_group(world_Comm, second_group, 0, &second_Comm); // creates the group
			if (err != MPI::SUCCESS)
				printf("MPI_group could not be created! \n");
			// ******************************************************************************

			MPI_Comm_dup(world_Comm, &SendToNodes_Comm);
			MPI_Comm_dup(world_Comm, &SendToCenter_Comm);
			MPI_Comm_dup(world_Comm, &NodeToNode_Comm);
			MPI_Comm_dup(world_Comm, &BCast_Comm);
			MPI_Comm_dup(world_Comm, &accumulator_Comm);
		}

		void win_allocate()
		{
			// anything outside the condition is applicable for all the processes
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, world_Comm, &refValueGlobal, &win_refValueGlobal);

			if (world_rank == 0)
			{
				// mpi mutex **********************************************************************************************
				MPI_Win_allocate(sizeof(bool), sizeof(bool), MPI::INFO_NULL, MPI_COMM_WORLD, &mpi_mutex.mutex, &win_mutex);
				// ********************************************************************************************************

				MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);
				MPI_Win_allocate(world_size * sizeof(bool), sizeof(bool), MPI::INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
				MPI_Win_allocate(world_size * sizeof(bool), sizeof(bool), MPI::INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(world_size * sizeof(bool), sizeof(bool), MPI::INFO_NULL, world_Comm, &inbox_bestResult, &win_inbox_bestResult);

				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(sizeof(bool), sizeof(bool), MPI::INFO_NULL, second_Comm, &finalFlag, &win_finalFlag);
			}
			else
			{
				// it is not required to allocate buffer memory for the other processes

				// mpi mutex **********************************************************************************************
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, MPI_COMM_WORLD, &mpi_mutex.mutex, &win_mutex);
				// ********************************************************************************************************

				MPI_Win_allocate(0, sizeof(int), MPI::INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, world_Comm, &inbox_bestResult, &win_inbox_bestResult);

				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, second_Comm, &finalFlag, &win_finalFlag);
			}

			//printf("Allocated\n");

			init();
		}

		void win_deallocate()
		{
			// mpi mutex ********************
			MPI_Win_free(&win_mutex);
			// ******************************

			MPI_Win_free(&win_accumulator);
			MPI_Win_free(&win_AvNodes);
			MPI_Win_free(&win_boolean);
			MPI_Win_free(&win_NumNodes);
			MPI_Win_free(&win_inbox_bestResult);
			MPI_Win_free(&win_refValueGlobal);

			if (MPI_COMM_NULL != second_Comm)
				MPI_Win_free(&win_finalFlag);

			MPI_Group_free(&second_group);
			MPI_Group_free(&world_group);

			if (MPI_COMM_NULL != second_Comm)
				MPI_Comm_free(&second_Comm);

			MPI_Comm_free(&SendToNodes_Comm);
			MPI_Comm_free(&SendToCenter_Comm);
			MPI_Comm_free(&NodeToNode_Comm);
			MPI_Comm_free(&BCast_Comm);
			MPI_Comm_free(&accumulator_Comm);
		}

		void init()
		{
			if (world_rank == 0)
			{
				// mpi mutex ********************
				mpi_mutex.mutex[0] = false;
				// ******************************

				bestResults.resize(world_size);

				numAvailableNodes[0] = world_size - 1;
				busyNodes[0] = 0;
				finalFlag[0] = false;
				for (int i = 0; i < world_size; i++)
				{
					inbox_boolean[i] = false;
					availableNodes[i] = false; // no node is available, each node is in charge of communicating its availability
					inbox_bestResult[i] = false;
				}
			}
			else
				numAvailableNodes[0] = world_size - 1;
		}

	private:
		int argc;
		char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Mutex mpi_mutex;
		MPI_Win win_mutex;

		MPI_Win win_boolean; // window for pushing request from nodes
		MPI_Win win_inbox_bestResult;
		MPI_Win win_refValueGlobal;
		MPI_Win win_finalFlag;
		MPI_Win win_NumNodes;	 // window for the number of available nodes
		MPI_Win win_AvNodes;	 // window for the list of available nodes
		MPI_Win win_accumulator; // windows for the busyNodes variable

		MPI_Group world_group;		// all ranks belong to this group
		MPI_Group second_group;		// only rank 0 & 1 belong to this group
		MPI_Comm world_Comm;		// world communicator
		MPI_Comm second_Comm;		// used to synchronise ranks 0 & 1
		MPI_Comm SendToNodes_Comm;	// exclusive communicator attached to the aformentionned groups
		MPI_Comm SendToCenter_Comm; // exclusive communicator attached to the aformentionned groups
		MPI_Comm NodeToNode_Comm;
		MPI_Comm BCast_Comm;	   // attached to number of nodes
		MPI_Comm accumulator_Comm; // attached to win_accumulator

		bool *inbox_boolean = nullptr;	  // receives signal of a node attempting to put data [only center node has the list]
		int *numAvailableNodes = nullptr; // Number of available nodes	[every node is aware of this number]
		int numAvailableNodes_old;
		bool *availableNodes = nullptr;	  // list of available nodes [only center node has the list]
		int *busyNodes = nullptr;		  // number of nodes working at the time
		bool *finalFlag = nullptr;		  // applicable only of waiting algorithms, it means that final result is ready
		int *refValueGlobal = nullptr;	  // reference value to chose a best result
		int refValueGlobal_old;			  // this help to avoid broadcasting if it hasn't changed
		bool *inbox_bestResult = nullptr; // a process is requesting to update the best result

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