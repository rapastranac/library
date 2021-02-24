#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <vector>

std::vector<std::string> read_graphs(std::string graphSize);

#ifdef VC_VOID
int main_void(int argc, char *argv[]);
#elif VC_VOID_MPI
int main_void_MPI(int argc, char *argv[]);
#elif VC_NON_VOID
int main_non_void(int argc, char *argv[]);
#elif VC_NON_VOID_MPI
int main_non_void_MPI(int argc, char *argv[]);
#endif

#endif
