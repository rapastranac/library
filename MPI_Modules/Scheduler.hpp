#pragma once
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP


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

#include <chrono>

#include <mutex>


#include <deque>


#define TAG_TASK 1
#define TAG_NOWORK 2
#define TAG_TERMINATE 3
#define TAG_AVAIL 4
#define TAG_OPTSOL 5
#define TAG_STARTEDWORKING 6
#define TAG_ACKAVAIL 7
#define TAG_WORKSENT 8

#define STATE_WORKING 1
#define STATE_ASSIGNED 2
#define STATE_AVAIL 3



#define VERBOSITY_NONE -1
#define VERBOSITY_BASIC 0
#define VERBOSITY_ALL 1

#define VERBOSITY 0


namespace library
{

	/*
	IMPORTANT STUFF THAT I REMOVED: 
	_branchHandler.setMaxThreads(threadsPerNode);
	if (std::is_void<_ret>::value)
		_branchHandler.functionIsVoid();
	
	*/



	class BranchHandler;
	

	class Scheduler
	{

	private:
		BranchHandler &_branchHandler;
		
		
		
		
		
		
		
		
		//int argc;
		//char **argv;
		int world_rank;			  // get the rank of the process
		int world_size;			  // get the number of processes/nodes
		
		char processor_name[128]; // name of the node

		MPI_Win win_centerBestVal; // window to send reference value global
		int *winbuf_centerBestVal = nullptr;	  // reference value to chose a best result

		MPI_Win win_waitingNodes;
		int *winbuf_waitingNodes = nullptr;

		vector<int> assignments;


		std::vector<int> tasks_per_node;


		std::mutex mtx; //local mutex
		
		
		bool verbose = VERBOSITY;

		
	public:
	
		/* singleton*/
		Scheduler(BranchHandler &branchHandler) : _branchHandler(branchHandler) 
		{
		
		}
	



		int getCenterBestVal()
		{
			if (!winbuf_centerBestVal)
				return 9999999;	//TODO : NOT GREAT
			int tmpbest;
			
			//Note : whenever we want to access a window, we NEED to lock, as per mpi standards
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, win_centerBestVal);
			tmpbest = winbuf_centerBestVal[0];
			MPI_Win_unlock(world_rank, win_centerBestVal);
			
			return tmpbest;
		}
		
		
		void setCenterBestVal(int newval)
		{
			
			if (newval >= getCenterBestVal())
				return;
			
			if (verbose > 0)
				cout<<cnow()<<"WR="<<world_rank<<" sending best val "<<newval<<" to center"<<endl;
			
			MPI_Send(&newval, 1, MPI_INT, 0, TAG_OPTSOL, MPI_COMM_WORLD);
		
		}
		
		



		void initMPI()
		{
		
			// Initilialise MPI and ask for thread support
			int provided;
			MPI_Init_thread(NULL, NULL, MPI_THREAD_SERIALIZED, &provided);
			if (provided < MPI_THREAD_SERIALIZED)
			{
				cout<<cnow()<<"The threading support level is lesser than that demanded, got "<<provided<<endl;
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}
			
			
			
			MPI_Comm_size(MPI_COMM_WORLD, &world_size);
			MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
			
			cout<<cnow()<<"WR="<<world_rank<<" WS="<<world_size<<endl;
			
			if (world_size < 2)
			{
				fmt::print("At least two processes required !!\n");
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}
			
			
			
			win_allocate();
			
			winbuf_centerBestVal[0] = 999999;	//TODO ML : NOT CLEAN
			
			
			for (int i = 0; i < world_size; ++i)
			{
				assignments.push_back(-1);
			}

		}
		
		
		
		
		
		
		

		void finalize()
		{
			win_deallocate();
			MPI_Finalize();
		}


		int getWorldRank()
		{
			return world_rank;
		}



		int getWorldSize()
		{
			return world_size;
		}

		std::vector<int> executedTasksPerNode()
		{
			return tasks_per_node;
		}
		
		
		
		
		//in a multithreaded setting, you'd better have a lock if you are calling this
		int getNextWaitingRank()
		{
			int retval = -1;

			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, win_waitingNodes);
			for (int i = 1; i < world_size; ++i)
			{
				if (winbuf_waitingNodes[i] == 1)
				{
					retval = i;
					break;
				}
			}

			MPI_Win_unlock(world_rank, win_waitingNodes);
			
			return retval;
		
		}
		
		
		
		
		
		//returns true if we managed to send the holder to another process, false otherwise
		template <typename Holder, typename Serializer>
		bool sendHolderToNode(Holder &holder, Serializer &&serializer)
		{

			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, win_waitingNodes);
			

			for (int i = 1; i < world_size; i++)
			{
				
				if (winbuf_waitingNodes[i] == 1)
				{
					winbuf_waitingNodes[i] = 0;
					MPI_Win_unlock(world_rank, win_waitingNodes);
					
					std::stringstream stream;
					auto fct = std::bind_front(serializer, std::ref(stream));
					std::apply(fct, holder.getArgs());
					int Bytes = stream.str().size(); // number of Bytes
					
					if (verbose > 0)
						cout<<"WR="<<world_rank<<" sending to "<<i<<endl;
					int err = MPI_Send(stream.str().data(), Bytes, MPI_CHAR, i, TAG_TASK, MPI_COMM_WORLD); // send buffer
					if (err != MPI_SUCCESS)
						cout<<"ERROR !  SENDING TASK FAILED!"<<endl;
					
					
					MPI_Send(&i, 1, MPI_INT, 0, TAG_WORKSENT, MPI_COMM_WORLD);
					
					if (verbose > 0)
						cout<<"WR="<<world_rank<<" DONE sending to "<<i<<endl;				
					
					return true;
					
				}
			}
			
			MPI_Win_unlock(world_rank, win_waitingNodes);
			
			/*if (verbose > 0)
			{
				cout<<cnow()<<"WR="<<world_rank<<" holder sent"<<endl;
			}*/
			
			
			return false;			
			
		}
		
		
		
		//TODO : not clean
		string cnow()
		{
			/*auto timenow =
			      chrono::system_clock::to_time_t(chrono::system_clock::now());
			    	cout << ctime(&timenow)<<" ";*/
			return "";
		}
		
		
		void printDebugInfo()
		{
			
			if (!winbuf_waitingNodes)
				return;
			cout<<"WR="<<world_rank<<" waitnodes=";
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, win_waitingNodes);
			for (int i = 0; i < world_size; ++i)
			{
				cout<<winbuf_waitingNodes[i]<<" ";
			}
			MPI_Win_unlock(world_rank, win_waitingNodes);
			cout<<endl;
		}
		
		
		
		
		void runCenter()		//ML : previously known as schedule
		{
			MPI_Barrier(MPI_COMM_WORLD);
					
					
			double t1, t2; 
			t1 = MPI_Wtime(); 


					
						
			int rcv_availability = 0;
			tasks_per_node.resize(world_size, 0);
			
			std::vector<int> nodeStates(world_size, STATE_WORKING);	//center thinks everyone is working, until they tell him that no
			
			
			int centerBestval = 9999999;	//TODO : not clean

			

			while (true)
			{
			

				if (verbose > 0)
				{
					cout<<cnow()<<"CENTER now receiving, nodeStates = ";
					for (int i : nodeStates)
						cout<<i<<" ";
					cout<<endl;
					
					cout<<cnow()<<"CENTER : handling workers, assignments = ";
					for (int i : assignments)
						cout<<i<<" ";
					cout<<endl;
					
				}
			
				MPI_Status status;
				int number_amount;

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				MPI_Get_count(&status, MPI_INT, &number_amount);

				int* number_buf = (int*)malloc(sizeof(int) * number_amount);

				MPI_Recv(number_buf, number_amount, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);


				if (status.MPI_TAG == TAG_AVAIL)
				{
					nodeStates[status.MPI_SOURCE] = STATE_AVAIL;
					assignments[status.MPI_SOURCE] = -1;
					
					if (verbose > 0)
					{
						
						cout<<cnow()<<"CENTER : received TAG_AVAIL from "<<status.MPI_SOURCE<<", nodeStates after = ";
						for (int i : nodeStates)
							cout<<i<<" ";
						cout<<endl;
					}
					
				}
				else if (status.MPI_TAG == TAG_OPTSOL)
				{
					if (verbose > 0)
						cout<<cnow()<<"CENTER : got TAG_OPTSOL, value "<<number_buf[0]<<" from "<<status.MPI_SOURCE<<endl;
					
					if (number_buf[0] < centerBestval)		//TODO ML : this isn't great
					{
						centerBestval = number_buf[0];
						if (verbose > 0)
						{
							cout<<cnow()<<"CENTER : centerBestval="<<centerBestval<<endl;
						}
					
						//ML : is a broadcast that does a passive put possible?
						for (int i = 1; i < world_size; ++i)
						{
							MPI_Win_lock(MPI_LOCK_EXCLUSIVE, i, 0, win_centerBestVal);
							MPI_Put(&centerBestval, 1, MPI_INT, i, 0, 1, MPI_INT, win_centerBestVal);
							MPI_Win_unlock(i, win_centerBestVal);
					    	}
					}
				}
				else if (status.MPI_TAG == TAG_WORKSENT)
				{
					int workdest = number_buf[0];
					if (verbose > 0)
						cout<<cnow()<<"CENTER : received TAG_WORKSENT from "<<status.MPI_SOURCE<<" who sent work to "<<workdest<<endl;
					
					nodeStates[workdest] = STATE_WORKING;
				}
				else if (status.MPI_TAG == TAG_STARTEDWORKING)
				{
					if (verbose > 0)
						cout<<cnow()<<"CENTER : received TAG_STARTEDWORKING from "<<status.MPI_SOURCE<<endl;
					nodeStates[status.MPI_SOURCE] = STATE_WORKING;
					assignments[status.MPI_SOURCE] = -1;
				}
				else
				{
					cout<<cnow()<<"CENTER : received tag "<<status.MPI_TAG<<" from "<<status.MPI_SOURCE<<" but center doesn't know what to do."<<endl;
				
				}
				
				
				free(number_buf);


				/*
				The last part of the center loop assigns available nodes to working nodes.
				The point is that workers receive some waiting nodes in their winbuf_waitingNodes. 
				When workers have time, they check their window and see that some node needs work, in which case they will send them work.
				
				The job of center here is to assign each available node to a unique worker.  That way, no race condition or whatever.
				
				We also check if no one's working.  If so we terminate.
				*/
				
				
				if (verbose > 0)
				{
					cout<<cnow()<<"CENTER : handling workers, nodeStates = ";
					for (int i : nodeStates)
						cout<<i<<" ";
					cout<<endl;
					
					cout<<cnow()<<"CENTER : handling workers, assignments = ";
					for (int i : assignments)
						cout<<i<<" ";
					cout<<endl;
					
					
				}
				vector<int> avail;
				int nbAssigned = 0;
				vector<int> working;
				for (int i = 1; i < world_size; ++i)
				{
					if (nodeStates[i] == STATE_WORKING)
					{
						working.push_back(i);
					}
					else if (nodeStates[i] == STATE_AVAIL)
					{
						avail.push_back(i);
					}
					else if (nodeStates[i] == STATE_ASSIGNED)
					{
						nbAssigned++;
					}
				}
				
				
				
				
				
				
				
				if (!working.empty())
				{
				
					int cptworking = 0;
					for (int i : avail)	//assign all avail
					{
						if (verbose > 0)
							cout<<cnow()<<"CENTER : assigning "<<i<<" to "<<working[cptworking]<<endl;
					
						int one = 1;
						//cout<<"Putting one to "<< working[cptworking] << " at index "<<i<<endl;
						MPI_Win_lock(MPI_LOCK_EXCLUSIVE, working[cptworking], 0, win_waitingNodes);
						//cout<<"got lock"<<endl;
						MPI_Put(&one, 1, MPI_INT, working[cptworking], i, 1, MPI_INT, win_waitingNodes); 
						//cout<<"sent"<<endl;
						MPI_Win_unlock(working[cptworking], win_waitingNodes);
						//cout<<"unlocked"<<endl;
						
						assignments[i] = working[cptworking];
						
						cptworking = (cptworking + 1) % working.size();
						nodeStates[i] = STATE_ASSIGNED;
					}
				
					if (verbose > 0)
						cout<<"CENTER : done assigning"<<endl;
					
				}
				else if (true || nbAssigned == 0)	//we're done!
				{
					if (verbose >= 0)
					{
						cout<<cnow()<<"CENTER : no one is working, will terminate"<<endl;
						cout<<cnow()<<"CENTER : BestVal received = "<<centerBestval<<endl;
					}
					
					
					
					for (int i = 1; i < world_size; ++i)
					{
						if (verbose > 0)
							cout<<cnow()<<"CENTER : telling "<<i<<" to terminate."<<endl;
						MPI_Send(&world_rank, 1, MPI_INT, i, TAG_TERMINATE, MPI_COMM_WORLD);
						
						if (verbose > 0)
							cout<<cnow()<<"CENTER : done telling "<<i<<endl;
					}
					
					t2 = MPI_Wtime(); 
					printf( "CENTER : Elapsed time is %f\n", t2 - t1 ); 					
					
					break;
				}
				
			}

		}
		
		
		
		
		
		
		template <typename Function, typename Serializer, typename Deserializer, typename Holder>
		void runNode(Function &function, Serializer &serializer, Deserializer &deserializer, Holder &initHolder, bool runInitHolder)		//ML : previously known as receiveSeed
		{
			MPI_Barrier(MPI_COMM_WORLD);
			while (true)
			{



				/*bool hasTasks = true;
				
				while (hasTasks)
				{
					Holder* h = static_cast<Holder*>(_branchHandler.popHolder());
					if (h)
					{
						_branchHandler.forward(function, -1, *h);
						delete h;
					}
					else
					{
						hasTasks = false;
					}
				}*/
				//if caller wants us to run the init holder, we will run it once.
				//normally it is set to true only for world_rank 1
				if (runInitHolder)
				{
					if (verbose > 0)
					{
						cout<<cnow()<<"WR="<<world_rank<<" main thread is running the init holder."<<endl;
					}
					runInitHolder = false;
					_branchHandler.forward(function, -1, initHolder);
					
					
					if (verbose > 0)
					{
						cout<<cnow()<<"WR="<<world_rank<<" main thread done running the init holder."<<"bestval="<<_branchHandler.getBestVal()<<endl;
					}

				}
				
				//wait until all work is finished
				//TODO : cleaner way to do this?
				long cptsleep = 0;
				while (_branchHandler.hasBusyThreads_nomutex())
				{
					_branchHandler.wait();
					
					/*std::this_thread::sleep_for(std::chrono::milliseconds(100));
					cptsleep++;
					
					if (cptsleep % 10 == 0)
					{
						cout<<"WR="<<world_rank<<" been sleepin for "<<cptsleep<<"cycles."<<endl;
					}*/
				}
				
				
				//At this point, we're done and all threads are done
				if (verbose > 0)
				{
					cout<<cnow()<<"WR="<<world_rank<<" sending TAG_AVAIL"<<endl;
				}
				
				
				
				
				//let assigned workers know that we have nothing for them, and they should move on to something else
				for (int i = 1; i < world_size; ++i)
				{
					MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, win_waitingNodes);
					if (winbuf_waitingNodes[i] == 1)
					{
						winbuf_waitingNodes[i] = 0;
						MPI_Win_unlock(world_rank, win_waitingNodes);
						
						if (verbose > 0)
						{
							cout<<cnow()<<"WR="<<world_rank<<" sending TAG_NOWORK to "<<i<<endl;
						}
						MPI_Send(&world_rank, 1, MPI_INT, i, TAG_NOWORK, MPI_COMM_WORLD);
						
						
					}
					else
					{
						MPI_Win_unlock(world_rank, win_waitingNodes);
					}
				
				}

				
				
				
				MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_AVAIL, MPI_COMM_WORLD);	//let center know we are available
					
				
				
			
				if (verbose > 0)
					cout<<cnow()<<"WR="<<world_rank<<" now receiving"<<endl;
			
				MPI_Status status;
				int Bytes; // Bytes to be received

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &Bytes);					  // receives total number of datatype elements of the message

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);


				
				if (verbose > 0)
				{
					cout<<cnow()<<"WR="<<world_rank<<" received tag "<<status.MPI_TAG<<" from "<<status.MPI_SOURCE<<endl;
				}
				
				if (status.MPI_TAG == TAG_NOWORK)
				{
					if (verbose > 0)
					{
						cout<<cnow()<<"WR="<<world_rank<<" received TAG_NOWORK.  Now sending TAG_AVAIL to center."<<endl;
					}
					
					//if we get here, it's because we told center we were available.  Then center assigned us to some working node.
					//But unluckily, that node is telling us here that it has no work for us.  So we'll just tell center that we're available again. 
					MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_AVAIL, MPI_COMM_WORLD);
					delete [] in_buffer;
				}
				else if (status.MPI_TAG == TAG_TERMINATE)
				{
					if (verbose >= 0)
						cout<<cnow()<<"WR="<<world_rank<<" got a terminate call"<<endl;
					delete [] in_buffer;
					break;
				}
				else if (status.MPI_TAG == TAG_TASK)
				{
					if (verbose > 0)
					{
						cout<<cnow()<<"WR="<<world_rank<<" received a task from "<<status.MPI_SOURCE<<endl;
					}
				
				
					Holder holder(-1); // copy types

					std::stringstream ss;
					for (int i = 0; i < Bytes; i++)
						ss << in_buffer[i];

					auto _deser = std::bind_front(deserializer, std::ref(ss));
					std::apply(_deser, holder.getArgs());

					delete [] in_buffer;
					
					
					//ML EDIT : this has changed.  now the one who sent the work tells center to whom they sent
					//we just got a task, so we must let center know we might want some help with that
					MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_STARTEDWORKING, MPI_COMM_WORLD);
					

					
					_branchHandler.forward(function, -1, holder); //we forward so that current thread can participate
				}
				else
				{
					cout<<cnow()<<"WR="<<world_rank<<" : received tag "<<status.MPI_TAG<<" from "<<status.MPI_SOURCE<<" but it doesn't know what to do."<<endl;
				
				}
				//if you add another possible tag, don't forget to delete [] in_buffer
				
			}
		
		}
		
		

	private:


		void win_allocate()
		{
			MPI_Alloc_mem(1 * sizeof(int), MPI_INFO_NULL, &winbuf_centerBestVal);
			MPI_Win_create(winbuf_centerBestVal, sizeof(int), sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win_centerBestVal);
			MPI_Win_fence(0, win_centerBestVal);
			
			
			MPI_Alloc_mem(world_size * sizeof(int), MPI_INFO_NULL, &winbuf_waitingNodes);
			for (int i = 0; i < world_size; i++)
			{
				winbuf_waitingNodes[i] = 0;
			}
			MPI_Win_create(winbuf_waitingNodes, sizeof(int) * world_size, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win_waitingNodes);
			MPI_Win_fence(0, win_waitingNodes);
		}
		
		
		void win_deallocate()
		{
			MPI_Win_free(&win_centerBestVal);
			MPI_Win_free(&win_waitingNodes);
			
			MPI_Free_mem(winbuf_waitingNodes);
			MPI_Free_mem(winbuf_centerBestVal);
		}



		


		

	
	};

} // namespace library

#endif
