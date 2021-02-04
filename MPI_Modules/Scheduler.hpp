#pragma once
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "serialize/stream.hpp"
#include "serialize/oarchive.hpp"
#include "serialize/iarchive.hpp"
#include "Utils.hpp"

#include <random>
#include <stdlib.h> /* srand, rand */
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <string>
#include <sstream>
#include <stdio.h>
#include <time.h>

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

		template <typename Result, typename F, typename Holder, typename Serialize, typename Deserialize>
		void start(int argc, char *argv[], F &&f, Holder &holder, Serialize &&serialize, Deserialize &&deserialize)
		{
			_branchHandler.is_MPI_enable = true;

			//_branchHandler._serialize = serialize;
			initMPI(argc, argv);

			printf("About to start, %d / %d!! \n", world_rank, world_size);
			if (world_rank == 0)
			{
				printf("scheduler() launched!! \n");
				this->schedule(holder, serialize);

				printf("process %d waiting at barrier \n", world_rank);
				MPI_Barrier(world_Comm);
				printf("process %d passed barrier \n", world_rank);
			}
			else
			{
				this->_branchHandler.setMaxThreads(threadsPerNode);
				this->_branchHandler.receiveSeed<Result>(f, serialize, deserialize, holder);

				printf("process %d waiting at barrier \n", world_rank);
				MPI_Barrier(world_Comm);
				printf("process %d passed barrier \n", world_rank);
			}
		}

		auto finalize()
		{
			win_deallocate();
			MPI_Finalize();
			return world_rank;
		}

		auto [[nodiscard("Move semantics need a recipient")]] retrieveResult()
		{
			return std::move(returnStream);
		}

		void printfStats()
		{
			printf("\n \n \n");
			printf("*****************************************************\n");
			printf("Total number of requests : %zu \n", totalRequests);
			printf("Number of approved requests : %zu \n", approvedRequests);
			printf("Number of failed requests : %zu \n", failedRequests);
			printf("*****************************************************\n");
			printf("\n \n \n");
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
			BcastNumAvNodes(); // comunicate to all nodes the total number of available nodes

			printf("*** Busy nodes: %d ***\n ", busyNodes[0]);
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
							BcastNumAvNodes();

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
					break;
				receiveResult();

			} while (true);
		}

		// this sends the termination signal
		auto breakLoop()
		{
			std::this_thread::sleep_for(std::chrono::seconds(1)); // 4 testing
			printf("test, busyNodes = %d\n", busyNodes[0]);
			if (busyNodes[0] == 0)
			{
				for (int dest = 1; dest < world_size; dest++)
				{
					int emptyBuffer;
					int tag = 3;
					MPI_Ssend(&emptyBuffer, 0, MPI::INTEGER, dest, tag, world_Comm);
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
				MPI_Recv(&Bytes, 1, MPI::INTEGER, MPI::ANY_SOURCE, MPI::ANY_TAG, world_Comm, &status);
				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI::CHARACTER, MPI::ANY_SOURCE, MPI::ANY_TAG, world_Comm, &status);

				for (int i = 0; i < Bytes; i++)
					this->returnStream << in_buffer[i];

				finalFlag[0] = false; // this should happen only once, thus breakLoop() will end the execution
				delete[] in_buffer;
			}
		}

		template <typename Holder, typename Serialize>
		void sendSeed(Holder &holder, Serialize &&serialize)
		{
			std::stringstream ss = std::args_handler::unpack_tuple(serialize, holder.getArgs());

			int count = ss.str().size(); // number of Bytes
			char *buffer = new char[count];
			std::memcpy(buffer, ss.str().data(), count);

			int rcvrNode = 1;
			int err = MPI_Ssend(&count, 1, MPI::INTEGER, rcvrNode, 0, world_Comm); // send buffer size
			if (err != MPI::SUCCESS)
				printf("count could not be sent! \n");

			err = MPI_Ssend(buffer, count, MPI::CHARACTER, 1, 0, world_Comm); // send buffer
			if (err == MPI::SUCCESS)
				printf("buffer sucessfully sent! \n");

			availableNodes[rcvrNode] = false; // becomes unavailable until it finishes
			delete[] buffer;
		}

		/* this should be called only when the number of available nodes is modified */
		void BcastNumAvNodes()
		{
			for (int i = 1; i < world_size; i++)
			{
				MPI_Win_lock(MPI::LOCK_EXCLUSIVE, i, 0, win_NumNodes);							  // open epoch
				MPI_Put(numAvailableNodes, 1, MPI::INTEGER, i, 0, 1, MPI::INTEGER, win_NumNodes); // put date through window
				MPI_Win_flush(i, win_NumNodes);													  // complete RMA operation
				MPI_Win_unlock(i, win_NumNodes);												  // close epoch
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
		}

		void initMPI(int argc, char *argv[])
		{
			// Initilialise MPI and ask for thread support
			int provided;
			MPI_Init_thread(&argc, &argv, MPI::THREAD_SERIALIZED, &provided);
			if (provided < MPI::THREAD_SERIALIZED)
			{
				printf("The threading support level is lesser than that demanded.\n");
				MPI_Abort(MPI::COMM_WORLD, EXIT_FAILURE);
			}
			else
			{
				printf("The threading support level corresponds to that demanded.\n");
			}

			communicators();

			int namelen;
			MPI_Get_processor_name(processor_name, &namelen);

			printf("Process %d of %d is on %s\n", world_rank, world_size, processor_name);
			MPI_Barrier(world_Comm);
			printf("About to create window, %d / %d!! \n", world_rank, world_size);
			MPI_Barrier(world_Comm);
			win_allocate();
			MPI_Barrier(world_Comm);

			this->_branchHandler.linkMPIargs(world_rank,
											 world_size,
											 processor_name,
											 numAvailableNodes,
											 &win_accumulator,
											 &win_finalFlag,
											 &win_AvNodes,
											 &win_boolean,
											 &win_NumNodes,
											 &world_Comm,
											 &second_Comm,
											 &SendToNodes_Comm,
											 &SendToCenter_Comm,
											 &NodeToNode_Comm);
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
			printf("About to allocate\n");

			if (world_rank == 0)
			{
				MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
				MPI_Win_allocate(world_size * sizeof(bool), sizeof(bool), MPI::INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
				MPI_Win_allocate(world_size * sizeof(bool), sizeof(bool), MPI::INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);

				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(sizeof(bool), sizeof(bool), MPI::INFO_NULL, second_Comm, &finalFlag, &win_finalFlag);
			}
			else
			{
				MPI_Win_allocate(sizeof(int), sizeof(int), MPI::INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
				MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
				MPI_Win_allocate(0, sizeof(int), MPI::INFO_NULL, accumulator_Comm, &busyNodes, &win_accumulator);

				if (MPI_COMM_NULL != second_Comm)
					MPI_Win_allocate(0, sizeof(bool), MPI::INFO_NULL, second_Comm, &finalFlag, &win_finalFlag);
			}

			printf("Allocated\n");

			init();
		}

		void win_deallocate()
		{

			MPI_Win_free(&win_accumulator);
			MPI_Win_free(&win_AvNodes);
			MPI_Win_free(&win_boolean);
			MPI_Win_free(&win_NumNodes);
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
				numAvailableNodes[0] = world_size - 1;
				busyNodes[0] = 0;
				finalFlag[0] = false;
				for (int i = 0; i < world_size; i++)
				{
					inbox_boolean[i] = false;
					availableNodes[i] = false; // no node is available, each node is in charge of communicating its availability
				}
			}
			else
				numAvailableNodes[0] = world_size - 1;
		}

	private:
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		char processor_name[128]; // name of the node
		MPI_Win win_boolean;	  // window for pushing request from nodes
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

		bool *inbox_boolean;	// receives signal of a node attempting to put data [only center node has the list]
		int *numAvailableNodes; // Number of available nodes	[every node is aware of this number]
		bool *availableNodes;	// list of available nodes [only center node has the list]
		int *busyNodes;			// number of nodes working at the time
		bool *finalFlag;

		std::stringstream returnStream;

		size_t threadsPerNode = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

		// statistics
		size_t totalRequests = 0;
		size_t approvedRequests = 0;
		size_t failedRequests = 0;

		/* singleton*/
		Scheduler(BranchHandler &branchHandler) : _branchHandler(branchHandler) {}
	};
} // namespace library
#endif