include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(plcc STATIC
    time_util.cpp
    symbol_map.cpp
    rate_limiter.cpp
    rate_estimator.cpp
    plcc/ConfigureReader.cpp
    plcc/PLCC.cpp
    )

target_link_libraries(plcc PUBLIC)
target_include_directories(plcc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(plcc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plcc)

add_executable (cfgupd plcc/ConfigUtil.cpp)
target_link_libraries(cfgupd plcc rt)

add_executable (json_upd plcc/JsonUtil.cpp)
target_link_libraries(json_upd plcc rt)

add_subdirectory(test)

