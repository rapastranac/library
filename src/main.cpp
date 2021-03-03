#include "../include/main.h"
#include "fmt/format.h"

#include <iostream>

//#include <mpi.h>
int main(int argc, char *argv[])
{
	int numThreads = 5;
	//auto filename = "input/prob_4/400/00400_1";
	//auto filename = "input/prob_4/600/00600_1";
	auto filename = "/home/andres/Downloads/vc_graphs/graphs_200_0.1/10000_200/prob_0.1/42";

	if (argc > 1)
	{
		numThreads = std::stoi(argv[1]);
		filename = argv[2];
	}
	fmt::print("argc: {}, threads: {}, filename: {} \n", argc, numThreads, filename);
	//return 0;

	//MPI_Init(NULL, NULL);
	//int size;
	//MPI_Type_size(MPI::BOOL, &size);
	//std::cout << "sizeof(MPI::BOOL) : " << size << std::endl;
	//MPI_Finalize();
	//return 0;

#ifdef VC_VOID
	return main_void(numThreads, filename);
#elif VC_VOID_MPI
	return main_void_MPI(numThreads, filename);
#elif VC_NON_VOID
	return main_non_void(numThreads, filename);
#elif VC_NON_VOID_MPI
	return main_non_void_MPI(numThreads, filename);
#endif
}
