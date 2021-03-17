# CMake generated Testfile for 
# Source directory: /home/andres/Documents/github/library
# Build directory: /home/andres/Documents/github/library/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(myTest "mpirun" "-n" "4" "a.out" "-N" "1" "-I" "input/prob_4/400/00400_1")
set_tests_properties(myTest PROPERTIES  _BACKTRACE_TRIPLES "/home/andres/Documents/github/library/CMakeLists.txt;60;add_test;/home/andres/Documents/github/library/CMakeLists.txt;0;")
