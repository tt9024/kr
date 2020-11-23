
add_executable(time_test time_test.cpp)
target_link_libraries(time_test PRIVATE rt pthread)

add_executable(csv_test  csv_test.cpp)
target_link_libraries(csv_test PRIVATE rt pthread)

add_executable(qtest qtest_shm.cpp)
target_include_directories(qtest PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qtest PRIVATE rt pthread)

