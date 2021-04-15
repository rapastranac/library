

#include "../include/main.h"
#include "../include/Graph.hpp"
#include "../MPI_Modules/Scheduler.hpp"

#ifdef BITVECTOR_VC
	#include "../include/VC_void_MPI_bitvec.hpp"
#else
	#include "../include/VC_void_MPI.hpp"
#endif

#include "../include/ResultHolder.hpp"
#include "../include/BranchHandler.hpp"

#include <chrono>
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


			/*int provided;
			MPI_Init_thread(NULL, NULL, MPI_THREAD_SERIALIZED, &provided);
			if (provided < MPI_THREAD_SERIALIZED)
			{
				cout<<"The threading support level is lesser than that demanded, got "<<provided<<endl;
				MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
			}*/
			
			
			

	using HolderType = library::ResultHolder<void, int, gbitset, int>;

	auto &branchHandler = library::BranchHandler::getInstance(); // parallel library

	branchHandler.init();
	cout<<"NUMTHREADS="<<numThreads<<endl;
	branchHandler.setMaxThreads(numThreads - 1);	//-1 to exclude current
	cout<<"set max threads to "<<numThreads<<endl;
	branchHandler.functionIsVoid();


	Graph graph;
	VC_void_MPI cover;

	auto function = std::bind(&VC_void_MPI::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
					// initialize MPI and member variable linkin
												
	graph.readEdges(filename);
	//graph.preprocessing();	

	cover.init(graph, numThreads, filename, prob);
	cover.setGraph(graph);

	
	int gsize = graph.adj.size() + 1;	//+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
	gbitset allzeros(gsize);
	gbitset allones = ~allzeros;

	//handler.setBestValue(gsize);
	
	int zero = 0;
	int solsize = graph.size();
	cout<<"solsize="<<solsize<<endl;
	HolderType holder(-1);
	holder.holdArgs(zero, allones, zero);
	
	cout<<"Starting MPI node "<<branchHandler.getWorldRank()<<endl;
	branchHandler.startMPINode(holder, function, user_serializer, user_deserializer);

	
	branchHandler.finalize();

	return 0;
}



