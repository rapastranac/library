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
#include <queue>

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

		void retrieveResult(std::stringstream &ret)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (bestResults[rank].first == refValueGlobal[0])
				{
					int SIZE = bestResults[rank].second.str().size();
					fmt::print("Stream retrieved, size : {} \n", SIZE);
					ret << bestResults[rank].second.rdbuf();
					break;
				}
			}
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

		void allgather(void *recvbuf, void *sendbuf, MPI_Datatype mpi_datatype)
		{
			MPI_Allgather(sendbuf, 1, mpi_datatype, recvbuf, 1, mpi_datatype, world_Comm);
			MPI_Barrier(world_Comm);
		}

		int getWorldSize()
		{
			return world_size;
		}

		std::vector<int> executedTasksPerNode()
		{
			return tasks_per_node;
		}

	private:
		template <typename Holder, typename Serialize>
		void schedule(Holder &holder, Serialize &&serialize)
		{
			std::vector<std::queue<int>> static_next(world_size); // static load balancing
			std::vector<int> dynamic_next(world_size, -1);		  // when static is no longer useful
			built_processes_tree(static_next);
			std::vector<int> requesting(world_size, 1);
			int nodes = world_size - 1; // number of available nodes -- private to this method
			int busy = 0;
			std::vector<int> aNodes(world_size, 1); // list of nodes availability
			Bcast_availability(aNodes, nodes);
			sendSeed(static_next, aNodes, nodes, busy, holder, serialize);

			int rcv_availability = 0;
			tasks_per_node.resize(world_size, 0);

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

			while (true)
			{
				//fmt::print("nodes {}, busy {} \n", nodes, busy);
				MPI_Status status;
				int buffer;
				MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

				int src_rank = status.MPI_SOURCE;
				int TAG = status.MPI_TAG;

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
					requesting[src_rank] = 1;

					if (nodes > 0)
					{

						reply_requesters(requesting, static_next, aNodes, nodes, busy, src_rank);

						BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes  (not doing anything relevant for now)
						++approvedRequests;
					}
					else // this scenarios should not happen anymore
					{
						BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes
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

		void built_processes_tree(std::vector<std::queue<int>> &static_next)
		{
			int numNodes = world_size - 1;

			for (int depth = 0; depth < log2(numNodes); depth++)
			{
				for (int rank = 1; rank <= pow(2, depth); rank++)
				{
					int child = next_node(numNodes, rank, depth);
					static_next[rank].push(child);
					fmt::print("process: {}, child: {}\n", rank, child);
				}
			}

			for (int i = 1; i < world_size; i++)
			{
				while (!static_next[i].empty())
				{
					int child = static_next[i].front();
					static_next[i].pop();
					fmt::print("rank: {}, child: {}\n", i, child);
				}
			}
		}

		int next_node(int numNodes, int rank, int depth)
		{
			int child = rank + pow(2, depth);

			if (child > numNodes)
				return -1;
			else
				return child;
		}

		void reply_requesters(std::vector<int> &requesting,
							  std::vector<std::queue<int>> &static_next,
							  std::vector<int> aNodes,
							  int nodes, int busy, int src_rank)
		{
			static bool flag = true;
			static int count = world_size - 1;

			if (flag)
			{
				if (!static_next[src_rank].empty()) // if empty, does not mean that all processes have
				{
					int next = static_next[src_rank].front();
					static_next[src_rank].pop();
					count--;
					if (count == 0)
						flag = false;
				}
			}

			for (int rank = 1; rank < world_size - 1; rank++)
			{
				int k = isAvailable(aNodes);

				if (k == -1)
					break; // no more available nodes

				if (aNodes[k] == 0)
					fmt::print("***********************ERROR************************** aNodes[{}] == 0\n ", k);

				aNodes[k] = 0;
				--nodes;
				++busy;
				put(&k, 1, src_rank, MPI_INT, 0, win_NextNode); // puts the next available node, worker does not need to receive
				tasks_per_node[k]++;
			}
		}

		void correct_dynamic_next(std::vector<int> &dynamic_next, std::vector<int> aNodes)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if ((aNodes[rank] == 1) && (dynamic_next[rank] != -1))
				{
					int next = dynamic_next[rank];
					if (aNodes[next] == 0)
						aNodes[next] = 0;
				}
			}
		}
		// first synchronisation, thus rank 0 is aware of other availability
		// the purpose is to broadcast the total availability to other ranks
		void
		Bcast_availability(std::vector<int> &aNodes, int &nodes)
		{
			aNodes[0] = 0;
			MPI_Bcast(&nodes, 1, MPI_INT, 0, world_Comm);
			MPI_Barrier(world_Comm); // synchronise process in world group
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
				fmt::print("solution received from {}, Bytes : {}, refVal {} \n", rank, Bytes, status.MPI_TAG);

				auto &ss = bestResults[rank].second; // it should be empty

				for (int i = 0; i < Bytes; i++)
				{
					ss << buffer[i];
				}

				bestResults[rank].first = TAG; // reference value corresponding to result
				//bestResults[rank].second = std::move(ss); // best result so far from this rank

				delete[] buffer;
			}
		}

		template <typename Holder, typename Serialize>
		void sendSeed(std::vector<std::queue<int>> &static_next, std::vector<int> &aNodes, int &nodes, int &busy, Holder &holder, Serialize &&serialize)
		{
			const int dest = 1;
			// global synchronisation **********************
			--nodes;
			++busy;
			aNodes[dest] = 0;
			BcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes

			// *********************************************

			// put next node *******************************
			if (!static_next[dest].empty()) // there might be a single worker
			{
				int next = static_next[dest].front();
				static_next[dest].pop();
				aNodes[next] = 0; // prioritized for node 1
				put(&next, 1, dest, MPI_INT, 0, win_NextNode);
			}
			// *********************************************

			std::stringstream _stream;
			auto __f = std::bind_front(serialize, std::ref(_stream));
			std::apply(__f, holder.getArgs());
			int count = _stream.str().size(); // number of Bytes

			int err = MPI_Ssend(_stream.str().data(), count, MPI_CHAR, dest, 0, world_Comm); // send buffer
			if (err != MPI_SUCCESS)
				fmt::print("buffer failed to send! \n");

			fmt::print("Seed sent \n");
		}

		void put(const void *origin_addr, int count, int dest_rank, MPI_Datatype mpi_datatype, MPI_Aint offset, MPI_Win &window)
		{
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dest_rank, 0, window);									   // open epoch
			MPI_Put(origin_addr, count, mpi_datatype, dest_rank, offset, count, mpi_datatype, window); // put date through window
			MPI_Win_flush(dest_rank, window);														   // complete RMA operation
			MPI_Win_unlock(dest_rank, window);														   // close epoch
		}

		//generic put blocking RMA
		void BcastPut(const void *origin_addr, int count, MPI_Datatype mpi_datatype, MPI_Aint offset, MPI_Win &window)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, window);									  // open epoch
				MPI_Put(origin_addr, count, mpi_datatype, rank, offset, count, mpi_datatype, window); // put date through window
				MPI_Win_flush(rank, window);														  // complete RMA operation
				MPI_Win_unlock(rank, window);														  // close epoch
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
			MPI_Comm_dup(world_Comm, &numNodes_Comm);
		}

		void win_allocate()
		{
			MPI_Barrier(world_Comm);
			// applicable for all the processes
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &numAvailableNodes, &win_NumNodes);
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, world_Comm, &refValueGlobal, &win_refValueGlobal);
			MPI_Win_allocate(2 * sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &nextNode, &win_NextNode);

			init();
			MPI_Barrier(world_Comm);
		}

		void win_deallocate()
		{
			MPI_Win_free(&win_NumNodes);
			MPI_Win_free(&win_refValueGlobal);
			MPI_Win_free(&win_NextNode);
			MPI_Group_free(&world_group);
		}

		void init()
		{
			numAvailableNodes[0] = 0;
			nextNode[0] = -1; // -1 one means it has no next node assigned
			nextNode[1] = -1; // -1 one means it has no next node assigned

			if (world_rank == 0)
			{
				bestResults.resize(world_size);
			}
		}

	private:
		int argc;
		char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Win win_NumNodes;		// window for the number of available nodes
		MPI_Win win_refValueGlobal; // window to send reference value global
		MPI_Win win_NextNode;

		MPI_Group world_group;	// all ranks belong to this group
		MPI_Comm numNodes_Comm; // attached to win_NumNodes
		MPI_Comm world_Comm;	// world communicator

		int *numAvailableNodes = nullptr; // Number of available nodes	[every node is aware of this number]
		int *refValueGlobal = nullptr;	  // reference value to chose a best result
		int *nextNode = nullptr;		  // potentially to replace numAvailableNodes

		std::stringstream returnStream;
		std::vector<std::pair<int, std::stringstream>> bestResults;

		size_t threadsPerNode = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

		std::vector<int> tasks_per_node;

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