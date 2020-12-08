
add_executable(time_test time_test.cpp)
target_link_libraries(time_test PRIVATE gtest gtest_main rt pthread)
set_target_properties(time_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME time_test COMMAND 
    $<TARGET_FILE:time_test>
)


add_executable(csv_test  csv_test.cpp)
target_link_libraries(csv_test PRIVATE gtest gtest_main rt pthread)
set_target_properties(csv_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME csv_test COMMAND 
    $<TARGET_FILE:csv_test>
)

add_executable(rate_test  rate_test.cpp)
target_link_libraries(rate_test PRIVATE gtest gtest_main rt pthread)
set_target_properties(rate_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME rate_test COMMAND 
    $<TARGET_FILE:rate_test>
)

# qtest is a manual test
add_executable(qtest qtest_shm.cpp)
target_include_directories(qtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qtest PRIVATE rt pthread)

