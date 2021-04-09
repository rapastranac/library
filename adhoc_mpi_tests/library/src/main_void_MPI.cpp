#ifdef VC_VOID_MPI

#include "../include/main.h"
#include "../include/Graph.hpp"


#include "../include/VC_void_MPI_bitvec.hpp"

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>





int main_void_MPI(int numThreads, int prob, std::string filename)
{

MPI_Init(NULL, NULL);
{
	Graph graph;
	Graph oGraph;
	VCScheduler scheduler;
	scheduler.initMPI();
	VC_MPI vcmpi(scheduler);
	scheduler.vcmpi = &vcmpi;
	scheduler.isdone = false;


	//int rank = scheduler.initMPI(NULL, NULL);					// initialize MPI and member variable linkin
	
	graph.readEdges(filename);
	vcmpi.setGraph(graph);


	int gsize = graph.adj.size() + 1;	//+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
	
cout<<"Gsize="<<gsize<<endl;
	scheduler.bestVal = gsize;
	
	scheduler.winBestSolBuf[0] = gsize;
	
	
	gbitset allzeros(gsize);
	gbitset allones = ~allzeros;
	
	
	typedef std::chrono::high_resolution_clock clock;
	typedef std::chrono::duration<float, std::milli> duration;
	static clock::time_point start = clock::now();
	
	
	if (scheduler.world_rank == 0)
	{
		scheduler.runCenter();
	}
	else
	{
		if (scheduler.world_rank == 1)
		{
			scheduler.addTask(allones, allzeros);
			cout<<"Main task added to wr=1"<<endl;
		}
		scheduler.runNode();
	}
	
	
	
	duration elapsed = clock::now() - start;
	
	if (scheduler.world_rank == 0)
	{
		cout<<"Time = "<<elapsed.count()/1000.0<<endl;
	}
	
	


	std::this_thread::sleep_for(2000ms);
	
	
	
	cout<<"DONE, rank "<<scheduler.world_rank<<" says best="<<scheduler.getBestVal()<<" and "<<"winbest="<<scheduler.winBestSolBuf[0]<<endl;
	
	scheduler.finalize();
}
	MPI_Finalize();
	
	return 0;
	
}

#endif
