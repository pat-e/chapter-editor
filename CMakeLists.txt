# Require a reasonably modern version of CMake
cmake_minimum_required(VERSION 3.10)

# Name the project
project(MKVChapterEditor CXX)

# Set the C++ standard to C++17 (needed for filesystem features if utilized)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find libmpv dynamically
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)

# Define the executable
add_executable(chapter-editor src/main.cpp)

# Link libmpv dynamically
target_include_directories(chapter-editor PRIVATE ${MPV_INCLUDE_DIRS})
target_link_libraries(chapter-editor PRIVATE ${MPV_LIBRARIES})