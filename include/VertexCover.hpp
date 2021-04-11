#ifndef VERTEXCOVER_HPP
#define VERTEXCOVER_HPP

//#include <cereal/archives/binary.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "Graph.hpp"
#include "BranchHandler.hpp"
#include "ResultHolder.hpp"
#include "util.hpp"
#include "fmt/core.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <chrono>
#include <ctime>

using namespace std::placeholders;

namespace fs = std::filesystem;

class VertexCover
{
public:
	VertexCover()
	{
	}

	virtual ~VertexCover() = default;

	void init(Graph &graph, int numThreads, std::string file, int prob)
	{
		this->graph = graph;
		input_file_name = file;
		this->numThreads = numThreads;
		std::string graph_size = std::to_string(graph.size());
		std::string nameout = graph_size + "_out.dat";
		std::string nameout_raw = graph_size + "_out_raw.csv";

		std::string dir = graph_size;
		std::string threads_dir = std::to_string(numThreads);

		this->outPath = "output/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += "prob_" + std::to_string(prob) + "/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += dir + "/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += threads_dir;

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath = outPath + "/";
		this->outPath_raw = outPath;

		this->outPath = outPath + nameout;
		this->outPath_raw = outPath_raw + nameout_raw;

		this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		if (!output.is_open())
		{
			printf("Error, output file not found !");
		}
		output.close();
		output_raw.close();
	}


protected:
	library::BranchHandler &branchHandler = library::BranchHandler::getInstance();

	Graph graph;
	std::chrono::steady_clock::time_point begin;
	std::chrono::steady_clock::time_point end;
	double elapsed_secs;
	size_t preSize = 0;
	std::mutex mtx;


	size_t leaves = 0;
	int currentMVCSize = 0;
	size_t refGlobal = 0;
	size_t foundAtDepth = 0;
	size_t measured_Depth = 0;

	double factor = 0.0; /* k_lower_bound [0.0 - 1.0] k_upper_bound*/
public:
	ofstream output;
	ofstream output_raw;
	std::string outPath;
	std::string outPath_raw;
	int wide = 60;

	std::string input_file_name;
	int numThreads = 0;
};

#endif
