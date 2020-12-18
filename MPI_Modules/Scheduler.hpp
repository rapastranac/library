#pragma once
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

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

	public:
		Scheduler(/* args */) {}

		template <typename Handler, typename F, typename... Args>
		void start(int argc, char **argv, Handler &handler, F &&f, Args &&... args)
		{
			this->PACKAGE_SIZE = 1;

			char *win_buffer;

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

			MPI_Comm_size(MPI_COMM_WORLD, &this->world_size);
			MPI_Comm_rank(MPI_COMM_WORLD, &this->world_rank);

			MPI_Comm_dup(MPI_COMM_WORLD, &SendToNodes_Comm);
			MPI_Comm_dup(MPI_COMM_WORLD, &SendToCenter_Comm);
			MPI_Comm_dup(MPI_COMM_WORLD, &NodeToNode_Comm);
			MPI_Comm_dup(MPI_COMM_WORLD, &BCast_Comm);
			MPI_Comm_dup(MPI_COMM_WORLD, &Accumulator_Comm);

			int namelen;
			this->processor_name = new char[128];
			MPI_Get_processor_name(processor_name, &namelen);

			fprintf(stdout, "Process %d of %d is on %s\n", world_rank, world_size, processor_name);
			fflush(stdout);

			printf("About to create window, %d / %d!! \n", world_rank, world_size);
			MPI_Barrier(MPI_COMM_WORLD);
			win_allocate();
			//goto dummy;
			MPI_Barrier(MPI_COMM_WORLD);

			handler.pass_MPI_args(world_rank,
								  world_size,
								  processor_name,
								  &win_boolean,
								  &win_data,
								  &win_NumNodes,
								  &win_AvNodes,
								  &inbox_boolean[world_rank],
								  &inbox_data[world_rank],
								  &SendToNodes_Comm,
								  &SendToCenter_Comm,
								  &NodeToNode_Comm,
								  numAvailableNodes,
								  &win_test,
								  &win_accumulator);

			printf("About to start, %d / %d!! \n", world_rank, world_size);
			if (world_rank == 0)
			{
				printf("scheduler() launched!! \n");
				this->scheduler(args...);
			}
			else
			{
				handler.seedReceiver();
			}

			printf("process %d waiting at barrier \n", world_rank);
			MPI_Barrier(MPI_COMM_WORLD);

			win_deallocate();

			MPI_Finalize();
		}

	private:
		/* all processes that belong to the same window group will be synchronised, such that
			at MPI_Win_create(...), the same processes will wait until all of them pass by
			their corresponding MPI_Win_create(...) */
		template <typename... Args>
		void scheduler(Args &&... args)
		{
			sendSeed(args...);
			updateNumAvNodes();
			BcastNumAvNodes(); // comunicate to all nodes the total available nodes

			//	while (busyNodes[0] == 0)
			//	{
			//		//It guarantees the upcoming infinite loop does not break before starting
			//	};

			std::this_thread::sleep_for(std::chrono::seconds(2)); // 4 testing

			printf("*** Busy nodes: %d ***\n ", busyNodes[0]);
			printf("Scheduler started!! \n");

			while (true) // inifite loop to receive information ||-- FIND A WAY TO TERMINATE THIS LOOP --||
			{
				for (int i = 1; i < world_size; i++)
				{
					MPI_Status status;
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

						//........
					}
				}
				std::this_thread::sleep_for(std::chrono::seconds(1)); // 4 testing
				if (breakLoop())
				{
					break;
				}
			}
		}

		bool breakLoop()
		{
			printf("test, busyNodes = %d\n", busyNodes[0]);
			int count = 0;
			for (int i = 1; i < world_size; i++)
			{
				count += availableNodes[i];
			}

			//if (count == (world_size - 1) && !onceFlag)
			if (busyNodes[0] == 0 && !onceFlag)
			{
				for (int target = 1; target < world_size; target++)
				{
					int buffer;
					MPI_Ssend(&buffer, 0, MPI_INT, target, 3, MPI_COMM_WORLD);
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

			//Serialize obj;
			//oarchive raw = obj.serialize(args...);
			//auto tuple = handler->getSeed();
			//TODO serialize tuple in here
			int localSeed = 0; //This is only initial depth for now
			int rcvrNode = 1;
			//int err = MPI_Send(&localSeed, 1, MPI_INT, rcvrNode, 0, SendToNodes_Comm);
			//int err = MPI_Ssend(&localSeed, 1, MPI_INT, 1, 0, SendToNodes_Comm);
			int err = MPI_Ssend(&localSeed, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
			if (err == MPI_SUCCESS)
			{
				printf("Seed planted sucessfully! \n");
			}
			availableNodes[rcvrNode] = 0;

			//busyNodes = 1;
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
					MPI_Win_allocate(PACKAGE_SIZE * world_size * sizeof(int), PACKAGE_SIZE * sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &inbox_data, &win_data);
					MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &availableNodes, &win_AvNodes);
					MPI_Win_allocate(world_size * sizeof(int), sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &testBuffer, &win_test);
					MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, Accumulator_Comm, &busyNodes, &win_accumulator);
				}
				else
				{
					MPI_Win_allocate(sizeof(int), sizeof(int), MPI_INFO_NULL, BCast_Comm, &numAvailableNodes, &win_NumNodes);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &inbox_boolean, &win_boolean);
					MPI_Win_allocate(0, PACKAGE_SIZE * sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &inbox_data, &win_data);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &availableNodes, &win_AvNodes);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, SendToCenter_Comm, &testBuffer, &win_test);
					MPI_Win_allocate(0, sizeof(int), MPI_INFO_NULL, Accumulator_Comm, &busyNodes, &win_accumulator);
				}

				printf("Heyyyyyy\n");

				init();
			}
			else
			{
				this->numAvailableNodes = new int[1];
				this->inbox_boolean = new int[world_size];
				this->inbox_data = new int[PACKAGE_SIZE * world_size];
				this->availableNodes = new int[world_size];
				this->busyNodes = new int[1];
				init();
			}
		}

		void win_deallocate()
		{
			if (world_size > 1)
			{
				MPI_Win_free(&win_data);
				MPI_Win_free(&win_boolean);
				MPI_Win_free(&win_NumNodes);
				MPI_Win_free(&win_AvNodes);
				MPI_Win_free(&win_accumulator);
			}
			else
			{
				delete[] numAvailableNodes;
				delete[] inbox_boolean;
				delete[] inbox_data;
				delete[] availableNodes;
				delete[] busyNodes;
			}

			MPI_Comm_free(&SendToNodes_Comm);
			MPI_Comm_free(&SendToCenter_Comm);
			MPI_Comm_free(&NodeToNode_Comm);
			MPI_Comm_free(&BCast_Comm);
			MPI_Comm_free(&Accumulator_Comm);
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
		MPI_Win win_data;	  // window for incoming data from nodes
		MPI_Win win_NumNodes; // window for the number of available nodes
		MPI_Win win_AvNodes;  // window for the list of available nodes
		MPI_Win win_test;	  // testing only
		MPI_Win win_accumulator;

		MPI_Comm SendToNodes_Comm;	// exclusive communicator attached to the aformentionned groups
		MPI_Comm SendToCenter_Comm; // exclusive communicator attached to the aformentionned groups
		MPI_Comm NodeToNode_Comm;
		MPI_Comm BCast_Comm;	   // attached to number of nodes
		MPI_Comm Accumulator_Comm; // attached to win_accumulator

		int *inbox_boolean;		// receives signal of a node attempting to put data [only center node has the list]
		int *inbox_data;		// if available, data is to be pushed in here [only center node has the list]
		int *numAvailableNodes; // Number of available nodes	[every node is aware of this number]
		int *availableNodes;	// list of available nodes [only center node has the list]
		int *busyNodes;

		int *testBuffer;

		int PACKAGE_SIZE; //size of data that will be sent over the network
	};
} // namespace library
#endif