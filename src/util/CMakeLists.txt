include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(utils STATIC
    plcc/ConfigureReader.cpp
    )

target_link_libraries(utils PUBLIC rt pthread)
target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

## TEST

add_subdirectory(test)
