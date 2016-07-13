cmake_minimum_required(VERSION 3.4)
project(sto)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

include_directories(src)

add_subdirectory(src)
add_subdirectory(tests)
