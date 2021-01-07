#pragma once
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "serialize/stream.hpp"
#include "serialize/oarchive.hpp"
#include "serialize/iarchive.hpp"
#include "Utils.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <sstream>

//#include <bits/stdc++.h>

#include <stdlib.h> /* srand, rand */
#include <time.h>

#include <mpi.h>
#include <stdio.h>

#include <stdlib.h> /* srand, rand */
#include <time.h>	/* time */

#define TEST 0

using namespace std;

namespace library
{

	class Scheduler
	{
		friend class BranchHandler;

	private:
		BranchHandler &handler;

	public:
		Scheduler(BranchHandler &bHandler) : handler(bHandler) {}
		Scheduler(BranchHandler &bHandler, size_t threadsPerNode) : handler(bHandler)
		{
			this->threadsPerNode = threadsPerNode;
		}

		template <typename F, typename... Args>
		void start(int argc, char **argv, F &&f, Args &&... args)
		{

			// Initilialise MPI and ask for thread support
			int provided;
			MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided);
			if (provided < MPI_THREAD_SERIALIZED)
			{
				printf("The threading support level is lesser than that demanded.\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}
			else
			{
				printf("The threading support level corresponds to that demanded.\n");
			}

			MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library

			MPI_Comm_size(world_Comm, &this->world_size);
			MPI_Comm_rank(world_Comm, &this->world_rank);

			// a communicator to syncronise only process 0 and 1 ****************************
			MPI_Comm_group(world_Comm, &world_group);						 // world group, all ranks
			const int prime_group_ranks[2] = {0, 1};						 // construct a group ranks in prime_group
			MPI_Group_incl(world_group, 2, prime_group_ranks, &prime_group); // include ranks in group
			MPI_Comm_create_group(world_Comm, prime_group, 0, &prime_Commm); // creates the group
																			 // ******************************************************************************
			MPI_Comm_dup(world_Comm, &SendToNodes_Comm);
			MPI_Comm_dup(world_Comm, &SendToCenter_Comm);
			MPI_Comm_dup(world_Comm, &NodeToNode_Comm);
			MPI_Comm_dup(world_Comm, &BCast_Comm);
			MPI_Comm_dup(world_Comm, &Accumulator_Comm);

			int namelen;
			this->processor_name = new char[128];
			MPI_Get_processor_name(processor_name, &namelen);

			fprintf(stdout, "Process %d of %d is on %s\n", world_rank, world_size, processor_name);
			fflush(stdout);

			printf("About to create window, %d / %d!! \n", world_rank, world_size);
			MPI_Barrier(world_Comm);
			win_allocate();
			//goto dummy;
			MPI_Barrier(world_Comm);

			handler.linkMPIargs(world_rank,
								world_size,
								processor_name,
								&win_boolean,
								&win_NumNodes,
								&win_AvNodes,
								&world_Comm,
								&SendToNodes_Comm,
								&SendToCenter_Comm,
								&NodeToNode_Comm,
								&prime_Commm,
								numAvailableNodes,
								&win_accumulator);

			printf("About to start, %d / %d!! \n", world_rank, world_size);
			if (world_rank == 0)
			{
				printf("scheduler() launched!! \n");
				this->schedule(args...);
			}
			else
			{
				this->handler.setMaxThreads(threadsPerNode);
				this->handler.receiveSeed(f, args...);
			}

			printf("process %d waiting at barrier \n", world_rank);
			MPI_Barrier(world_Comm);
			printf("process %d passed barrier \n", world_rank);

			win_deallocate();

			MPI_Finalize();
		}

	private:
		/* all processes that belong to the same window group will be synchronised, such that
			at MPI_Win_create(...), the same processes will wait until all of them pass by
			their corresponding MPI_Win_create(...) */
		template <typename... Args>
		void schedule(Args &&... args)
		{
			sendSeed(args...);
			MPI_Barrier(prime_Commm); // syncrhonises only process 0 and 1 - this guarantees ...
									  // ... that process 0 does not terminate the loop before process 1...
									  // ... receives the seed, then busyNodes will be != 0 for the first ...
									  // ... loop
			updateNumAvNodes();
			BcastNumAvNodes(); // comunicate to all nodes the total number of available nodes

			printf("*** Busy nodes: %d ***\n ", busyNodes[0]);
			printf("Scheduler started!! \n");

			while (true) // inifite loop to receive information ||-- FIND A WAY TO TERMINATE THIS LOOP --||
			{
				for (int i = 1; i < world_size; i++)
				{
					updateNumAvNodes();		   // check list of available nodes
					if (inbox_boolean[i] == 1) //if this cell changes, then a node has requested permission to push
					{
						if (numAvailableNodes[0] > 0) //if found, positive signal is sent back to requesting node
						{
							--numAvailableNodes[0];
							BcastNumAvNodes();

							int k = findAvailableNode();						  // first node available in the list
							inbox_boolean[i] = false;							  // reset boolean to zero lest center node check it again
							int flag = true;									  // signal to be sent back to node 'i'
							MPI_Ssend(&flag, 1, MPI_INT, i, k, SendToNodes_Comm); // returns signal to 'i' that data can be received
																				  //MPI_Recv(&inbox_data[i], 1, MPI_INT, i, MPI_ANY_TAG, SendToCenter_Comm, &status); // receives data
																				  //MPI_Ssend(&inbox_data[i], 1, MPI_INT, k, 0, SendToNodes_Comm);					  // send data to node 'k'
																				  //printf("process %d forwarded to process %d \n", i, k);
						}
						else
						{
							/* No available nodes, return false signal so the handler could forward
							the information sequentially */
							int flag = false;
							inbox_boolean[i] = false;
							//printf("Hello from line 214 \n");
							MPI_Ssend(&flag, 1, MPI_INT, i, 0, SendToNodes_Comm); //returns signal that data cannot be received
						}
					}
				}

				if (breakLoop())
				{
					break;
				}
			}
		}

		bool breakLoop()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 4 testing
			printf("test, busyNodes = %d\n", busyNodes[0]);
			if (busyNodes[0] == 0)
			{
				for (int dest = 1; dest < world_size; dest++)
				{
					int buffer;
					int tag = 3;
					MPI_Ssend(&buffer, 0, MPI_INT, dest, tag, world_Comm);
				}
				printf("BusyNodes = 0 achieved \n");
				onceFlag = true;
				return true;
			}
			return false;
		}

		bool onceFlag = false;

		template <typename... Args>
		void sendSeed(Args &&... args)
		{
			serializer::stream os;
			serializer::oarchive oa(os);
			Utils::buildBuffer(oa, args...);
			/*
			size_t Bytes = os.size();
			serializer::stream is(os);
			is.allocate(Bytes);
			serializer::iarchive ia(is);
			Utils::readBuffer(ia, args...);*/

			//TODO serialize tuple in here
			int count = os.size();
			int rcvrNode = 1;
			//int err = MPI_Send(&localSeed, 1, MPI_INT, rcvrNode, 0, SendToNodes_Comm);
			//int err = MPI_Ssend(&localSeed, 1, MPI_INT, 1, 0, SendToNodes_Comm);
			int err = MPI_Ssend(&count, 1, MPI_INT, rcvrNode, 0, world_Comm);
			if (err != MPI_SUCCESS)
			{
				printf("count could not be sent! \n");
			}

			err = MPI_Ssend(&os[0], count, MPI_CHAR, 1, 0, world_Comm);
			if (err == MPI_SUCCESS)
			{
				printf("Seed planted sucessfully! \n");
			}
			availableNodes[rcvrNode] = 0;
		}

		/* this should be called only when the number of available nodes is modified */
		void BcastNumAvNodes()
		{
			//local number at [0] is broadcasted to other nodes
			for (int i = 1; i < world_size; i++)
			{
				MPI_Win_lock(MPI_LOCK_SHARED, i, 0, win_NumNodes);
				MPI_Put(numAvailableNodes, 1, MPI_INT, i, 0, 1, MPI_INT, win_NumNodes);
				MPI_Win_flush(i, win_NumNodes);
				MPI_Win_unlock(i, win_NumNodes);
			}
		}

		/*
		this is supposed to be invoked only when there are available nodes
		returns -1 if no available node
	*/
		int findAvailableNode()
		{
			std::vector<int> nodes; // testing
			int availableNodeID = -1;
			for (int j = 1; j < world_size; j++)
			{
				if (availableNodes[j] == 1)
				{
					nodes.push_back(j); // testing
										/* availableNodes[j] = false; //if a node is picked, then this is no longer available
				availableNodeID = j;
				break; */
				}
			}
			srand(time(NULL));						 // testing
			int val = rand() % nodes.size();		 // testing
			availableNodeID = nodes[val];			 // testing
			availableNodes[availableNodeID] = false; // testing

			return availableNodeID;
		}

		/* if other nodes become idle, they will put "true" into their cell at availableNodes*/
		void updateNumAvNodes()
		{
			int count = 0;
			for (int i = 1; i < world_size; i++)
			{
				count = availableNodes[i];
			}
			numAvailableNodes[0] = count;
		}

		void win_allocate()
		{
			printf("About to allocate\n");
			if (world_size > 1)
			{
				if (world_rank == 0)
				{
					MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
					MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
					MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
					MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, Accumulator_Comm, &busyNodes, &win_accumulator);
				}
				else
				{
					MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, world_Comm, &availableNodes, &win_AvNodes);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, Accumulator_Comm, &busyNodes, &win_accumulator);
				}

				printf("Heyyyyyy\n");

				init();
			}
			else
			{
				this->numAvailableNodes = new int[1];
				this->inbox_boolean = new int[world_size];
				this->availableNodes = new int[world_size];
				this->busyNodes = new int[1];
				init();
			}
		}

		void win_deallocate()
		{
			if (world_size > 1)
			{
				MPI_Win_free(&win_boolean);
				MPI_Win_free(&win_NumNodes);
				MPI_Win_free(&win_AvNodes);
				MPI_Win_free(&win_accumulator);
			}
			else
			{
				delete[] numAvailableNodes;
				delete[] inbox_boolean;
				delete[] availableNodes;
				delete[] busyNodes;
			}

			MPI_Comm_free(&SendToNodes_Comm);
			MPI_Comm_free(&SendToCenter_Comm);
			MPI_Comm_free(&NodeToNode_Comm);
			MPI_Comm_free(&BCast_Comm);
			MPI_Comm_free(&Accumulator_Comm);

			if (world_rank == 0 || world_rank == 1)
			{
				MPI_Group_free(&world_group);
				MPI_Group_free(&prime_group);
				MPI_Comm_free(&prime_Commm);
			}
			MPI_Comm_free(&world_Comm);
		}

		void init()
		{
			//isNodeAvailable.resize(world_size, true);
			//isNodeAvailable[0] = false;
			if (world_rank == 0)
			{
				numAvailableNodes[0] = world_size - 1;
				busyNodes[0] = 0;

				for (int i = 0; i < world_size; i++)
				{
					inbox_boolean[i] = 0;
					availableNodes[i] = 0; // no node is available, each node is in charge of communicating its availability
				}
			}
			else
			{
				numAvailableNodes[0] = world_size - 1;
			}
		}

	private:
		int world_rank;		  // get the rank of the process
		int world_size;		  // get the number of processes
		char *processor_name; // name of the node

		std::unique_ptr<std::thread> sch_t;

		MPI_Win win_boolean;  // window for push request from nodes
		MPI_Win win_NumNodes; // window for the number of available nodes
		MPI_Win win_AvNodes;  // window for the list of available nodes
		MPI_Win win_accumulator;

		MPI_Group world_group;
		MPI_Group prime_group;
		MPI_Comm prime_Commm;

		MPI_Comm SendToNodes_Comm;	// exclusive communicator attached to the aformentionned groups
		MPI_Comm SendToCenter_Comm; // exclusive communicator attached to the aformentionned groups
		MPI_Comm NodeToNode_Comm;
		MPI_Comm BCast_Comm;	   // attached to number of nodes
		MPI_Comm Accumulator_Comm; // attached to win_accumulator
		MPI_Comm world_Comm;

		int *inbox_boolean;		// receives signal of a node attempting to put data [only center node has the list]
		int *numAvailableNodes; // Number of available nodes	[every node is aware of this number]
		int *availableNodes;	// list of available nodes [only center node has the list]
		int *busyNodes;

		int *testBuffer;

		size_t threadsPerNode = std::thread::hardware_concurrency();
	};
} // namespace library
#endif