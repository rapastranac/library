#include "../include/main.h"
#include "../include/Sort.h"
#include "../MPI_Modules/Scheduler.hpp"
#include "../MPI_Modules/Serialize.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

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

int main(int argc, char **argv)
{

	Sort objet;

	library::BranchHandler &handler = library::BranchHandler::getInstance();

	//buildUnsorted(10, 50000000);
	//return 0;

	library::Scheduler scheduler;

	std::vector<size_t> arr;
	std::vector<size_t> sorted;
	read(arr, "input/1000.txt");

	//library::Serialize instance;
	//instance.serialize(-1, arr);

	auto _f = std::bind(&Sort::mergeSort, objet, 0, arr);
	scheduler.start(argc, argv, handler, _f, -1, arr);
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
