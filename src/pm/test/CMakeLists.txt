
# er_test
add_executable(er_test er_test.cpp)
target_link_libraries(er_test PRIVATE floorlib)

# idp_test
add_executable(idp_test idp_test.cpp)
target_link_libraries(idp_test PRIVATE floorlib)

# idp_test
add_executable(pm_test pm_test.cpp)
target_link_libraries(pm_test PRIVATE floorlib)

