cmake_minimum_required(VERSION 3.4)
project(sto)

# for FindRocksDB.cmake
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")

# TODO proper path, e.g. into <MMT>/vendor/
set(ROCKSDB_HOME "/home/david/tmp/rocksdb")
find_package(RocksDB REQUIRED)

include_directories(${ROCKSDB_INCLUDE_DIR})
#target_link_libraries( ${ROCKSDB_LIBRARIES})


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

include_directories(src)

add_subdirectory(src)
add_subdirectory(tests)
