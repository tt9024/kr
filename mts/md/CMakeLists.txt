include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../util)

# bar_reader
add_executable(booktap snap_reader.cpp)
target_link_libraries(booktap PRIVATE plcc rt pthread)

# bar_reader
add_executable(bar_reader bar_reader.cpp)
target_link_libraries(bar_reader PRIVATE plcc rt pthread)

## test
add_subdirectory(test)

