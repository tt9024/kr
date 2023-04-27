include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../util)

# booktap
add_executable(booktap snap_reader.cpp)
target_link_libraries(booktap PRIVATE plcc rt pthread)

# bar_reader
add_executable(bar_reader bar_reader.cpp)
target_link_libraries(bar_reader PRIVATE plcc rt pthread)

# td_parser
add_executable(td_parser td_parser.cpp)
target_link_libraries(td_parser PRIVATE plcc rt pthread)

## test
add_subdirectory(test)

