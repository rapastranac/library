#include "../include/main.h"
#include "../include/Sort.h"
#include "../MPI_Modules/Scheduler.hpp"
#include "../MPI_Modules/serialize/oarchive.hpp"
#include "../MPI_Modules/serialize/iarchive.hpp"
#include "../MPI_Modules/serialize/stream.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include <unordered_set>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

void print(std::vector<size_t> &ordered)
{

	std::fstream file;
	std::string path = "ouput";
	std::string name = std::to_string(ordered.size()) + ".txt";
	if (!fs::is_directory(path))
	{
		fs::create_directory(path);
	}
	file.open(path + "/" + name, std::ofstream::in | std::ofstream::out | std::ofstream::app);

	auto it = ordered.begin();
	while (it != ordered.end())
	{
		file << *it << "\n";
		it++;
	}

	file.close();
}

int main(int argc, char *argv[])
{

	//buildUnsorted(10, 1000000);
	//return 0;

	Sort objet;
	auto mainAlgo = std::bind(&Sort::mergeSort, &objet, _1, _2); // target algorithm [all arguments]

	std::vector<size_t> arr;
	std::vector<size_t> sorted;
	//read(arr, "input/1000.txt");
	read(arr, "input/50000000.txt");

	auto &handler = library::BranchHandler::getInstance();
	library::ResultHolder<std::vector<size_t>, std::vector<size_t>> holder(handler);

	//handler.setMaxThreads(1);
	//sorted = mainAlgo(0, arr);
	//printf("Sorted size : %d \n", sorted.size());
	//
	//std::cout << "first : " << sorted.front() << " ";
	//std::cout << "last : " << sorted.back() << " ";
	//std::cout << "\n";
	//return 0;

	holder.holdArgs(arr);

	//auto ss = user_serializer(arr);
	/*	std::stringstream ss = std::args_handler::unpack_tuple(user_serializer, holder.getArgs());
	int SIZE = ss.str().size();
	char *buffer = new char[SIZE];
	std::stringstream ss2;
	std::memcpy(buffer, ss.str().data(), SIZE);
	for (int i = 0; i < SIZE; i++)
		ss2 << buffer[i];
	user_deserializer(ss2, sorted); */

	auto &scheduler = library::Scheduler::getInstance(handler); // MPI Scheduler
	scheduler.setThreadsPerNode(1);

	scheduler.start<std::vector<size_t>>(argc, argv,
										 mainAlgo,
										 holder,
										 user_serializer,
										 user_deserializer); // solve in parallel, ignore args{id, tracker(is applicable)}
	int rank = scheduler.finalize();
	if (rank == 0)
	{
		scheduler.printfStats();

		auto result = scheduler.retrieveResult(); // returns a stringstream
		user_deserializer(result, sorted);
		printf("Sorted size : %d \n", sorted.size());

		std::cout << "first : " << sorted.front() << " ";
		std::cout << "last : " << sorted.back() << " ";

		//for (size_t i = 0; i < sorted.size(); i++)
		//{
		//	std::cout << sorted[i] << " ";
		//}
		std::cout << "\n";
	}
	return 0;

	objet.setUnsorted(arr);

	//mergeSort(-1, arr, 0, arr.size() - 1);
	//std::vector<size_t> arr{ 12, 11, 13, 5, 6, 7 };
	objet.sort();
	//arr = mergeSort(-1, arr);
	objet.printLog();

	sorted = objet.fetch();

	//print(sorted);

	return 0;
}
