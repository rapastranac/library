#ifdef VC_NON_VOID

#include "../include/main.h"
#include "../include/Graph.hpp"

#include "../include/VC_non_void.hpp"

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

int main_non_void(int argc, char *argv[])
{
	auto &handler = library::BranchHandler::getInstance(); // parallel library

	Graph graph;
	Graph oGraph;
	VC_non_void cover;

	auto file = "input/prob_4/400/00400_1";
	graph.readEdges(file);

	cover.init(graph, 6, file, 4);
	cover.findCover(1);
	cover.printSolution();

	return 0;
}

#endif