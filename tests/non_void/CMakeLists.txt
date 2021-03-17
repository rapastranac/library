cmake_minimum_required(VERSION 3.8)

project(non_void)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads  REQUIRED)


add_definitions(-DDLB=true)
add_definitions(-DVC_NON_VOID)

add_compile_options("-std=c++2a" "-fconcepts" "-O3")

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} )

include_directories("../../include")
#include_directories("../../MPI_Modules")
include_directories("../../cereal" )
include_directories("../../fmt/include" )

file(GLOB Sources "../../src/*.cpp")

add_executable(non_void.out  ${Sources} 
                    ../../fmt/src/format.cc
                    ../../fmt/src/os.cc)

target_link_libraries(non_void.out ${CMAKE_THREAD_LIBS_INIT})
