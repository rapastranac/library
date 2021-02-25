#include "../include/main.h"

#include <iostream>
//#include <mpi.h>
int main(int argc, char *argv[])
{

	//MPI_Init(NULL, NULL);
	//int size;
	//MPI_Type_size(MPI::BOOL, &size);
	//std::cout << "sizeof(MPI::BOOL) : " << size << std::endl;
	//MPI_Finalize();
	//return 0;

#ifdef VC_VOID
	return main_void(argc, argv);
#elif VC_VOID_MPI
	return main_void_MPI(argc, argv);
#elif VC_NON_VOID
	return main_non_void(argc, argv);
#elif VC_NON_VOID_MPI
	return main_non_void_MPI(argc, argv);
#endif
}
