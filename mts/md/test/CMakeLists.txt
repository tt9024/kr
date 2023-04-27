
# bar_writer
add_executable(bar_writer bar_writer.cpp)
target_link_libraries(bar_writer PRIVATE plcc rt pthread)

# snap_pub
add_executable(test_pub_l1 l1_pub.cpp)
target_link_libraries(test_pub_l1 PRIVATE plcc rt pthread)

# bar_reader
add_executable(br_test br_test.cpp)
target_link_libraries(br_test PRIVATE plcc gtest gtest_main rt pthread)
set_target_properties(br_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME br_test COMMAND 
    $<TARGET_FILE:br_test>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

configure_file(bar_test.csv "${CMAKE_BINARY_DIR}/tests/bar_test.csv")
