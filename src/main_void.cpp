#ifdef VC_VOID

#include "../include/main.h"
#include "../include/Graph.hpp"

#include "../include/VC_void.hpp"

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

int main_void(int numThreads, std::string filename)
{

	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VC_void cover;

	graph.readEdges(filename);

	cover.init(graph, numThreads, filename, 4);
	cover.findCover(1);
	cover.printSolution();

	return 0;
}

#endif