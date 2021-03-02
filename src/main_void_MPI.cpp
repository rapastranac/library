#ifdef VC_VOID_MPI

#include "../include/main.h"
#include "../include/Graph.hpp"
#include "../MPI_Modules/Scheduler.hpp"

#include "../include/VC_void_MPI.hpp"

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

int main_void_MPI(int numThreads, std::string filename)
{
	using HolderType = library::ResultHolder<void, int, Graph>;

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VC_void_MPI cover;

	auto mainAlgo = std::bind(&VC_void_MPI::mvc, &cover, _1, _2, _3, _4); // target algorithm [all arguments]
	//graph.readEdges(file);

	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	int rank = scheduler.initMPI(NULL, NULL);					// initialize MPI and member variable linkin
																//HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

	HolderType holder(handler, -1); //it creates a ResultHolder, required to retrive result
	int depth = 0;

	//if (rank == 0) //only center node will read input and printing resultscd
	//{
	//}
	graph.readEdges(filename);

	int preSize = graph.preprocessing();

	size_t k_mm = cover.maximum_matching(graph);
	size_t k_uBound = graph.max_k();
	size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
	//cover.setMVCSize(k_prime);
	handler.setRefValue(k_prime);

	cover.init(graph, numThreads, filename, 4);

	scheduler.setThreadsPerNode(numThreads);
	holder.holdArgs(depth, graph);
	scheduler.start<void>(mainAlgo, holder, user_serializer, user_deserializer);

	auto world_size = scheduler.getWorldSize();
	std::vector<double> idleTime(world_size);
	double idl_tm = 0;

	if (rank != 0)
		idl_tm = handler.getPoolIdleTime();

	scheduler.allgather(idleTime.data(), &idl_tm);

	if (rank == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(300)); // to let other processes to print
		scheduler.printfStats();

		std::stringstream &result = scheduler.retrieveResult(); // returns a stringstream
		int SIZE = result.str().size();
		user_deserializer(result, oGraph);
		auto cv = oGraph.postProcessing();
		printf("Cover size : %zu \n", cv.size());

		double sum = 0;
		for (size_t i = 0; i < idleTime.size(); i++)
		{
			//fmt::print("idleTime[{}]: {} \n", i, idleTime[i]);
			sum += idleTime[i];
		}
		fmt::print("\nGlobal pool idle time: {0:.6f} seconds\n\n\n", sum);
	}
	scheduler.finalize();

	return 0;
}

#endif