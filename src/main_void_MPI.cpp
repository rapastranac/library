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

int main_void_MPI(int numThreads, int prob, std::string filename)
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

	graph.readEdges(filename);

	//auto ss = user_serializer(graph);
	//int buffer_size = ss.str().size();
	//fmt::print("SIZE = {} \n", buffer_size);
	//return 0;

	int preSize = graph.preprocessing();

	size_t k_mm = cover.maximum_matching(graph);

	size_t k_uBound = graph.max_k();
	size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
	//cover.setMVCSize(k_prime);
	handler.setRefValue(k_prime);

	cover.init(graph, numThreads, filename, prob);

	scheduler.setThreadsPerNode(numThreads);
	holder.holdArgs(depth, graph);
	scheduler.start<void>(mainAlgo, holder, user_serializer, user_deserializer);

	// *****************************************************************************************
	// this is a generic way of getting information from all the other processes after execution retuns
	auto world_size = scheduler.getWorldSize();
	std::vector<double> idleTime(world_size);
	std::vector<size_t> threadRequests(world_size);

	double idl_tm = 0;
	size_t rqst = 0;

	if (rank != 0)
	{ //rank 0 does not run an instance of BranchHandler
		idl_tm = handler.getPoolIdleTime();
		rqst = handler.getNumberRequests();
	}

	// here below, idl_tm is the idle time of the other ranks, which is gathered by .allgather() and stored in
	// a contiguos array
	scheduler.allgather(idleTime.data(), &idl_tm, MPI_DOUBLE);
	scheduler.allgather(threadRequests.data(), &rqst, MPI_UNSIGNED_LONG_LONG);

	// *****************************************************************************************

	if (rank == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // to let other processes to print
		scheduler.printStats();

		std::stringstream &result = scheduler.retrieveResult(); // returns a stringstream
		int SIZE = result.str().size();
		user_deserializer(result, oGraph);
		auto cv = oGraph.postProcessing();
		fmt::print("Cover size : {} \n", cv.size());

		double sum = 0;
		for (size_t i = 1; i < world_size; i++)
		{
			//fmt::print("idleTime[{}]: {} \n", i, idleTime[i]);
			sum += idleTime[i];
		}
		fmt::print("\nGlobal pool idle time: {0:.6f} seconds\n\n\n", sum);

		// **************************************************************************
		auto tasks_per_node = scheduler.executedTasksPerNode();

		for (size_t rank = 1; rank < world_size; rank++)
		{
			fmt::print("tasks executed by rank {} = {} \n", rank, tasks_per_node[rank]);
		}
		fmt::print("\n\n\n");

		for (size_t i = 1; i < world_size; i++)
		{
			fmt::print("rank {}, thread requests: {} \n", i, threadRequests[i]);
		}

		// **************************************************************************
	}
	scheduler.finalize();

	return 0;
}

#endif