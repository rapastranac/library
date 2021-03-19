cmake_minimum_required(VERSION 3.8)

project(global)

# testing scenarios
#add_subdirectory(tests/void)
#add_subdirectory(tests/non_void)
#add_subdirectory(tests/void_MPI)
#add_subdirectory(tests/non_void_MPI)


set(CMAKE_CXX_STANDARD 20)

#Set(CMAKE_EXE_LINKER_FLAGS  "-lboost_system -lboost_serialization -lboost_mpi -lboost_fiber")
#Set(CMAKE_EXE_LINKER_FLAGS  "-lboost_fiber")

find_package(Threads  REQUIRED)
find_package(MPI  REQUIRED)
#find_package(Boost  COMPONENTS  mpi serialization fiber REQUIRED)
#find_package(Boost  COMPONENTS fiber REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

#link_directories(${Boost_LIBRARY_DIRS})
#include_directories(${Boost_INCLUDE_DIRS})

#add_definitions(-DDLB=true)
add_definitions(-DMPI_ENABLED)
add_definitions(-DDEBUG_COMMENTS)
#add_definitions(-DVC_VOID)
#add_definitions(-DVC_FPT_VOID)
add_definitions(-DVC_VOID_MPI)
#add_definitions(-DVC_NON_VOID)
#add_definitions(-DVC_NON_VOID_MPI)

#add_compile_options("-std=c++2a" "-fconcepts" "-O3")

#Here below for boost
#add_compile_options("-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts")
add_compile_options("-g" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts")

#sets the executable in main folder
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} )

include_directories("include")
include_directories("MPI_Modules")
include_directories("cereal" )
include_directories("fmt/include" )

file(GLOB Sources "src/*.cpp")

add_executable(a.out  ${Sources} 
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/format.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/os.cc)

target_link_libraries(a.out ${CMAKE_THREAD_LIBS_INIT}
                            ${MPI_LIBRARIES}
                            )
#                            ${Boost_LIBRARIES}

if(MPI_COMPILE_FLAGS)
  set_target_properties(a.out PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()



enable_testing()
add_test(void                       ../tests/void/void.out                  -N 5 -I ../input/prob_4/400/00400_1)
add_test(non_void                   ../tests/non_void/non_void.out          -N 5 -I ../input/prob_4/400/00400_1)
add_test(void_mpi       mpirun -n 5 ../tests/void_MPI/void_mpi.out          -N 1 -I ../input/prob_4/400/00400_1)
add_test(non_void_mpi   mpirun -n 5 ../tests/non_void_MPI/non_void_mpi.out  -N 1 -I ../input/prob_4/400/00400_1)
