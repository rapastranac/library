#include "../include/main.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <string>
#include <random>

namespace fs = std::filesystem;

void printTofile(std::unordered_set<size_t> &data)
{
	std::fstream file;
	std::string path = "input";
	std::string name = std::to_string(data.size()) + ".txt";
	if (!fs::is_directory(path))
	{
		fs::create_directory(path);
	}
	file.open(path + "/" + name, std::ofstream::in | std::ofstream::out | std::ofstream::app);

	auto it = data.begin();
	while (it != data.end())
	{
		file << *it << "\n";
		it++;
	}

	file.close();
}

void buildUnsorted(size_t itemSize, size_t packSize)
{
	std::unordered_set<size_t> data;

	size_t mx = data.max_size();

	srand(time(NULL));

	std::random_device rd;	// Will be used to obtain a seed for the random number engine
	std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
	std::uniform_int_distribution<> distrib(1, 9);

	while (data.size() < packSize)
	{
		std::string str;
		for (size_t i = 0; i < itemSize; i++)
		{
			size_t tmp = distrib(gen);
			str = str + std::to_string(tmp);
		}
		std::string::size_type sz;
		size_t val = std::stoll(str, &sz);
		data.insert(val);
	}
	printTofile(data);
}

void read(std::vector<size_t> &unsorted, std::string path)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		printf("Input file not found");
		throw "Input file not found";
	}
	size_t val;
	while (!file.eof())
	{
		file >> val;
		unsorted.push_back(val);
	}
	file.close();
}
