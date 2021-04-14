#pragma once
#ifndef MPI_SCHEDULER_HPP
#define MPI_SCHEDULER_HPP

#include "StreamHandler.hpp"
#include <ResultHolder.hpp>

#include <condition_variable>
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

#define TAG_TERMINATE 3
#define TAG_AVAILABLE 4
#define TAG_PUSH_REQUEST 5
#define TAG_RESULT_REQUEST 6
#define TAG_NO_RESULT 7
#define TAG_REF_VAL_REQUEST 8
#define TAG_REF_VAL_UPDATE 9
#define TAG_RUNNING 22
#define TAG_NEXT_NODE_REQUEST 23

#define STATE_RUNNING 1
#define STATE_ASSIGNED 2
#define STATE_AVAILABLE 3

namespace GemPBA
{
	template <typename _Ret, typename... Args>
	class ResultHolder;

	// inter process communication handler
	class MPI_Scheduler
	{

	public:
		static MPI_Scheduler &getInstance()
		{
			static MPI_Scheduler instance;
			return instance;
		}

		void setThreadsPerNode(size_t threadsPerNode)
		{
			this->threadsPerNode = threadsPerNode;
		}

		int establishIPC(int *argc, char *argv[])
		{
			// Initilialise MPI and ask for thread support
			int provided;
			MPI_Init_thread(argc, &argv, MPI_THREAD_FUNNELED, &provided);

			if (provided < MPI_THREAD_FUNNELED)
			{
				fmt::print("The threading support level is lesser than that demanded.\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}

			createCommunicators();

			int namelen;
			MPI_Get_processor_name(processor_name, &namelen);
			fmt::print("Process {} of {} is on {}\n", world_rank, world_size, processor_name);
			allocateMPI();

			return world_rank;
		}

		void start(const char *buffer, const int count)
		{

			start_time = MPI_Wtime();
			schedule(buffer, count);
			end_time = MPI_Wtime();
		}

		void finalize()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, before deallocate \n", world_rank);
#endif
			deallocateMPI();
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

		void barrier()
		{
			MPI_Barrier(world_Comm);
		}

		void listen(auto &&receiver)
		{
			int count_rcv = 0;

			while (true)
			{
				MPI_Status status;
				int count; // bytes to be received

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &count);					 // receives total number of datatype elements of the message

				char *incoming_buffer = new char[count];
				MPI_Recv(incoming_buffer, count, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);
				count_rcv++;
				int src = status.MPI_SOURCE; // it might be useful for non-void functions
				int TAG = status.MPI_TAG;

				if (TAG == TAG_TERMINATE)
				{
					delete incoming_buffer;
					fmt::print("rank {} exited\n", world_rank);
					break;
				}
				notifyStateToCenter();

				fmt::print("rank {}, received buffer from rank {}\n", world_rank, src);
				//  push to the thread pool *********************************************************************
				auto *holder = receiver(incoming_buffer, count); // holder might be useful for non-void functions
				// **********************************************************************************************

				// buffers created by thread are to be sent with the next loop
				while (true)
				{
					sendBufferToNextNode();
					// TODO tell center node
					wait();
					if (notifyAvlToCenter())
						break;

					fmt::print("rank {}, notification received, sending buffer to rank {}\n", world_rank, newNode);
				}

				delete holder;
				delete incoming_buffer;
			}
		}

		bool notifyAvlToCenter()
		{
			if (idle_node_signal)
			{
				int buffer = 0;
				MPI_Ssend(&buffer, 1, MPI_INT, 0, TAG_AVAILABLE, world_Comm);
				idle_node_signal = false;
				return true;
			}
			return false;
		}

		// if current node has no assigned node already, a nextNode request is made to center

		void notifyStateToCenter()
		{

			int buffer = 0;
			MPI_Ssend(&buffer, 1, MPI_INT, 0, TAG_NEXT_NODE_REQUEST, world_Comm);
		}

		void notifyRunningToCenter()
		{
			int buf = 0;
			MPI_Ssend(&buf, 1, MPI_INT, 0, TAG_RUNNING, world_Comm);
		}

		void sendBufferToNextNode()
		{
			if (!streamHandler.empty())
			{
				std::unique_lock<std::mutex> lck(mtx);

				fmt::print("rank {} about to send buffer to rank {}\n", world_rank, newNode);
				auto stringbuffer = streamHandler.string_buf();

				MPI_Ssend(stringbuffer.data(), stringbuffer.size(), MPI_CHAR, newNode, 0, world_Comm);
				streamHandler.clear();
				fmt::print("rank {} sent buffer to rank {}\n", world_rank, newNode);
			}
			//			node_request_signal = false;
		}

		void schedule(const char *SEED, const int SEED_SIZE)
		{
			builtProcessesTopology();
			//std::vector<int> requesting(world_size, 1);
			sendSeed(nWorking, SEED, SEED_SIZE);

			int rcv_availability = 0;
			tasks_per_node.resize(world_size, 0);

			while (true)
			{
				//fmt::print("nodes {}, nWorking {} \n", nodes, nWorking);
				MPI_Status status;
				int buffer;
				MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

				int src_rank = status.MPI_SOURCE;
				int TAG = status.MPI_TAG;

				switch (TAG)
				{
				case TAG_AVAILABLE:
				{
					++rcv_availability;
					//#ifdef DEBUG_COMMENTS
					//					fmt::print("rank {} availability received {} times\n", world_rank, rcv_availability);
					//#endif
					//					if (nodeState[src_rank] == STATE_AVAILABLE)
					//						fmt::print("***********************ERROR************************** aNodes[{}] == 1\n ", src_rank);
					nodeState[src_rank] = STATE_AVAILABLE;
					--nWorking;
					++nAvailable;
					//bcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes
				}
				break;
					//case TAG_PUSH_REQUEST:
					//{
					//	requesting[src_rank] = 1;
					//	if (nodes > 0)
					//	{
					//		//reply_requesters(requesting, static_next, aNodes, nodes, nWorking, src_rank);
					//		bcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes  (not doing anything relevant for now)
					//		++approvedRequests;
					//	}
					//	else // this scenarios should not happen anymore
					//	{
					//		bcastPut(&nodes, 1, MPI_INT, 0, win_NumNodes); // Broadcast number of nodes
					//		++failedRequests;
					//	}
					//	++totalRequests;
					//}
					//break;
				case TAG_REF_VAL_REQUEST:
				{
					// send ref value global
					MPI_Ssend(refValueGlobal, 1, MPI_INT, src_rank, 0, world_Comm);

					// receive either the update ref value global or just pass
					MPI_Recv(&buffer, 1, MPI_INT, src_rank, MPI_ANY_TAG, world_Comm, &status);
					int TAG2 = status.MPI_TAG;
					if (TAG2 == TAG_REF_VAL_UPDATE)
					{
						refValueGlobal[0] = buffer;
						bcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);
					}
				}
				break;

				case TAG_RUNNING:
				{
				}
				break;

				case TAG_NEXT_NODE_REQUEST:
				{
					nodeState[src_rank] = STATE_RUNNING;
					++nWorking;

					int nxt = isAvailable();
					nodeState[nxt] = STATE_ASSIGNED;

					--nAvailable;
					MPI_Ssend(&nxt, 1, MPI_INT, src_rank, 0, world_Comm);
				}
				break;
				}

				if (breakLoop())
					break;
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

		void shift_left(int v[], const int size)
		{
			for (int i = 0; i < (size - 1); i++)
			{
				if (v[i] != -1)
					v[i] = v[i + 1]; // shift one cell to the left
				else
					break; // no more data
			}
		}

		bool try_next_node(auto &serializer, auto &tuple)
		{
			while (isTransmitting)
				;
			std::unique_lock<std::mutex> lck(mtx);

			if (nextNode[0] != -1)
			{
				newNode = nextNode[0];
				streamHandler.build_buffer(serializer, tuple);
				shift_left(nextNode, world_size);
				notifyNodeRequest();
				return true;
			}

			return false;
		}

		void notifyNodeRequest()
		{
			isTransmitting = true;
			node_request_signal = true;
			cv.notify_one();
		}

		void notifyHasNoTasks()
		{
			//fmt::print("rank {} about to notify out of tasks\n", world_rank);
			std::unique_lock<std::mutex> lck(mtx);
			no_tasks_signal = true;
			cv.notify_one();
			//fmt::print("rank {} notified out of tasks\n", world_rank);
		}

		void wait()
		{
			std::unique_lock<std::mutex> lck(mtx);
			isTransmitting = false;
			cv.wait(lck, [this]() {
				return false || node_request_signal || no_tasks_signal;
			});
		}

		void builtProcessesTopology()
		{
			int numNodes = world_size - 1;
			nAvailable = world_size - 1;
			// this loop builds the tree distribution
			for (int depth = 0; depth < log2(numNodes); depth++)
			{
				for (int rank = 1; rank <= pow(2, depth); rank++)
				{
					int child = getNextNode(numNodes, rank, depth);
					nodesTopology[rank].insert(child);
					fmt::print("process: {}, child: {}\n", rank, child);
				}
			}
			// put into nodes
			for (int rank = 1; rank < world_size; rank++)
			{
				int offset = 0;
				for (auto &child : nodesTopology[rank])
				{
					put(&child, 1, rank, MPI_INT, offset, win_NextNode);
					nodeState[child] = STATE_ASSIGNED;
					--nAvailable;
					++offset;
				}
			}
		}

		int getNextNode(int numNodes, int rank, int depth)
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
							  int nodes, int nWorking, int src_rank)
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
				int k = isAvailable();

				if (k == -1)
					break; // no more available nodes

				if (aNodes[k] == 0)
					fmt::print("***********************ERROR************************** aNodes[{}] == 0\n ", k);

				aNodes[k] = 0;
				--nodes;
				++nWorking;
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
		bcastAvailability(std::vector<int> &aNodes, int &nodes)
		{
			aNodes[0] = 0;
			MPI_Bcast(&nodes, 1, MPI_INT, 0, world_Comm);
			MPI_Barrier(world_Comm); // synchronise process in world group
		}

		int isAvailable()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (nodeState[rank] == STATE_AVAILABLE)
					return rank;
			}
			return -1; // this should not happen
		}

		// this sends the termination signal
		bool breakLoop()
		{
			//std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 4 testing
			//fmt::print("test, nWorking = {}\n", nWorking[0]);

			if (nAvailable == (world_size - 1) && nWorking == 0)
			{
				fmt::print("Termination achieved: nodes {}, nWorking {} \n", nAvailable, nWorking);
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

		void sendSeed(int &nWorking, const char *buffer, const int COUNT)
		{
			const int dest = 1;
			// global synchronisation **********************
			++nWorking;
			--nAvailable;
			nodeState[dest] = STATE_RUNNING;
			// *********************************************

			int err = MPI_Ssend(buffer, COUNT, MPI_CHAR, dest, 0, world_Comm); // send buffer
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
		void bcastPut(const void *origin_addr, int count, MPI_Datatype mpi_datatype, MPI_Aint offset, MPI_Win &window)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, window);									  // open epoch
				MPI_Put(origin_addr, count, mpi_datatype, rank, offset, count, mpi_datatype, window); // put date through window
				MPI_Win_flush(rank, window);														  // complete RMA operation
				MPI_Win_unlock(rank, window);														  // close epoch
			}
		}

		void createCommunicators()
		{
			MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library

			MPI_Comm_size(world_Comm, &this->world_size);
			MPI_Comm_rank(world_Comm, &this->world_rank);

			/*if (world_size < 2)
			{
				fmt::print("At least two processes required !!\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}*/

			MPI_Comm_group(world_Comm, &world_group); // world group, all ranks
			MPI_Comm_dup(world_Comm, &numNodes_Comm);
		}

		void allocateMPI()
		{
			MPI_Barrier(world_Comm);
			// applicable for all the processes
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &numAvailableNodes, &win_NumNodes);
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, world_Comm, &refValueGlobal, &win_refValueGlobal);
			MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &nextNode, &win_NextNode);

			init();
			MPI_Barrier(world_Comm);
		}

		void deallocateMPI()
		{
			MPI_Win_free(&win_NumNodes);
			MPI_Win_free(&win_refValueGlobal);
			MPI_Win_free(&win_NextNode);
			MPI_Group_free(&world_group);
		}

		void init()
		{
			numAvailableNodes[0] = 0;
			nodeState.resize(world_size, STATE_AVAILABLE);

			for (int i = 0; i < world_size; i++)
			{
				nextNode[i] = -1; // -1 one means it has no next node assigned
			}

			if (world_rank == 0)
			{
				bestResults.resize(world_size);
			}
		}

		int getRank()
		{
			return world_rank;
		}

	private:
		int argc;
		char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		int nWorking = 0;
		int nAvailable = 0;
		std::vector<int> nodeState;					// state of the nodes : running, assgined or available
		std::map<int, std::set<int>> nodesTopology; // order map and set, determine next node to push to

		std::mutex mtx;
		std::condition_variable cv;
		bool isTransmitting = false;
		bool node_request_signal = false;
		bool no_tasks_signal = false;
		bool idle_node_signal = false;
		int newNode;

		StreamHandler streamHandler; // threads in BranchHandler build this buffer when there's an available node

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
		MPI_Scheduler() {}
	};

} // namespace GemPBA

#endif