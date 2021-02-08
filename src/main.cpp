#include "../include/main.h"
#include "../include/VertexCover.hpp"
#include "../include/Graph.hpp"
#include "../MPI_Modules/Scheduler.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>

int main(int argc, char *argv[])
{

	using HolderType = library::ResultHolder<void, int, Graph>;

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VertexCover cover;

	auto mainAlgo = std::bind(&VertexCover::mvc, &cover, _1, _2, _3); // target algorithm [all arguments]

	auto file = "input/prob_4/400/00400_1";
	graph.readEdges(file);

	/*auto ss = user_serializer(graph);
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

	cover.init(graph, 1, file, 4);
	cover.findCover(12);
	cover.printSolution();

	return 0;

#ifdef MPI_ENABLE
	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	int rank = scheduler.initMPI(argc, argv);					// initialize MPI and member variable linkin
	scheduler.setThreadsPerNode(1);								// set number of thread to be used per node
	HolderType holder(handler);									//it creates a ResultHolder, required to retrive result
#endif

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

#ifndef MPI_ENABLE

	handler.setMaxThreads(3);
	auto begin = std::chrono::steady_clock::now();
	sorted = mainAlgo(0, arr);
	auto end = std::chrono::steady_clock::now();
	double time_tmp = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count()) / 1000.0;
	printf("Elapsed time : %3.2f \n", time_tmp);
	printf("Sorted size : %d \n", sorted.size());

	std::cout << "first : " << sorted.front() << " ";
	std::cout << "last : " << sorted.back() << " ";
	std::cout << "\n";

	return 0;

#else

	if (rank == 0) //only center node will read input and printing results
	{
		//read(arr, "input/1000.txt");
		//holder.holdArgs(arr);

		//handler.setRefValue(arr.size());

		//auto ss = user_serializer(arr);
		/*	std::stringstream ss = std::args_handler::unpack_tuple(user_serializer, holder.getArgs());
		int SIZE = ss.str().size();
		char *buffer = new char[SIZE];
		std::stringstream ss2;
		std::memcpy(buffer, ss.str().data(), SIZE);
		for (int i = 0; i < SIZE; i++)
			ss2 << buffer[i];
		user_deserializer(ss2, sorted); */
	}

	//scheduler.start<std::vector<size_t>>(mainAlgo,
	//									 holder,
	//									 user_serializer,
	//									 user_deserializer); // solve in parallel, ignore args{id, tracker(is applicable)}
	scheduler.finalize();

	if (rank == 0)
	{
		scheduler.printfStats();

		auto result = scheduler.retrieveResult(); // returns a stringstream
		//user_deserializer(result, sorted);
		//printf("Sorted size : %d \n", sorted.size());

		//std::cout << "first : " << sorted.front() << " ";
		//std::cout << "last : " << sorted.back() << " ";

		//for (size_t i = 0; i < sorted.size(); i++)
		//{
		//	std::cout << sorted[i] << " ";
		//}
		std::cout << "\n";
	}

#endif

	return 0;

	//objet.setUnsorted(arr);

	//mergeSort(-1, arr, 0, arr.size() - 1);
	//std::vector<size_t> arr{ 12, 11, 13, 5, 6, 7 };
	//objet.sort();
	//arr = mergeSort(-1, arr);
	//objet.printLog();

	//sorted = objet.fetch();

	//print(sorted);

	return 0;
}
