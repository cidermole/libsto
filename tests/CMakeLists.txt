#include_directories(${GTEST_SOURCE_DIR}/include ${GTEST_SOURCE_DIR})

add_executable(runVocabTests VocabTests.cpp $<TARGET_OBJECTS:src_objs>)

#install(FILES res/vocab.tdx DESTINATION ${CMAKE_CURRENT_BINARY_DIR} CONFIGURATIONS Debug Release)
#configure_file(res/vocab.tdx COPYONLY)
file(COPY res/vocab.tdx DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/res/)

target_link_libraries(runVocabTests gtest gtest_main)