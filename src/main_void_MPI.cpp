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

int main_void_MPI(int argc, char *argv[])
{
	using HolderType = library::ResultHolder<void, int, Graph>;

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VC_void_MPI cover;

	auto mainAlgo = std::bind(&VC_void_MPI::mvc, &cover, _1, _2, _3, _4); // target algorithm [all arguments]
	//graph.readEdges(file);

	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	int rank = scheduler.initMPI(argc, argv);					// initialize MPI and member variable linkin
																//HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

	//handler.functionIsVoid();
	//auto file = "input/prob_4/600/0600_93";
	auto file = "input/prob_4/400/00400_1";

	HolderType holder(handler, -1); //it creates a ResultHolder, required to retrive result
	int depth = 0;

	//if (rank == 0) //only center node will read input and printing resultscd
	//{
	//}
	graph.readEdges(file);

	int preSize = graph.preprocessing();

	size_t k_mm = cover.maximum_matching(graph);
	size_t k_uBound = graph.max_k();
	size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
	//cover.setMVCSize(k_prime);
	handler.setRefValue(k_prime);

	cover.init(graph, 1, file, 4);

	scheduler.setThreadsPerNode(1);
	holder.holdArgs(depth, graph);
	scheduler.start<void>(mainAlgo, holder, user_serializer, user_deserializer);

	if (rank == 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(300)); // to let other processes to print
		scheduler.printfStats();

		std::stringstream &result = scheduler.retrieveResult(); // returns a stringstream
		int SIZE = result.str().size();
		user_deserializer(result, oGraph);
		auto cv = oGraph.postProcessing();
		printf("Cover size : %d \n", cv.size());

		//std::cout << "first : " << sorted.front() << " ";
		//std::cout << "last : " << sorted.back() << " ";

		//for (size_t i = 0; i < sorted.size(); i++)
		//{
		//	std::cout << sorted[i] << " ";
		//}
		std::cout << "\n";
	}
	scheduler.finalize();

	return 0;
}

#endif