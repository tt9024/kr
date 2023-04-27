include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# basic FloorCPR functions
add_library(floorlib STATIC
    ExecutionReport.cpp
    PositionData.cpp
    PositionManager.cpp
    RiskMonitor.cpp
    )

target_link_libraries(floorlib PUBLIC plcc rt pthread)
target_include_directories(floorlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(floorlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../md)
target_include_directories(floorlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../util)

# floor manager
add_executable(floor FloorManagerMain.cpp
                     FloorManager.cpp
    )
target_link_libraries(floor PRIVATE floorlib)

# floor trader
# add akk execution traders here
add_executable(ftrader FloorTraderMain.cpp 
                       FloorTrader.cpp
                       TWAPTrader.cpp
    )
target_link_libraries(ftrader PRIVATE floorlib)

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

