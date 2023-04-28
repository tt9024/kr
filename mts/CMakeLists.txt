cmake_minimum_required(VERSION 3.10)

project(KISCO VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-Wall -g")

add_definitions(-DENV="DEV")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

enable_testing()

add_subdirectory(util)
add_subdirectory(md)
add_subdirectory(pm)
#add_subdirectory(tp)
#add_subdirectory(algo)
