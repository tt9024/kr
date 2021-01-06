include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(floorlib STATIC
    ExecutionReport.cpp
    PositionData.cpp
    PositionManager.cpp
    FloorManager.cpp
    )

target_link_libraries(floorlib PUBLIC utils rt pthread)
target_include_directories(floorlib PUBLIC ${CMAKE_SOURCE_DIR}/util)

# floor
add_executable(floor FloorManagerMain.cpp)
target_link_libraries(floor PRIVATE floorlib)

# flr
add_executable(flr flr.cpp)
target_link_libraries(flr PRIVATE floorlib)

# floortap
add_executable(ftap floortap.cpp)
target_link_libraries(ftap PRIVATE floorlib)

# floorpoke
add_executable(fpoke floorpoke.cpp)
target_link_libraries(fpoke PRIVATE floorlib)

# exch_mock
add_executable(exch_mock exch_mock.cpp)
target_link_libraries(exch_mock PRIVATE floorlib)

## TESTS
add_subdirectory(test)

