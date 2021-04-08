# CMake generated Testfile for 
# Source directory: /home/andres/cloud/library
# Build directory: /home/andres/cloud/library/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(void "../tests/void/void.out" "-N" "5" "-I" "../input/prob_4/400/00400_1")
set_tests_properties(void PROPERTIES  _BACKTRACE_TRIPLES "/home/andres/cloud/library/CMakeLists.txt;74;add_test;/home/andres/cloud/library/CMakeLists.txt;0;")
add_test(non_void "../tests/non_void/non_void.out" "-N" "5" "-I" "../input/prob_4/400/00400_1")
set_tests_properties(non_void PROPERTIES  _BACKTRACE_TRIPLES "/home/andres/cloud/library/CMakeLists.txt;75;add_test;/home/andres/cloud/library/CMakeLists.txt;0;")
add_test(void_mpi "mpirun" "-n" "6" "../tests/void_MPI/void_mpi.out" "-N" "1" "-I" "../input/prob_4/400/00400_1")
set_tests_properties(void_mpi PROPERTIES  _BACKTRACE_TRIPLES "/home/andres/cloud/library/CMakeLists.txt;76;add_test;/home/andres/cloud/library/CMakeLists.txt;0;")
add_test(non_void_mpi "mpirun" "-n" "6" "../tests/non_void_MPI/non_void_mpi.out" "-N" "1" "-I" "../input/prob_4/400/00400_1")
set_tests_properties(non_void_mpi PROPERTIES  _BACKTRACE_TRIPLES "/home/andres/cloud/library/CMakeLists.txt;77;add_test;/home/andres/cloud/library/CMakeLists.txt;0;")
