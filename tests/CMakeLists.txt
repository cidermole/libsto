#include_directories(${GTEST_SOURCE_DIR}/include ${GTEST_SOURCE_DIR})

add_executable(runVocabTests VocabTests.cpp $<TARGET_OBJECTS:src_objs>)

target_link_libraries(runVocabTests gtest gtest_main)