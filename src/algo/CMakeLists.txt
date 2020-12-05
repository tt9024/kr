# AlgoBase
add_library(algolib STATIC
    AlgoBase.cpp
    AlgoThread.cpp
    )

target_link_libraries(algolib PUBLIC rt pthread)
target_include_directories(algolib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../util
    ${CMAKE_CURRENT_SOURCE_DIR}/../md
    ${CMAKE_CURRENT_SOURCE_DIR}/../pm
    )

# All strategies as shared libraries
add_library(ar1 STATIC
    strat/AR1.cpp
    )

target_link_libraries(ar1 PUBLIC algolib)
target_include_directories(ar1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/strat
    )

add_executable(strat_run 
    ThreadMain.cpp
    )
target_link_libraries(strat_run PUBLIC rt pthread algolib ar1)

