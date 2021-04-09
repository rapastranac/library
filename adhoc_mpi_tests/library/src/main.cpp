#include "../include/main.h"
#include "fmt/format.h"

#include "argsparse/argparse.hpp"

#include <iostream>

#include <mpi.h>


/*
int main(int argc, char* argv[])
{
    MPI_Init(&argc, &argv);
 
    // Check that only 2 MPI processes are spawn
    int comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if(comm_size != 2)
    {
        printf("This application is meant to be run with 2 MPI processes, not %d.\n", comm_size);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
 
    // Get my rank
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
 
    // Create the window
    int* window_buffer;
	MPI_Alloc_mem(1 * sizeof(int), MPI_INFO_NULL, &window_buffer);
	
	window_buffer[0] = 0;
	
    MPI_Win window;
    MPI_Win_create(window_buffer, sizeof(int), sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &window);
	
    if(my_rank == 1)
    {
        printf("[MPI process 1] Value in my window_buffer before MPI_Put: %d.\n", window_buffer[0]);
    }

 
    if(my_rank == 0)
    {
        // Push my value into the first integer in MPI process 1 window
        int my_value = 12345;
		MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 1, 0, window);
        MPI_Put(&my_value, 1, MPI_INT, 1, 0, 1, MPI_INT, window);
		MPI_Win_unlock(1, window);
        printf("[MPI process 0] I put data %d in MPI process 1 window via MPI_Put.\n", my_value);
    }
 
    // Wait for the MPI_Put issued to complete before going any further
    //MPI_Win_fence(0, window);
 
    if(my_rank == 1)
    {
		for (int i = 0; i < 1000; i++)
		{
		MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 1, 0, window);
        printf("[MPI process 1] Value in my window_buffer after MPI_Put: %d.\n", window_buffer[0]);
		MPI_Win_unlock(1, window);
		}
    }
 
    // Destroy the window
    MPI_Win_free(&window);
	MPI_Free_mem(window_buffer);
 
    MPI_Finalize();
 
    return EXIT_SUCCESS;
}
*/







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
#endif
}
