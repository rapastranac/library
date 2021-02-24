#include "../include/main.h"

#include <iostream>

int main(int argc, char *argv[])
{

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
