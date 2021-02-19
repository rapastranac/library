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

static int id = 0;
int createID()
{
	return ++id;
}

struct S
{
	S(std::shared_ptr<S> parent)
	{
		this->id = createID();

		if (!parent)
		{
			root = &self;
			return;
		}
		else
		{
			this->parent = parent;
			//this->parent->children.emplace_back(std::make_shared<S>(*this));
			root = &(*parent->root);
		}
	}

	void lowerRoot()
	{
		*(this->root) = &(*this);
		parent = nullptr;
	}

	void prune()
	{
		root = nullptr;
		//root = &itself;
		root = &self;
		parent = nullptr;
	}

	void addChildren(std::shared_ptr<S> &child)
	{
		children.push_back(child);
	}

	~S()
	{
		if (!children.empty())
		{
			children.clear();
			//for (auto &child : children)
			//{
			//	int count = child.use_count();
			//	child.reset();
			//}
		}
		printf("Destructor called for  id : %d \n", id);
	}

	int val;
	int id = -1;
	std::shared_ptr<S> parent;
	S **root = nullptr;
	std::shared_ptr<S> itself;
	S *self = this;
	std::list<std::shared_ptr<S>> children;
};

const int SIZE = 7;

ctpl::Pool ppool(SIZE);

template <class F, class SMRT_PTR>
void forwarder(F &&f, int depth, SMRT_PTR s)
{
	if (depth > 3)
	{
		auto *root = *s->root; //raw pointer
		if (root->children.size() > 1)
		{
			auto leftMost = root->children.front(); // raw pointers
			root->children.pop_front();
			auto rightMost = root->children.front(); // raw pointers
			root->children.pop_front();

			rightMost->prune();
			leftMost->lowerRoot();

			int c1 = rightMost.use_count();
			int c2 = leftMost.use_count();
			int c3 = s.use_count();
			ppool.push(f, depth + 1, rightMost);
			return;
		}
	}
	int count = s.use_count();
	ppool.push(f, depth, s);
}

auto deleter = [](S *s) {
	if (s)
	{
		printf("structure holding id = %d, deleted\n", s->id);
		delete s;
		return;
	}
	printf("structure holding id = %d, was already deleted\n", s->id);
};

void foo(int id, int depth, std::shared_ptr<S> parent)
{
	if (depth >= SIZE)
		return;
	std::shared_ptr<S> left(new S(parent), deleter);
	std::shared_ptr<S> right(new S(parent), deleter);
	if (parent)
	{
		parent->addChildren(left);
		parent->addChildren(right);
	}
	left->val = depth;
	right->val = depth;

	int c1 = left.use_count();
	int c2 = right.use_count();
	int count = parent.use_count();

	//if (depth == 3)
	//	left->prune();

	forwarder(foo, depth + 1, left);

	printf("Hello from level %d \n", depth);
}

std::list<std::shared_ptr<S>> objs;

void bar(std::shared_ptr<S> dummy)
{
	int c = dummy.use_count();
	return;
}

int main(int argc, char *argv[])
{
	{
		std::shared_ptr<S> mt;
		{
			std::shared_ptr<S> A(new S(nullptr), deleter);
			//std::shared_ptr<S> B(new S(A), deleter);
			//std::shared_ptr<S> C(new S(B), deleter);
			S *ptr = A.get();
			mt.reset(ptr, deleter);

			//bar(C);
			int c1 = A.use_count();
			//int c2 = B.use_count();
			//int c3 = C.use_count();
			//objs.push_back(A);
			//objs.push_back(B);
			//objs.push_back(C);
			c1 = A.use_count();
			//c2 = B.use_count();
			//c3 = C.use_count();
		}
		int c1 = mt.use_count();
		int dfs = 4543;
	}
	objs.clear();
	int dgf = 3453;
	{
		ppool.push(foo, 0, nullptr);
	}

	//std::this_thread::sleep_for(2s);
	ppool.interrupt(true);

	return 0;

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

	cover.init(graph, 2, file, 4);
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
