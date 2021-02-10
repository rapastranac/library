#include "../include/main.h"
#include "../include/VertexCover.hpp"
#include "../include/Graph.hpp"
#include "../MPI_Modules/Scheduler.hpp"

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

int main(int argc, char *argv[])
{

	using HolderType = library::ResultHolder<void, int, Graph>;

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VertexCover cover;

#ifndef MPI_ENABLE
	printf("MPI disable section \n");

	auto file = "input/prob_4/400/00400_1";
	graph.readEdges(file);

	/*
	graph.preprocessing();

	auto ss = user_serializer(graph);
	int SIZE = ss.str().size();
	char buffer[SIZE];
	std::memcpy(buffer, ss.str().data(), SIZE);
	std::stringstream ss2;
	for (int i = 0; i < SIZE; i++)
	{
		ss2 << buffer[i];
	}
	user_deserializer(ss2, oGraph);
*/
	cover.init(graph, 12, file, 4);
	cover.findCover(1);
	cover.printSolution();

	return 0;
//#endif
//#ifdef MPI_ENABLE
#else
	printf("MPI enable section \n");

	auto mainAlgo = std::bind(&VertexCover::mvc, &cover, _1, _2, _3); // target algorithm [all arguments]
	//graph.readEdges(file);

	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	int rank = scheduler.initMPI(argc, argv);					// initialize MPI and member variable linkin
																//HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

	//handler.functionIsVoid();
	auto file = "input/prob_4/100/00100_1";

	HolderType holder(handler); //it creates a ResultHolder, required to retrive result
	int depth = 0;

	//if (rank == 0) //only center node will read input and printing results
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
		scheduler.printfStats();

		auto result = scheduler.retrieveResult(); // returns a stringstream
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
#endif
}
