#pragma once
#ifndef MPI_SCHEDULER_HPP
#define MPI_SCHEDULER_HPP

#include "StreamHandler.hpp"
#include "Tree.hpp"
#include <Queue.hpp>

#include <algorithm>
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

#define TERMINATION_TAG 6
#define REFVAL_UPDATE_TAG 9

#define HAS_RESULT_TAG 13
#define NO_RESULT_TAG 14

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

		void setThreadsPerNode(size_t threads_per_process)
		{
			this->threads_per_process = threads_per_process;
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
			return tasks_per_process;
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

				char *message = new char[count];
				MPI_Recv(message, count, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);
				recvdMessages++;

				if (isTerminated(status.MPI_TAG, message))
					break;

				notifyRunningState();

				//if (transmitting)
				//{
				//	fmt::print("rank {} is transmitting, received from {} this should not happen \n", world_rank, status.MPI_SOURCE);
				//	throw;
				//}

				fmt::print("rank {}, received buffer from rank {}\n", world_rank, status.MPI_SOURCE);
				//  push to the thread pool *********************************************************************
				auto *holder = bufferDecoder(message, count); // holder might be useful for non-void functions
				fmt::print("rank {}, pushed buffer to thread pool \n", world_rank, status.MPI_SOURCE);
				// **********************************************************************************************

				taskFunneling(branchHandler);
				notifyStateAvailable();

				//delete holder;
				delete[] message;
			}

			// TODO.. send result

			sendSolution(resultFetcher);
		}

		void taskFunneling(auto &branchHandler)
		{
			std::string *message;
			bool isPop = q.pop(message);

			while (true)
			{
				while (isPop)
				{
					std::scoped_lock lck(mtx);

					std::unique_ptr<std::string> ptr(message);
					nTasks++;

					sendTask(*message);

					isPop = q.pop(message);

					if (!isPop)
						transmitting = false;
					else
					{
						fmt::print("Task found in queue, this should not happen,\t transmitting :{} \n");
						throw;
					}
				}
				checkRefValueUpdate(branchHandler);
				isPop = q.pop(message);

				if (!isPop && branchHandler.isDone())
				{
					/* by the time the thread realises that the thread pool has no more tasks,
                        another buffer there might have been pushed, which should be verified in the next line*/
					isPop = q.pop(message);
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

		bool isChanged(int newVal, int oldVal)
		{
			return newVal != oldVal ? true : false;
		}

		void checkRefValueUpdate(auto &branchHandler)
		{
			// thoughts: no need to care for maximisation or minimisation since the branchHandler does it already
			if (isChanged(refValueGlobal[0], refValueGlobal_old)) // center has put a new value
			{
				refValueGlobal_old = refValueGlobal[0];
				int mostUpToDate;
				// pass old cuz value might change in between
				bool success = branchHandler.updateRefValue(refValueGlobal_old, &mostUpToDate);
				if (!success)
					MPI_Ssend(&mostUpToDate, 1, MPI_INT, 0, REFVAL_UPDATE_TAG, world_Comm);
			}
			else if (isChanged(branchHandler.refValue(),
							   refValueGlobal[0])) // this process has attained a better value
			{
				int temp = branchHandler.refValue();
				MPI_Ssend(&temp, 1, MPI_INT, 0, REFVAL_UPDATE_TAG, world_Comm);
			}
		}

		// enqueue a message which will be sent to the next assigned process
		void push(std::string &&message)
		{
			transmitting = true;
			dest_rank_tmp = next_process[0];
			fmt::print("rank {} entered MPI_Scheduler::tryPush(..) for the node {}\n", world_rank, dest_rank_tmp);
			shift_left(next_process, world_size);

			auto pck = std::make_shared<std::string>(std::forward<std::string &&>(message));
			auto _message = new std::string(*pck);

			if (!q.empty())
			{
				throw std::runtime_error("ERROR: q is not empty !!!!\n");
			}

			q.push(_message);

			releasePriority();
		}

		/*	- return true is priority is acquired, false otherwise
			- priority released automatically if a message is pushed, otherwise it should be released manually
			- only ONE buffer will be enqueued at a time
        	- if the taskFunneling is transmitting the buffer to another node, this method will return false
        	- if previous conditions are met, then actual condition for pushing is evaluated next_process[0] > 0
		*/
		bool acquirePriority()
		{
			std::unique_lock<std::mutex> lck1(mtx, std::defer_lock);
			if (lck1.try_lock() && !transmitting.load() && !acquired.load())
			{
				if (next_process[0] > 0)
				{
					acquired = true;
					return true;
				}
			}
			return false;
		}

		void releasePriority()
		{
			if (acquired)
				acquired = false;
			else
				throw std::runtime_error("Error, double release of priority at releasePriority()\n");
		}

		bool isTerminated(int TAG, char *buffer)
		{
			if (TAG == TERMINATION_TAG)
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

		void sendTask(std::string &message)
		{
			if (dest_rank_tmp > 0)
			{
				if (dest_rank_tmp == world_rank)
				{
					auto msg = "rank " + std::to_string(world_rank) + " attempting to send to itself !!!\n";
					throw std::runtime_error(msg);
				}

				fmt::print("rank {} about to send buffer to rank {}\n", world_rank, dest_rank_tmp);
				MPI_Ssend(message.data(), message.size(), MPI_CHAR, dest_rank_tmp, 0, world_Comm);
				fmt::print("rank {} sent buffer to rank {}\n", world_rank, dest_rank_tmp);
				dest_rank_tmp = -1;
			}
			else
			{
				auto msg = "rank " + std::to_string(world_rank) + ", could not send task to rank " + std::to_string(dest_rank_tmp) + "\n";
				throw std::runtime_error(msg);
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
					put(&child, 1, rank, MPI_INT, offset, win_nextProcess);
					processState[child] = STATE_ASSIGNED;
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
				MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, NO_RESULT_TAG, world_Comm);
			}
			else
			{
				MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, HAS_RESULT_TAG, world_Comm);
				MPI_Send(&refVal, 1, MPI_INT, 0, HAS_RESULT_TAG, world_Comm);
			}
		}

		/* it returns the substraction between end and start*/
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
				int buffer;
				MPI_Status status;
				MPI_Request request;
				int ready;
				double begin = MPI_Wtime();
				MPI_Irecv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &request);

				if (!awaitMessage(buffer, ready, begin, status, request))
					break;

				switch (status.MPI_TAG)
				{
				case STATE_RUNNING: // received if and only if a worker receives from other but center
				{
					processState[status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
					++nRunning;
					fmt::print("rank {} reported running, nRunning :{}\n", status.MPI_SOURCE, nRunning);

					if (processTree[status.MPI_SOURCE].isAssigned())
						processTree[status.MPI_SOURCE].release();

					if (!processTree[status.MPI_SOURCE].hasNext()) // checks if notifying node has a child to push to
					{
						int nxt = getAvailable();
						if (nxt > 0)
						{
							put(&nxt, 1, status.MPI_SOURCE, MPI_INT, 0, win_nextProcess);
							processTree[status.MPI_SOURCE].addNext(nxt);
							processState[nxt] = STATE_ASSIGNED;
							--nAvailable;
						}
					}
					tasks_per_process[status.MPI_SOURCE]++;
					totalRequests++;
				}
				break;
				case STATE_AVAILABLE:
				{
					++rcv_availability;
					processState[status.MPI_SOURCE] = STATE_AVAILABLE;
					++nAvailable;
					--nRunning;

					if (processTree[status.MPI_SOURCE].isAssigned())
						processTree[status.MPI_SOURCE].release();

					fmt::print("rank {} reported available, nRunning :{}\n", status.MPI_SOURCE, nRunning);

					for (int rank = 1; rank < world_size; rank++)
					{
						if (processState[rank] == STATE_RUNNING) // finds the first running node
						{
							if (!processTree[rank].hasNext()) // checks if running node has a child to push to
							{
								put(&status.MPI_SOURCE, 1, rank, MPI_INT, 0,
									win_nextProcess); // assigns returning node to the running node
								processTree[rank].addNext(
									status.MPI_SOURCE);							  // assigns returning node to the running node
								processState[status.MPI_SOURCE] = STATE_ASSIGNED; // it flags returning node as assigned
								--nAvailable;									  // assigned, not available any more
								break;											  // breaks for-loop
							}
						}
					}
				}
				break;
				case REFVAL_UPDATE_TAG:
				{
					/* if center reaches this point, for sure nodes have attained a better reference value
                            or they are not up-to-date, thus it is required to broadcast it whether this value
                            changes or not  */
					bool signal;

					if (maximisation)
						signal = (buffer > refValueGlobal[0]) ? refValueGlobal[0] = buffer : false;
					else
						signal = (buffer < refValueGlobal[0]) ? refValueGlobal[0] = buffer : false;

					bcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);

					if (signal)
					{
						static int success = 0;
						success++;
						fmt::print("refValueGlobal updated to : {} by rank {} \n", refValueGlobal[0],
								   status.MPI_SOURCE);
						fmt::print("SUCCESS updates : {}\n", success);
					}
					else
					{
						static int failures = 0;
						failures++;
						fmt::print("FAILED updates : {} \n", failures);
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

		/* return false if message not received, which is signal of termination
			all workers report (with a message) to center process when about to run a task or when becoming available
			if no message is received within a TIMEOUT window, then all processes will have finished
		*/
		bool awaitMessage(int buffer, int &ready, double begin, MPI_Status &status, MPI_Request &request)
		{
			int cycles = 0;
			while (true)
			{
				MPI_Test(&request, &ready, &status);
				// Check whether the underlying communication had already taken place
				while (!ready && (difftime(begin, MPI_Wtime()) < TIMEOUT_TIME))
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
						return false;
					}
				}
				else
					return true;
			}
		}

		void notifyTermination()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				char buffer[] = "exit signal";
				int count = sizeof(buffer);
				MPI_Send(&buffer, count, MPI_CHAR, rank, TERMINATION_TAG, world_Comm); // send positive signal
			}
			MPI_Barrier(world_Comm);
		}

		int getAvailable()
		{
			for (int rank = 1; rank < world_size; rank++)
			{
				if (processState[rank] == STATE_AVAILABLE)
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
				case HAS_RESULT_TAG:
				{
					std::string buf(buffer, count);

					int refValue;
					MPI_Recv(&refValue, 1, MPI_INT, rank, HAS_RESULT_TAG, world_Comm, &status);

					bestResults[rank].first = refValue; // reference value corresponding to result
					bestResults[rank].second = buf;		// best result so far from this rank

					delete[] buffer;

					fmt::print("solution received from {}, count : {}, refVal {} \n", rank, count, refValue);
				}
				break;

				case NO_RESULT_TAG:
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
			processState[dest] = STATE_RUNNING;
			++tasks_per_process[dest];
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
			MPI_Comm_dup(world_Comm, &availableProcesses_Comm);
		}

		void allocateMPI()
		{
			MPI_Barrier(world_Comm);
			// applicable for all the processes
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, availableProcesses_Comm, &availableProcesses,
							 &win_availableProcesses);
			MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, world_Comm, &refValueGlobal, &win_refValueGlobal);
			MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, availableProcesses_Comm,
							 &next_process,
							 &win_nextProcess);

			init();
			MPI_Barrier(world_Comm);
		}

		void deallocateMPI()
		{
			MPI_Win_free(&win_availableProcesses);
			MPI_Win_free(&win_refValueGlobal);
			MPI_Win_free(&win_nextProcess);
			MPI_Group_free(&world_group);
		}

		void init()
		{
			availableProcesses[0] = 0;
			tasks_per_process.resize(world_size, 0);
			processState.resize(world_size, STATE_AVAILABLE);
			processTree.resize(world_size);

			for (int i = 0; i < world_size; i++)
			{
				next_process[i] = -1; // -1 one means it has no next node assigned
			}

			if (world_rank == 0)
			{
				bestResults.resize(world_size, std::make_pair(-1, std::string()));
			}

			transmitting = false;
			acquired = false;
		}

		int getRank()
		{
			return world_rank;
		}

		void linkRefValue(int **refValueNodes)
		{
			if (refValueGlobal)
				*refValueNodes = this->refValueGlobal;
			else
			{
				fmt::print("Error, refValueGlobal has not been initialized in MPI_Scheduler.hpp\n");
				throw std::runtime_error("Error, refValueGlobal has not been initialized in MPI_Scheduler.hpp\n");
			}
		}

		void setRefValStrategyLookup(bool maximisation)
		{
			this->maximisation = maximisation;
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
		std::vector<int> processState; // state of the nodes : running, assigned or available
		Tree processTree;

		std::mutex mtx;
		std::atomic<bool> transmitting;
		std::atomic<bool> acquired;
		std::condition_variable cv;
		int dest_rank_tmp = -1;

		detail::Queue<std::string *> q;
		bool exit = false;

		MPI_Win win_availableProcesses; // window for the number of available processes
		MPI_Win win_refValueGlobal;		// window to send reference value global
		MPI_Win win_nextProcess;

		MPI_Group world_group;			  // all ranks belong to this group
		MPI_Comm availableProcesses_Comm; // attached to win_availableProcesses
		MPI_Comm world_Comm;			  // world communicator

		int *availableProcesses = nullptr; // Number of available processes	[every process is aware of this number]
		int *refValueGlobal = nullptr;	   // reference value to chose a best solution
		int refValueGlobal_old;
		int *next_process = nullptr; // potentially to replace availableProcesses

		bool maximisation; // true if Maximising, false if Minimising

		std::vector<std::pair<int, std::string>> bestResults;

		size_t threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

		std::vector<int> tasks_per_process;

		// statistics
		size_t totalRequests = 0;
		size_t recvdMessages = 0;
		double start_time = 0;
		double end_time = 0;

		/* singleton*/
		MPI_Scheduler()
		{
		}
	};

} // namespace GemPBA

#endif