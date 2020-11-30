
# bar_reader
add_executable(bar_writer bar_writer.cpp)
target_link_libraries(bar_writer PRIVATE rt pthread)

# snap_pub
add_executable(test_pub_l1 l1_pub.cpp)
target_link_libraries(test_pub_l1 PRIVATE rt pthread)

