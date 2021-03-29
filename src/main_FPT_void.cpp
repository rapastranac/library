#ifdef VC_FPT_VOID

#include "../include/main.h"
#include "../include/Graph.hpp"

#include "../include/VC_FPT_void.hpp"

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

int main_FPT_void(int numThreads, int prob, std::string filename)
{

    auto &handler = library::BranchHandler::getInstance(); // parallel library

    Graph graph;
    Graph oGraph;
    VC_FPT_void cover;

    graph.readEdges(filename);
    //graph.readDimacs(filename);

    cover.init(graph, numThreads, filename, prob);
    cover.findCover(1);
    //cover.printSolution();

    return 0;
}

#endif