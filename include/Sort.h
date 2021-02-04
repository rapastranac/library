//#pragma once
#ifndef SORT_H
#define SORT_H

#include "BranchHandler.hpp"
#include "ResultHolder.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>

#define MPI_TAG

using namespace std::placeholders;
namespace fs = std::filesystem;

auto user_serializer = [](auto &...args) {
	/* here inside, user can implement his/her favourite serialization method given the
	arguments pack and it must return a std::stream */
	std::stringstream ss;
	cereal::BinaryOutputArchive archive(ss);
	archive(args...);
	return std::move(ss);
};

auto user_deserializer = [](std::stringstream &ss, auto &...args) {
	/* here inside, user can implement his/her favourite deserialization method given buffer
	and the arguments pack*/
	cereal::BinaryInputArchive archive(ss);
	archive(args...);
};

class Sort
{
public:
	Sort()
	{
		this->_f = std::bind(&Sort::mergeSort, this, _1, _2);
	}
	~Sort() {}
	void setUnsorted(std::vector<size_t> &unsorted)
	{
		this->unsorted = std::move(unsorted);
	}
	void sort()
	{
		branchHandler.setMaxThreads(6);

		start = std::chrono::steady_clock::now();
		sorted = mergeSort(-1, unsorted);

		end = std::chrono::steady_clock::now();
		elapsed_secs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1.0e-9;
	}

	void printLog()
	{
		std::fstream file;
		std::string path = "ouput";
		std::string name = std::to_string(sorted.size()) + "_log.csv";
		if (!fs::is_directory(path))
		{
			fs::create_directory(path);
		}
		file.open(path + "/" + name, std::ofstream::in | std::ofstream::out | std::ofstream::app);

		file << sorted.size() << ","
			 << elapsed_secs << ","
			 << branchHandler.getIdleTime() << ","
			 << branchHandler.fetchPoolIdleTime() << ","
			 << "\n";
		file.close();
	}

	std::vector<size_t> &fetch()
	{
		return sorted;
	}

	std::vector<size_t> merge(std::vector<size_t> &L, std::vector<size_t> &R)
	{

		std::vector<size_t> merged(L.size() + R.size());

		size_t k = 0;
		size_t i = 0;
		size_t j = 0;
		while (i < L.size() && j < R.size())
		{
			if (L[i] <= R[j])
			{
				merged[k] = L[i];
				i++;
			}
			else
			{
				merged[k] = R[j];
				j++;
			}
			k++;
		}

		while (i < L.size())
		{
			merged[k] = L[i];
			i++;
			k++;
		}

		while (j < R.size())
		{
			merged[k] = R[j];
			j++;
			k++;
		}

		return merged;
	}

	std::vector<size_t> mergeSort(int id, std::vector<size_t> &section)
	{
		if (section.size() <= 1)
			return section;

		//size_t middle = (left + (right - 1)) / 2;
		size_t middle = section.size() / 2;

		size_t Nl = middle;
		size_t Nr = section.size() - middle;

		std::vector<size_t> L(Nl);
		std::vector<size_t> R(Nr);
		std::vector<size_t> merged;

		std::memcpy(L.data(), &section[0], Nl * sizeof(size_t)); //faster than loops
		std::memcpy(R.data(), &section[middle], Nr * sizeof(size_t));

		library::ResultHolder<std::vector<size_t>, std::vector<size_t>> hl(branchHandler);
		//std::this_thread::sleep_for(std::chrono::milliseconds(10));
		hl.holdArgs(L);
#ifndef MPI_TAG
		branchHandler.push(_f, id, hl);
#else
		branchHandler.push(_f, user_serializer, id, hl);
#endif
		//L = mergeSort(id, L);
		//L = _f(id, L);
		R = mergeSort(id, R);
#ifndef MPI_TAG
		hl.get(L);
#else
		hl.get(user_deserializer, L);
#endif
		merged = merge(L, R);

		return merged;
	}

private:
	library::BranchHandler &branchHandler = library::BranchHandler::getInstance();
	std::function<std::vector<size_t>(int, std::vector<size_t> &)> _f;
	std::vector<size_t> unsorted;
	std::vector<size_t> sorted;

	std::chrono::steady_clock::time_point start;
	std::chrono::steady_clock::time_point end;
	double elapsed_secs;
};

#endif
