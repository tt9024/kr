cmake_minimum_required(VERSION 3.13)

project(MTS VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-Wall -g")
add_definitions(-DLINUX_COMPILATION)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/cpp/C*.cpp)
add_library(tp ${SRC_FILES})

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED  ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package( Boost 1.60.0 REQUIRED COMPONENTS 
    chrono
    system
    date_time
    thread
    regex
    filesystem
    atomic
    iostreams
    serialization
    )

if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(tp ${Boost_LIBRARIES})
endif()

find_package(Python3 COMPONENTS 
    Interpreter 
    Development
    # Compiler
    #NumPy
    )

if (Python3_FOUND)
    include_directories(${Python3_INCLUDE_DIRS})
    target_link_libraries(tp ${Python3_LIBRARIES})
endif()

find_library(quickfix_location NAMES libquickfix.so)
message(STATUS ${quickfix_location})
add_library(quickfix SHARED IMPORTED)
set_target_properties(quickfix PROPERTIES IMPORTED_LOCATION ${quickfix_location})

target_link_libraries(tp quickfix)
target_link_libraries(tp floorlib)

# the tpmain executable
add_executable(tpmain ${CMAKE_CURRENT_SOURCE_DIR}/cpp/main.cpp)
target_link_libraries(tpmain tp)

# the recovery executable
#add_executable(mts_recover ${CMAKE_CURRENT_SOURCE_DIR}/cpp/mts_dc.cpp)
#target_link_libraries(mts_recover tp)

#target_link_libraries(tpmain 
#    tp
#    quickfix
#    ${Boost_LIBRARIES}
#    ${Python3_LIBRARIES}
#    )

add_subdirectory(bpipe)

