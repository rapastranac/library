#ifdef VC_VOID_MPI

#include "../include/main.h"
#include "../include/Graph.hpp"
#include "../MPI_Modules/IPC_Handler.hpp"

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

void foo(int id, int depth, float value, void *parent)
{
	fmt::print("Hello from foo, id : {}\n");
}

int main_void_MPI(int numThreads, int prob, std::string filename)
{
	//using HolderType = GemPBA::ResultHolder<void, int, Graph>;
	using HolderType = GemPBA::ResultHolder<void, int, float>;

	auto &handler = GemPBA::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VC_void_MPI cover;

	//auto mainAlgo = std::bind(&VC_void_MPI::mvc, &cover, _1, _2, _3, _4); // target algorithm [all arguments]

	auto &ipc_handler = GemPBA::IPC_Handler::getInstance(); // MPI IPC_Handler
	int rank = ipc_handler.establish_IPC(NULL, NULL);		// initialize MPI and member variable linkin
															//HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

	HolderType holder(handler, -1); //it creates a ResultHolder, required to retrive result
	int depth = 0;

	graph.readEdges(filename);

	// ******************************************************************
	// temp for mini-cluster
	if (rank == 1)
		numThreads = 5; //cuz center is also in this machine
	if (rank == 2)
		numThreads = 4;
	// ******************************************************************

	//int preSize = graph.preprocessing();
	//size_t k_mm = cover.maximum_matching(graph);
	//size_t k_uBound = graph.max_k();
	//size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
	//handler.setRefValue(k_prime);
	//cover.init(graph, numThreads, filename, prob);

	ipc_handler.setThreadsPerNode(numThreads);
	//holder.holdArgs(depth, graph);
	holder.holdArgs(depth, 6.7);
	std::stringstream ss;
	user_deserializer(std::ref(ss), depth, graph);

	if (rank == 0)
		ipc_handler.start(ss.str().data(), ss.str().size());
	else
	{
		auto receiver = handler.receive<void, int, float>(foo, user_deserializer);
		ipc_handler.listen(receiver);
	}

	ipc_handler.barrier();

	// *****************************************************************************************
	// this is a generic way of getting information from all the other processes after execution retuns
	auto world_size = ipc_handler.getWorldSize();
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
	ipc_handler.allgather(idleTime.data(), &idl_tm, MPI_DOUBLE);
	ipc_handler.allgather(threadRequests.data(), &rqst, MPI_UNSIGNED_LONG_LONG);

	// *****************************************************************************************
	//ipc_handler.finalize();
	//return 0;

	if (rank == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // to let other processes to print
		ipc_handler.printStats();

		std::stringstream result;
		ipc_handler.retrieveResult(result); // returns a stringstream

		user_deserializer(result, oGraph);
		auto cv = oGraph.postProcessing();
		fmt::print("Cover size : {} \n", cv.size());

		double sum = 0;
		for (int i = 1; i < world_size; i++)
		{
			sum += idleTime[i];
		}
		fmt::print("\nGlobal pool idle time: {0:.6f} seconds\n\n\n", sum);

		// **************************************************************************
		auto tasks_per_node = ipc_handler.executedTasksPerNode();

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("tasks executed by rank {} = {} \n", rank, tasks_per_node[rank]);
		}
		fmt::print("\n");

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("rank {}, thread requests: {} \n", rank, threadRequests[rank]);
		}

		fmt::print("\n\n\n");

		// **************************************************************************
	}
	ipc_handler.finalize();
	return 0;
}

#endif