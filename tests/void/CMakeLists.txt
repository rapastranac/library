cmake_minimum_required(VERSION 3.8)

project(void)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads  REQUIRED)
find_package(MPI  REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

add_definitions(-DDLB=true)
add_definitions(-DVC_VOID)

add_compile_options("-std=c++2a" "-fconcepts" "-O3")

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} )

include_directories("../../include")
#include_directories("../../MPI_Modules")
include_directories("../../cereal" )
include_directories("../../fmt/include" )

file(GLOB Sources "../../src/*.cpp")

add_executable(void.out  ${Sources} 
                    ../../fmt/src/format.cc
                    ../../fmt/src/os.cc)

target_link_libraries(void.out ${CMAKE_THREAD_LIBS_INIT}
                            ${MPI_LIBRARIES}
                            )

if(MPI_COMPILE_FLAGS)
  set_target_properties(void.out PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()
