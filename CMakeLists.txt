cmake_minimum_required(VERSION 3.4)
project(sto)

# for FindRocksDB.cmake
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")

# Boost
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost REQUIRED COMPONENTS
        system
        #thread
        filesystem
        #unit_test_framework
        program_options
        #iostreams
        )


# TODO proper path, e.g. into <MMT>/vendor/
set(ROCKSDB_HOME "/home/david/tmp/rocksdb")
find_package(RocksDB REQUIRED)

include_directories(${ROCKSDB_INCLUDE_DIR})
#target_link_libraries( ${ROCKSDB_LIBRARIES})


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

include_directories(src)

add_subdirectory(src)
add_subdirectory(tests)


# tools:

set(TOOL_SOURCES
        src/tools/mtt-build.cpp
        src/tools/symal2mam.cpp
        src/tools/mmlex-build.cpp
        )

foreach(toolSrc ${TOOL_SOURCES})
    get_filename_component(toolName ${toolSrc} NAME_WE)
    add_executable(${toolName} ${toolSrc} $<TARGET_OBJECTS:sto>)
    target_link_libraries(${toolName}  ${Boost_LIBRARIES}  tcmalloc profiler  ${ROCKSDB_LIBRARIES})
endforeach(toolSrc)
