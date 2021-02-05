cmake_minimum_required(VERSION 3.8)

project(mpi_simple_test)

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

add_definitions(-g -Wall)
add_compile_options("-std=c++2a" "-fconcepts" "-Wall" "-Wextra")

#add_compile_options("-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread")
#add_compile_options("-g" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread")

#sets the executable in main folder
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} )

include_directories("cereal" "fmt/include")

set(Include
    include/args_handler.hpp
    include/BranchHandler.hpp
    include/ctpl_stl.hpp
    include/fwPool.hpp
    include/main.h
    include/POOL.hpp
    include/pool_include.hpp
    include/ResultHolder.hpp
    include/Sort.h
)

set(MPI_inc
        MPI_Modules/Scheduler.hpp
        MPI_Modules/serialize/archive.hpp
        MPI_Modules/serialize/iarchive.hpp
        MPI_Modules/serialize/oarchive.hpp        
        MPI_Modules/serialize/stream.hpp        
        )

set(Sources
    src/main.cpp
    src/read_graphs.cpp
)


add_executable(a.out  ${Sources} ${Include}
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/format.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/os.cc)

target_link_libraries(a.out ${CMAKE_THREAD_LIBS_INIT}
                            ${MPI_LIBRARIES}
                            )
#                            ${Boost_LIBRARIES}

if(MPI_COMPILE_FLAGS)
  set_target_properties(a.out PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()
