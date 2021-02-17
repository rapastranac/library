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

struct S
{
	S(std::shared_ptr<S> parent)
	{
		if (!parent)
		{
			this->root = &itself;
			return;
		}
		else
		{
			this->parent = parent;
			this->root = &(*parent->root);
			this->parent->children.push_back(this);
		}
	}

	void prune()
	{
		root = nullptr;
		root = &itself;
		parent = nullptr;
	}

	~S()
	{
		printf("Destructor called for : %d \n", val);
	}

	int val;
	std::shared_ptr<S> parent;
	S **root = nullptr;
	S *itself = this;
	std::list<S *> children;
};

const int SIZE = 7;
std::vector<std::thread> threads(SIZE);

void foo(int depth, std::shared_ptr<S> parent)
{
	if (depth >= SIZE)
		return;
	auto s = std::make_shared<S>(parent);
	s->val = depth;

	if (depth == 3)
		s->prune();

	threads[depth] = std::thread(foo, depth + 1, s);

	//std::this_thread::sleep_for(1s);

	printf("Hello from level %d \n", depth);
}

int main(int argc, char *argv[])
{
	{
		threads[0] = std::thread(foo, 1, nullptr);
	}

	for (size_t i = 0; i < SIZE; i++)
	{
		if (threads[i].joinable())
			threads[i].join();
	}

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VertexCover cover;
	std::vector<int> *vec;

#ifndef MPI_ENABLED
	printf("MPI disable section \n");

	auto file = "input/prob_4/400/00400_1";
	graph.readEdges(file);

	//auto ss = user_serializer(graph);
	//int SIZE = ss.str().size();
	//char buffer[SIZE];
	//std::memcpy(buffer, ss.str().data(), SIZE);
	//std::stringstream ss2;
	//for (int i = 0; i < SIZE; i++)
	//{
	//	ss2 << buffer[i];
	//}
	//user_deserializer(ss2, oGraph);

	cover.init(graph, 12, file, 4);
	cover.findCover(1);
	cover.printSolution();

	auto res = cover.getGraphRes();
	auto ss = user_serializer(res);
	int SIZE = ss.str().size();
	char buffer[SIZE];
	std::memcpy(buffer, ss.str().data(), SIZE);
	std::stringstream ss2;
	for (int i = 0; i < SIZE; i++)
	{
		ss2 << buffer[i];
	}
	user_deserializer(ss2, oGraph);

	auto mvc = oGraph.postProcessing();
	int mvc_size = mvc.size();

	printf("mvc size : %d \n", mvc_size);

	return 0;
#else
	printf("MPI enabled section \n");

	auto mainAlgo = std::bind(&VertexCover::mvc, &cover, _1, _2, _3); // target algorithm [all arguments]
	//graph.readEdges(file);

	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	int rank = scheduler.initMPI(argc, argv);					// initialize MPI and member variable linkin
																//HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	/* previous input and output required before following condition
	thus, other nodes know the data type*/

	//handler.functionIsVoid();
	//auto file = "input/prob_4/600/0600_93";
	auto file = "input/prob_4/600/00600_1";

	HolderType holder(handler); //it creates a ResultHolder, required to retrive result
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
#endif
}
