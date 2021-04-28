#pragma once
#ifndef MPI_SCHEDULER_HPP
#define MPI_SCHEDULER_HPP

#include "StreamHandler.hpp"
#include "Tree.hpp"
#include <Queue.hpp>

#include <condition_variable>
#include <cstring>
#include <random>
#include <stdlib.h> /* srand, rand */
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <string>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <time.h>
#include <thread>
#include <queue>

#define STATE_RUNNING 1
#define STATE_ASSIGNED 2
#define STATE_AVAILABLE 3
#define STATE_NEED_NXT_NODE 4

#define NO_NODE_ASSIGNED 5

#define ACTION_TERMINATE 6
#define ACTION_PUSH_REQUEST 7

#define ACTION_REF_VAL_REQUEST 8
#define ACTION_REF_VAL_UPDATE 9
#define ACTION_MAXIMISE_REF_VALUE 10
#define ACTION_MINIMISE_REF_VALUE 11

#define TAG_HAS_RESULT 12
#define TAG_NO_RESULT 13

#define TIMEOUT_TIME 3

namespace GemPBA
{
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

		int init(int *argc, char *argv[])
		{
			// Initialise MPI and ask for thread support
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

		std::string fetchSolution()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (bestResults[rank].first == refValueGlobal[0])
				{
					return bestResults[rank].second;
				}
			}
		}

		auto fetchResVec()
		{
			return bestResults;
		}

		void printStats()
		{
			fmt::print("\n \n \n");
			fmt::print("*****************************************************\n");
			fmt::print("Elapsed time : {:4.3f} \n", elapsedTime());
			fmt::print("Total number of requests : {} \n", totalRequests);
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
			if (world_Comm != MPI_COMM_NULL)
				MPI_Barrier(world_Comm);
		}

		void runNode(auto &branchHandler, auto &&bufferDecoder, auto &&resultFetcher, auto &&serializer)
		{
			MPI_Barrier(world_Comm);

			while (true)
			{
				MPI_Status status;
				int count; // count to be received

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &count);					 // receives total number of datatype elements of the message

				char *incoming_buffer = new char[count];
				MPI_Recv(incoming_buffer, count, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);
				recvdMessages++;

				if (isTerminated(status.MPI_TAG, incoming_buffer))
					break;

				notifyRunningState();

				if (isTransmitting)
				{
					fmt::print("rank {} is transmitting, received from {} this should not happen \n", world_rank, status.MPI_SOURCE);
					throw;
				}

				fmt::print("rank {}, received buffer from rank {}\n", world_rank, status.MPI_SOURCE);
				//  push to the thread pool *********************************************************************
				auto *holder = bufferDecoder(incoming_buffer, count); // holder might be useful for non-void functions
				fmt::print("rank {}, pushed buffer to thread pool \n", world_rank, status.MPI_SOURCE);
				// **********************************************************************************************

				taskFunneling(branchHandler);
				notifyStateAvailable();

				//delete holder;
				delete[] incoming_buffer;
			}

			// TODO.. send result

			sendSolution(resultFetcher);
		}

		void taskFunneling(auto &branchHandler)
		{
			std::string *buffer;
			bool isPop = q.pop(buffer);

			while (true)
			{
				while (isPop)
				{
					std::unique_ptr<std::string> ptr(buffer);

					nTasks++;
					sendNextNode(*buffer);

					isPop = q.pop(buffer);

					if (!isPop)
						isTransmitting = false;
				}
				isPop = q.pop(buffer);

				if (!isPop && branchHandler.isDone())
				{
					/* by the time the thread realises that the thread pool has no more tasks, 
						there might be another buffer pushed, which should be verified in the next line*/
					isPop = q.pop(buffer);
					if (!isPop)
					{
						fmt::print("rank {} sent {} tasks\n", world_rank, nTasks);
						break;
					}
				}
			}
			/* to reuse the task funneling, otherwise it will exit 
			right away the second time the process receives a task*/
		}

		// push a task to be sent via MPI
		void push(std::string &&buffer)
		{
			auto pck = std::make_shared<std::string>(std::forward<std::string &&>(buffer));
			auto _buffer = new std::string(*pck);
			if (!q.empty())
			{
				fmt::print("ERROR: q is not empty !!!!\n");
				throw;
			}

			q.push(_buffer);
		}

		/*
        - this method attempts pushing to other nodes, only ONE buffer will be enqueued at a time
        - if the taskFunneling is transmitting the buffer to another node, this method will return false
        - if previous conditions are met, then actual condition for pushing is evaluated nextNode[0] > 0
        */
		bool tryPush(auto &getBuffer)
		{
			std::unique_lock<std::mutex> lck(mtx, std::defer_lock);
			if (lck.try_lock() && !isTransmitting)
			{
				if (nextNode[0] > 0)
				{
					isTransmitting = true;

					newNode = nextNode[0];
					fmt::print("rank {} entered MPI_Scheduler::tryPush(..) for the node {}\n", world_rank, newNode);

					shift_left(nextNode, world_size);

					push(getBuffer());
					return true;
				}
				lck.unlock(); // NEVER FORGET THIS ONE
			}
			return false;
		}

		bool isTerminated(int TAG, char *buffer)
		{
			if (TAG == ACTION_TERMINATE)
			{
				delete[] buffer;
				fmt::print("rank {} exited\n", world_rank);
				MPI_Barrier(world_Comm);
				return true;
			}
			return false;
		}

		void notifyStateAvailable()
		{
			int buffer = 0;
			MPI_Ssend(&buffer, 1, MPI_INT, 0, STATE_AVAILABLE, world_Comm);
			fmt::print("rank {} entered notifyStateAvailable()\n", world_rank);
		}

		void notifyRunningState()
		{
			int buffer = 0;
			MPI_Ssend(&buffer, 1, MPI_INT, 0, STATE_RUNNING, world_Comm);
		}

		void sendNextNode(std::string &buffer)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (newNode > 0)
			{
				if (newNode == world_rank)
				{
					fmt::print("rank {} attempting to send to itself !!!\n", world_rank);
					throw;
				}

				fmt::print("rank {} about to send buffer to rank {}\n", world_rank, newNode);
				MPI_Ssend(buffer.data(), buffer.size(), MPI_CHAR, newNode, 0, world_Comm);
				fmt::print("rank {} sent buffer to rank {}\n", world_rank, newNode);
				newNode = -1;
			}
			else
			{
				fmt::print("rank {}, target rank is {}, something happened\n", world_rank, newNode);
				throw;
			}
		}

		/* shift a position to left of an array, leaving -1 as default value*/
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

		void createNodesTopology()
		{
			//world_size = 5;
			int numNodes = world_size - 1;
			nAvailable = world_size - 1;
			// this loop builds the tree distribution
			for (int depth = 0; depth < log2(numNodes); depth++)
			{
				for (int rank = 1; rank <= pow(2, depth); rank++)
				{
					int child = getNextNode(numNodes, rank, depth);
					if (child > 0)
					{
						processTree[rank].addNext(child);
						fmt::print("process: {}, child: {}\n", rank, child);
					}
				}
			}
		}
		/*	each nodes has an array containing its children were it is going to send tasks,
			this method puts the rank of these nodes into the array in the order that they
			are supposed to help the parent 
		*/
		void assignNodes()
		{
			// put into nodes
			for (int rank = 1; rank < world_size; rank++)
			{
				int offset = 0;
				for (auto &child : processTree[rank])
				{
					put(&child, 1, rank, MPI_INT, offset, win_NextNode);
					nodeState[child] = STATE_ASSIGNED;
					--nAvailable;
					++offset;
				}
			}
		}

		/*	given a rank ID, this method returns its child
			TODO .. adapt it for multi-branching
		*/
		int getNextNode(int numNodes, int rank, int depth)
		{
			int child = rank + pow(2, depth);

			if (child > numNodes)
				return -1;
			else
				return child;
		}

		/*	send solution attained from node to the center node */
		void sendSolution(auto &&resultFetcher)
		{
			auto [refVal, buffer] = resultFetcher();
			if (buffer.starts_with("Empty"))
			{
				MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, TAG_NO_RESULT, world_Comm);
			}
			else
			{
				MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, TAG_HAS_RESULT, world_Comm);
				MPI_Send(&refVal, 1, MPI_INT, 0, TAG_HAS_RESULT, world_Comm);
			}
		}

		/* it returns the substraction between end and double*/
		double difftime(double start, double end)
		{
			return end - start;
		}

		/*	run the center node */
		void runCenter(const char *SEED, const int SEED_SIZE)
		{
			MPI_Barrier(world_Comm);
			start_time = MPI_Wtime();

			createNodesTopology();
			assignNodes();
			sendSeed(SEED, SEED_SIZE);

			int rcv_availability = 0;

			while (true)
			{
				//fmt::print("nodes {}, nRunning {} \n", nodes, nRunning);
				MPI_Status status;
				MPI_Request request;
				int buffer;
				int ready;
				//MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

				double begin = MPI_Wtime();
				MPI_Irecv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &request);

			awaitPendingMsg:
				MPI_Test(&request, &ready, &status);

				// Check whether the underlying communication had already taken place
				int cycles = 0;
				while (!ready && difftime(begin, MPI_Wtime()) < TIMEOUT_TIME)
				{
					MPI_Test(&request, &ready, &status);
					cycles++;
				}

				if (!ready)
				{
					if (nRunning == 0)
					{
						// Cancellation due to TIMEOUT
						MPI_Cancel(&request);
						MPI_Request_free(&request);
						printf("rank %d: receiving TIMEOUT, buffer : %d, cycles : %d\n", world_rank, buffer, cycles);
						break;
					}
					/* if it reaches this point, then there's still a pending message
					so the center keeps trying to receive it without making another call
					to MPI_Irecv(..)
					*/
					goto awaitPendingMsg;
				}

				switch (status.MPI_TAG)
				{
				case STATE_RUNNING: // received if and only if a worker receives from other but center
				{
					nodeState[status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
					++nRunning;
					fmt::print("rank {} reported running, nRunning :{}\n", status.MPI_SOURCE, nRunning);

					if (processTree[status.MPI_SOURCE].isAssigned())
						processTree[status.MPI_SOURCE].release();

					if (!processTree[status.MPI_SOURCE].hasNext()) // checks if notifying node has a child to push to
					{
						int nxt = getAvailable();
						if (nxt > 0)
						{
							put(&nxt, 1, status.MPI_SOURCE, MPI_INT, 0, win_NextNode);
							processTree[status.MPI_SOURCE].addNext(nxt);
							nodeState[nxt] = STATE_ASSIGNED;
							--nAvailable;
						}
					}
					tasks_per_node[status.MPI_SOURCE]++;
					totalRequests++;
				}
				break;
				case STATE_AVAILABLE:
				{
					++rcv_availability;
					nodeState[status.MPI_SOURCE] = STATE_AVAILABLE;
					++nAvailable;
					--nRunning;

					if (processTree[status.MPI_SOURCE].isAssigned())
						processTree[status.MPI_SOURCE].release();

					fmt::print("rank {} reported available, nRunning :{}\n", status.MPI_SOURCE, nRunning);

					for (int rank = 1; rank < world_size; rank++)
					{
						if (nodeState[rank] == STATE_RUNNING) // finds the first running node
						{
							if (!processTree[rank].hasNext()) // checks if running node has a child to push to
							{
								put(&status.MPI_SOURCE, 1, rank, MPI_INT, 0, win_NextNode); // assigns returning node to the running node
								processTree[rank].addNext(status.MPI_SOURCE);				// assigns returning node to the running node
								nodeState[status.MPI_SOURCE] = STATE_ASSIGNED;				// it flags returning node as assigned
								--nAvailable;												// assigned, not available any more
								break;														// breaks for-loop
							}
						}
					}
				}
				break;
				case ACTION_REF_VAL_UPDATE:
				{
					// send ref value global
					MPI_Ssend(refValueGlobal, 1, MPI_INT, status.MPI_SOURCE, 0, world_Comm);

					// receive either the update ref value global or just pass
					MPI_Recv(&buffer, 1, MPI_INT, status.MPI_SOURCE, MPI_ANY_TAG, world_Comm, &status);
					int TAG2 = status.MPI_TAG;
					if (TAG2 == ACTION_REF_VAL_UPDATE)
					{
						refValueGlobal[0] = buffer;
						bcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);
					}
				}
				break;
				}
			}

			/*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
			notifyTermination();

			// receive solution from other processes
			receiveSolution();

			end_time = MPI_Wtime();
		}

		void notifyTermination()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				char buffer[] = "exit signal";
				int count = sizeof(buffer);
				MPI_Send(&buffer, count, MPI_CHAR, rank, ACTION_TERMINATE, world_Comm); // send positive signal
			}
			MPI_Barrier(world_Comm);
		}

		int getAvailable()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (nodeState[rank] == STATE_AVAILABLE)
					return rank;
			}
			return -1; // all nodes are running
		}

		/*	receive solution from nodes */
		void receiveSolution()
		{
			// order order order order
			//int nodes = world_size - 1;

			for (int rank = 1; rank < world_size; rank++)
			{

				MPI_Status status;
				int count;
				// sender would not need to send data size before hand **********************************************
				MPI_Probe(rank, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR,
							  &count); // receives total number of datatype elements of the message
				//***************************************************************************************************

				char *buffer = new char[count];
				MPI_Recv(buffer, count, MPI_CHAR, rank, MPI_ANY_TAG, world_Comm, &status);

#ifdef DEBUG_COMMENTS
				fmt::print("fetching result from rank {} \n", rank);
#endif

				switch (status.MPI_TAG)
				{
				case TAG_HAS_RESULT:
				{
					std::string buf(buffer, count);

					int refValue;
					MPI_Recv(&refValue, 1, MPI_INT, rank, TAG_HAS_RESULT, world_Comm, &status);

					bestResults[rank].first = refValue; // reference value corresponding to result
					bestResults[rank].second = buf;		// best result so far from this rank

					delete[] buffer;

					fmt::print("solution received from {}, count : {}, refVal {} \n", rank, count, refValue);
				}
				break;

				case TAG_NO_RESULT:
				{
					delete[] buffer;
					fmt::print("solution NOT received from rank {}\n", rank);
				}
				break;
				}
			}
		}

		void sendSeed(const char *buffer, const int COUNT)
		{
			const int dest = 1;
			// global synchronisation **********************
			--nAvailable;
			nodeState[dest] = STATE_RUNNING;
			++tasks_per_node[dest];
			// *********************************************

			int err = MPI_Ssend(buffer, COUNT, MPI_CHAR, dest, 0, world_Comm); // send buffer
			if (err != MPI_SUCCESS)
				fmt::print("buffer failed to send! \n");

			fmt::print("Seed sent \n");
		}

		void put(const void *origin_addr, int count, int dest_rank, MPI_Datatype mpi_datatype, MPI_Aint offset,
				 MPI_Win &window)
		{
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dest_rank, 0, window); // open epoch
			MPI_Put(origin_addr, count, mpi_datatype, dest_rank, offset, count, mpi_datatype,
					window); // put date through window
			MPI_Win_flush(dest_rank,
						  window);			   // complete RMA operation
			MPI_Win_unlock(dest_rank, window); // close epoch
		}

		//generic put blocking RMA
		void bcastPut(const void *origin_addr, int count, MPI_Datatype mpi_datatype, MPI_Aint offset, MPI_Win &window)
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, window); // open epoch
				MPI_Put(origin_addr, count, mpi_datatype, rank, offset, count, mpi_datatype,
						window); // put date through window
				MPI_Win_flush(rank,
							  window);		  // complete RMA operation
				MPI_Win_unlock(rank, window); // close epoch
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
			MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, numNodes_Comm, &nextNode,
							 &win_NextNode);

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
			tasks_per_node.resize(world_size, 0);
			nodeState.resize(world_size, STATE_AVAILABLE);
			processTree.resize(world_size);

			for (int i = 0; i < world_size; i++)
			{
				nextNode[i] = -1; // -1 one means it has no next node assigned
			}

			if (world_rank == 0)
			{
				bestResults.resize(world_size, std::make_pair(-1, std::string()));
			}
		}

		int getRank()
		{
			return world_rank;
		}

		void linkRefValue(int **refValueNodes)
		{
			if (refValueGlobal)
				*refValueNodes = refValueGlobal;
			else
			{
				fmt::print("Error, refValueGlobal has not been initialized in MPI_Scheduler.hpp\n");
				throw std::runtime_error("Error, refValueGlobal has not been initialized in MPI_Scheduler.hpp\n");
			}
		}

		void updateRefValue(int val, std::string_view keyword)
		{
			MPI_Ssend(&val, 1, MPI_INT, 0, ACTION_REF_VAL_UPDATE, world_Comm);
		}

	private:
		int argc;
		char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		int nTasks = 0;
		int nRunning = 0;
		int nAvailable = 0;
		std::vector<int> nodeState;					// state of the nodes : running, assigned or available
		std::map<int, std::set<int>> nodesTopology; // order map and set, determine next node to push to
		Tree processTree;

		std::mutex mtx;
		std::condition_variable cv;
		bool isTransmitting = false;
		int newNode = -1;

		detail::Queue<std::string *> q;
		bool exit = false;

		MPI_Win win_NumNodes;		// window for the number of available nodes
		MPI_Win win_refValueGlobal; // window to send reference value global
		MPI_Win win_NextNode;

		MPI_Group world_group;	// all ranks belong to this group
		MPI_Comm numNodes_Comm; // attached to win_NumNodes
		MPI_Comm world_Comm;	// world communicator

		int *numAvailableNodes = nullptr; // Number of available nodes	[every node is aware of this number]
		int *refValueGlobal = nullptr;	  // reference value to chose a best result
		int *nextNode = nullptr;		  // potentially to replace numAvailableNodes

		std::vector<std::pair<int, std::string>> bestResults;

		size_t threadsPerNode = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

		std::vector<int> tasks_per_node;

		// statistics
		size_t totalRequests = 0;
		size_t recvdMessages = 0;
		double start_time = 0;
		double end_time = 0;

		/* singleton*/
		MPI_Scheduler() {}
	};

} // namespace GemPBA

#endif