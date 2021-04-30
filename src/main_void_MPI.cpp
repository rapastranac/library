#ifdef VC_VOID_MPI

#include "../include/main.h"
#include "../include/Graph.hpp"
#include "../MPI_Modules/MPI_Scheduler.hpp"

#include "../include/VC_void_MPI.hpp"

#include "../include/resultholder/ResultHolder.hpp"
#include "../include/BranchHandler.hpp"
#include "../include/DLB_Handler.hpp"

#include "Tree.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>

GemPBA::DLB_Handler &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance(); // parallel library

std::mutex mtx;
size_t leaves = 0;

int k = 28;

void foo(int id, int depth, float treeIdx, void *parent)
{
	using HolderType = GemPBA::ResultHolder<void, int, float>;

	//fmt::print("rank {}, id : {} depth : {} treeIdx : {}\n", branchHandler.rank_me(), id, depth, treeIdx); // node id in the tree

	if (depth >= k)
	{
		std::scoped_lock<std::mutex> lck(mtx);
		leaves++;
		branchHandler.holdSolution(leaves, leaves, serializer);

		int tmp = branchHandler.refValue();
		if ((int)leaves > tmp)
			if (leaves % 8 == 0)
				branchHandler.updateRefValue(leaves, true);

		//fmt::print("rank {}, Leaves : {}\n", branchHandler.rank_me(), leaves);
		return;
	}
	int newDepth = depth + 1;
	HolderType hol_l(dlb, id, parent);
	float newTreeIdx = treeIdx + pow(2, depth);
	hol_l.holdArgs(newDepth, newTreeIdx);

	//if (depth < 5)
	branchHandler.try_push_MP<void>(foo, id, hol_l, serializer);
	//else
	//	foo(id, newDepth, newTreeIdx, nullptr);
	//	branchHandler.try_push_MT<void>(foo, id, hol_l); // only threads

	//std::this_thread::sleep_for(1s);

	foo(id, newDepth, treeIdx, nullptr);
}

int bar(int id, int depth, float treeIdx, void *parent)
{
	using H_Type = GemPBA::ResultHolder<int, int, float>;

	if (depth > 4)
	{
		std::scoped_lock<std::mutex> lck(mtx);
		leaves++;
		return leaves;
	}
	int newDepth = depth + 1;
	//fmt::print("rank {}, id : {} depth : {} treeIdx : {}\n", branchHandler.rank_me(), id, depth, treeIdx);
	H_Type hol_l(dlb, id, parent);
	float newTreeIdx = treeIdx + pow(2, depth);
	hol_l.holdArgs(newDepth, newTreeIdx);

	//branchHandler.try_push_MP<void>(foo, id, hol, serializer);
	branchHandler.try_push_MT<int>(bar, id, hol_l);

	//std::this_thread::sleep_for(1s);
	int r_left = bar(id, newDepth, treeIdx, nullptr);

	int r_right = hol_l.get();
	return r_left > r_right ? r_left : r_right;
}

int main_void_MPI(int numThreads, int prob, std::string filename)
{
	//using HolderType = GemPBA::ResultHolder<void, int, Graph>;

	Graph graph;
	Graph oGraph;
	VC_void_MPI cover;

	auto mainAlgo = std::bind(&VC_void_MPI::mvc, &cover, _1, _2, _3, _4); // target algorithm [all arguments]

	auto &mpiScheduler = GemPBA::MPI_Scheduler::getInstance(); // MPI MPI_Scheduler
	branchHandler.link_mpiScheduler(&mpiScheduler);
	int rank = mpiScheduler.init(NULL, NULL); // initialize MPI and member variable linkin
											  //HolderType holder(handler);									//it creates a ResultHolder, required to retrive result

	//GemPBA::ResultHolder<int, int, float> hldr(dlb, -1, nullptr);
	//float val = 845.515;
	//hldr.holdArgs(val);
	//branchHandler.setMaxThreads(6);

	//foo(-1, 0, -1, nullptr);
	//auto res = bar(-1, 0, -1, nullptr);

	//while (!branchHandler.isDone())
	//	fmt::print("Not done yet !!\n");

	//branchHandler.try_push_MT<int>(bar, -1, hldr);
	//std::this_thread::sleep_for(1s); //emulates quick task
	//branchHandler.wait();
	//fmt::print("Leaves : {}\n", res);
	//fmt::print("Thread calls : {}\n", branchHandler.number_thread_requests());

	//return 0;

	/* previous input and output required before following condition
	thus, other nodes know the data type*/
	using HolderType = GemPBA::ResultHolder<void, int, float>;

	HolderType holder(dlb, -1); //it creates a ResultHolder, required to retrive result
	int depth = 0;

	graph.readEdges(filename);

	// ******************************************************************
	// temp for mini-cluster
	//if (rank == 1)
	//	numThreads = 5; //cuz center is also in this machine
	//if (rank == 2)
	//	numThreads = 4;
	// ******************************************************************

	//int preSize = graph.preprocessing();
	//size_t k_mm = cover.maximum_matching(graph);
	//size_t k_uBound = graph.max_k();
	//size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
	//handler.setRefValue(k_prime);
	//cover.init(graph, numThreads, filename, prob);

	//mpiScheduler.setThreadsPerNode(numThreads);
	//holder.holdArgs(depth, graph);

	// foo(int id, int depth, float treeIdx, void *parent) ********************
	float treeIdx = 1;
	std::string buffer = serializer(depth, treeIdx);

	// ************************************************************************
	//branchHandler.setRefValue(0, "MAXIMISE");
	branchHandler.setRefValStrategyLookup("maximise");

	if (rank == 0)
		mpiScheduler.runCenter(buffer.data(), buffer.size());
	else
	{
		branchHandler.initThreadPool(numThreads);
		auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, float>(foo, deserializer);
		auto resultFetcher = branchHandler.constructResultFetcher();
		mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer);
	}

	mpiScheduler.barrier();

	// *****************************************************************************************
	// this is a generic way of getting information from all the other processes after execution retuns
	auto world_size = mpiScheduler.getWorldSize();
	std::vector<double> idleTime(world_size);
	std::vector<size_t> threadRequests(world_size);

	double idl_tm = 0;
	size_t rqst = 0;

	if (rank != 0)
	{ //rank 0 does not run an instance of BranchHandler
		idl_tm = branchHandler.getPoolIdleTime();
		rqst = branchHandler.number_thread_requests();
	}

	// here below, idl_tm is the idle time of the other ranks, which is gathered by .allgather() and stored in
	// a contiguos array
	mpiScheduler.allgather(idleTime.data(), &idl_tm, MPI_DOUBLE);
	mpiScheduler.allgather(threadRequests.data(), &rqst, MPI_UNSIGNED_LONG_LONG);

	// *****************************************************************************************
	//ipc_handler.finalize();
	//return 0;

	if (rank == 0)
	{
		auto solutions = mpiScheduler.fetchResVec();

		int summation = 0;
		for (size_t i = 0; i < mpiScheduler.getWorldSize(); i++)
		{
			if (solutions[i].first != -1)
				summation += solutions[i].first;
		}
		fmt::print("\n\n");
		fmt::print("Summation of refValGlobal : {}\n", summation);

		//std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // to let other processes to print
		mpiScheduler.printStats();

		//print sumation of refValGlobal

		//std::stringstream ss;
		//std::string buffer = mpiScheduler.fetchSolution(); // returns a stringstream
		//
		//ss << buffer;
		//
		//deserializer(ss, oGraph);
		//auto cv = oGraph.postProcessing();
		//fmt::print("Cover size : {} \n", cv.size());
		//
		double sum = 0;
		for (int i = 1; i < world_size; i++)
		{
			sum += idleTime[i];
		}
		fmt::print("\nGlobal pool idle time: {0:.6f} seconds\n\n\n", sum);

		// **************************************************************************
		auto tasks_per_process = mpiScheduler.executedTasksPerNode();

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("tasks executed by rank {} = {} \n", rank, tasks_per_process[rank]);
		}
		fmt::print("\n");

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("rank {}, thread requests: {} \n", rank, threadRequests[rank]);
		}

		fmt::print("\n\n\n");

		// **************************************************************************
	}
	mpiScheduler.finalize();
	return 0;
}

#endif