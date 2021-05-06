#include "../include/main.h"
#include "fmt/format.h"

#include "argsparse/argparse.hpp"

#include <iostream>

//#include <mpi.h>
int main(int argc, char *argv[])
{
	argparse::ArgumentParser program("main");
	program.add_argument("-N", "--nThreads")
		.help("Number of threads")
		.nargs(1)
		.default_value(int{1})
		.action([](const std::string &value) { return std::stoi(value); });

	program.add_argument("-P", "--prob")
		.help("Density probability of input graph")
		.nargs(1)
		.default_value(int{4})
		.action([](const std::string &value) { return std::stoi(value); });

	program.add_argument("-I", "--indir")
		.help("Input directory of the graph to be read")
		.nargs(1)
		.default_value(std::string{"input/prob_4/400/00400_1"})
		.action([](const std::string &value) { return value; });
	try
	{
		program.parse_args(argc, argv);
	}
	catch (const std::runtime_error &err)
	{
		std::cout << err.what() << std::endl;
		std::cout << program;
		exit(0);
	};

	int numThreads = program.get<int>("--nThreads");
	int prob = program.get<int>("--prob");
	auto filename = program.get<std::string>("--indir");

	//int numThreads = 1;
	//int prob = 4;
	//auto filename = "input/prob_4/400/00400_1";
	//auto filename = "input/prob_4/600/00600_1";
	//auto filename = "/home/andres/Downloads/vc_graphs/graphs_500_0.1/10000_500/prob_0.1/0";
	//filename = "input/p_hat1000-2";
	//filename = "input/prob_4/100/00100_1";

	//if (argc > 1)
	//{
	//	numThreads = std::stoi(argv[1]);
	//	prob = std::stoi(argv[2]);
	//	filename = argv[3];
	//}
	fmt::print("argc: {}, threads: {}, prob : {}, filename: {} \n", argc, numThreads, prob, filename);
	//return 0;

	//MPI_Init(NULL, NULL);
	//int size;
	//MPI_Type_size(MPI::BOOL, &size);
	//std::cout << "sizeof(MPI::BOOL) : " << size << std::endl;
	//MPI_Finalize();
	//return 0;

	
#ifdef VC_VOID
	return main_void(numThreads, prob, filename);
#elif VC_FPT_VOID
	return main_FPT_void(numThreads, prob, filename);
#elif VC_VOID_MPI
	return main_void_MPI(numThreads, prob, filename);
#elif VC_NON_VOID
	return main_non_void(numThreads, prob, filename);
#elif VC_NON_VOID_MPI
	return main_non_void_MPI(numThreads, prob, filename);
#elif BITVECTOR_VC
	return main_void_MPI_bitvec(numThreads, prob, filename);
#endif
}
