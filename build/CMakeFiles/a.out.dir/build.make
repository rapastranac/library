# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/andres/Documents/github/library

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/andres/Documents/github/library/build

# Include any dependencies generated for this target.
include CMakeFiles/a.out.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/a.out.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/a.out.dir/flags.make

CMakeFiles/a.out.dir/src/main.cpp.o: CMakeFiles/a.out.dir/flags.make
CMakeFiles/a.out.dir/src/main.cpp.o: ../src/main.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/andres/Documents/github/library/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/a.out.dir/src/main.cpp.o"
	/usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/a.out.dir/src/main.cpp.o -c /home/andres/Documents/github/library/src/main.cpp

CMakeFiles/a.out.dir/src/main.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/a.out.dir/src/main.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/andres/Documents/github/library/src/main.cpp > CMakeFiles/a.out.dir/src/main.cpp.i

CMakeFiles/a.out.dir/src/main.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/a.out.dir/src/main.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/andres/Documents/github/library/src/main.cpp -o CMakeFiles/a.out.dir/src/main.cpp.s

CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o: CMakeFiles/a.out.dir/flags.make
CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o: ../src/buildUnsorted.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/andres/Documents/github/library/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o"
	/usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o -c /home/andres/Documents/github/library/src/buildUnsorted.cpp

CMakeFiles/a.out.dir/src/buildUnsorted.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/a.out.dir/src/buildUnsorted.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/andres/Documents/github/library/src/buildUnsorted.cpp > CMakeFiles/a.out.dir/src/buildUnsorted.cpp.i

CMakeFiles/a.out.dir/src/buildUnsorted.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/a.out.dir/src/buildUnsorted.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/andres/Documents/github/library/src/buildUnsorted.cpp -o CMakeFiles/a.out.dir/src/buildUnsorted.cpp.s

# Object files for target a.out
a_out_OBJECTS = \
"CMakeFiles/a.out.dir/src/main.cpp.o" \
"CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o"

# External object files for target a.out
a_out_EXTERNAL_OBJECTS =

../a.out: CMakeFiles/a.out.dir/src/main.cpp.o
../a.out: CMakeFiles/a.out.dir/src/buildUnsorted.cpp.o
../a.out: CMakeFiles/a.out.dir/build.make
../a.out: /usr/lib/x86_64-linux-gnu/openmpi/lib/libmpi_cxx.so
../a.out: /usr/lib/x86_64-linux-gnu/openmpi/lib/libmpi.so
../a.out: CMakeFiles/a.out.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/andres/Documents/github/library/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking CXX executable ../a.out"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/a.out.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/a.out.dir/build: ../a.out

.PHONY : CMakeFiles/a.out.dir/build

CMakeFiles/a.out.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/a.out.dir/cmake_clean.cmake
.PHONY : CMakeFiles/a.out.dir/clean

CMakeFiles/a.out.dir/depend:
	cd /home/andres/Documents/github/library/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/andres/Documents/github/library /home/andres/Documents/github/library /home/andres/Documents/github/library/build /home/andres/Documents/github/library/build /home/andres/Documents/github/library/build/CMakeFiles/a.out.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/a.out.dir/depend

