#include_directories(${GTEST_SOURCE_DIR}/include ${GTEST_SOURCE_DIR})

# Google Test
find_package(GTest REQUIRED)


#install(FILES res/vocab.tdx DESTINATION ${CMAKE_CURRENT_BINARY_DIR} CONFIGURATIONS Debug Release)
#configure_file(res/vocab.tdx COPYONLY)

set(RESOURCES res/vocab.tdx res/corpus.mct res/align.mam res/index.sfa res/corpus2.txt)
set(TEST_SOURCES VocabTests.cpp CorpusTests.cpp TokenIndexTests.cpp BenchmarkTests.cpp RBTreeIteratorTests.cpp DocumentMapTests.cpp BitextTests.cpp BitextBenchmarkTests.cpp)

set(OTHER_SOURCES fisheryates.h filesystem.h)

file(COPY ${RESOURCES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/res/)

foreach(testSrc ${TEST_SOURCES})
    get_filename_component(testName ${testSrc} NAME_WE)
    add_executable(run${testName} ${testSrc} ${OTHER_SOURCES} $<TARGET_OBJECTS:sto>)
    target_link_libraries(run${testName}  ${Boost_LIBRARIES}  gtest gtest_main  tcmalloc profiler  ${ROCKSDB_LIBRARIES}  pthread)
endforeach(testSrc)
