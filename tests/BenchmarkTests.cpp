/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cassert>
#include <memory>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <map>
#include <gtest/gtest.h>

#include "Vocab.h"
#include "Corpus.h"
#include "TokenIndex.h"
#include "Types.h"
#include "util/Time.h"
#include "util/usage.h"

using namespace sto;

/**
 * Test Fixture.
 */
struct BenchmarkTests : testing::Test {
  void create_random_queries(TokenIndex<SrcToken> &tokenIndex, std::vector<std::vector<SrcToken>> &queries, size_t num = 100000);
};

/**
 * Loop over sentences, one in each line of the file, tokenize at ' ' and call process(words)
 */
template<typename ProcessLine>
void ProcessTextFileLines(std::istream &is, ProcessLine process, size_t nlines = (size_t) -1) {
  std::string line;

  for(size_t iline = 0; iline < nlines && std::getline(is, line); iline++) {
    std::vector<std::string> words;

    size_t start_word = 0, i;
    for(i = 0; i < line.size(); i++) {
      if(line[i] == ' ') {
        words.push_back(std::string(line.data() + start_word, line.data() + i));
        start_word = i + 1;
      }
    }
    words.push_back(std::string(line.data() + start_word, line.data() + i));
    process(words); // [](std::vector<std::string> &words) {}
  }
}

/**
 * Read 'nlines' of 'textFileName' into Corpus and Vocab.
 */
void ReadTextFile(Corpus<SrcToken> &corpus, Vocab<SrcToken> &vocab, std::string textFileName, size_t nlines = (size_t) -1) {
  std::ifstream ifs(textFileName.c_str());

  assert(ifs.good());

  ProcessTextFileLines(ifs, [&corpus, &vocab](std::vector<std::string> &words) {
    assert(words.size() <= static_cast<size_t>(static_cast<Corpus<SrcToken>::Offset>(-1))); // ensure sentence length fits
    std::vector<SrcToken> sent;
    for(auto w : words)
      sent.push_back(vocab[w]);
    corpus.AddSentence(sent);
  }, nlines);
}

std::string kTextFile = "/home/david/mmt/data/training/real/ep/train.clean.en";

TEST_F(BenchmarkTests, asdf) {
  // ~/mmt/data/training/real/ep/train.clean.en has 750k lines.
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(vocab);
  ReadTextFile(corpus, vocab, kTextFile, /* nlines = */ 1);
  std::cerr << "[size=" << corpus.sentence(0).size() << "] " << corpus.sentence(0).surface() << std::endl;
}

void BenchmarkTests::create_random_queries(TokenIndex<SrcToken> &tokenIndex, std::vector<std::vector<SrcToken>> &queries, size_t num) {
  std::random_device rd;
  std::mt19937 gen(rd());
  IndexSpan<SrcToken> span = tokenIndex.span();
  std::uniform_int_distribution<size_t> dist(0, span.size()-1); // spans all corpus positions

  for(size_t i = 0; i < num; i++) {
    // pick corpus positions at random
    Position<SrcToken> pos = span[dist(gen)];
    Sentence<SrcToken> sent = tokenIndex.corpus()->sentence(pos.sid);

    std::uniform_int_distribution<size_t> len_dist(1, std::min<size_t>(5, sent.size() - static_cast<size_t>(pos.offset)));
    size_t len = len_dist(gen);
    std::vector<SrcToken> query;
    for(size_t j = 0; j < len; j++)
      query.push_back(pos.add(j, *tokenIndex.corpus()).token(*tokenIndex.corpus()));

    queries.push_back(query);
  }
}

TEST_F(BenchmarkTests, index_10k) {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(vocab);

  std::string textFile = kTextFile;
  const size_t nlines = 10000;

  /*
   * $ wc /tmp/ep.10k
   * 10000  278881 1543417 /tmp/ep.10k
   *
   * $ wc /tmp/ep.100k
   * 100000  2795109 15470337 /tmp/ep.100k
   *
   * 100 k lines, 2.8 M tokens
   */
  static_assert(sizeof(SrcToken) == 4); // -> ~ 1 MB Corpus track (ep.10k)

  // Sid (4), Offset (1) is padded up in the struct.
  static_assert(sizeof(Position<SrcToken>) == 8); // -> ~2 MB TokenIndex, if purely SA based (ep.10k)

  util::PrintUsage(std::cerr);

  benchmark_time([&corpus, &vocab, &textFile, &nlines](){
    ReadTextFile(corpus, vocab, textFile, nlines);
  }, "read");
  util::PrintUsage(std::cerr);

  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 100000); // will lead to single split with ep.10k

  // TODO: </s> sentinels
  benchmark_time([&corpus, &tokenIndex](){
    for(size_t i = 0; i < corpus.size(); i++) {
      if(i % 1000 == 0)
        std::cerr << "tokenIndex @ AddSentence(i=" << i << ")..." << std::endl;
      tokenIndex.AddSentence(corpus.sentence(i));
    }
  }, "build_index");
  util::PrintUsage(std::cerr);

  std::vector<std::vector<SrcToken>> queries;
  create_random_queries(tokenIndex, queries, /* num = */ 1000000);

  benchmark_time([&corpus, &tokenIndex, &queries](){
    for(auto query : queries) {
      IndexSpan<SrcToken> span = tokenIndex.span();
      for(auto token : query)
        EXPECT_GT(span.narrow(token), 0) << "queries for existing locations must succeed"; // since we just randomly sampled them, they must be in the corpus.
    }
  }, "query_index");
}

TEST_F(BenchmarkTests, map_hack) {
  std::map<int, int> m;
  m[1] = 1;
  auto it = m.find(1);
  const_cast<int&>(it->first) = 2;
  EXPECT_EQ(1, m[2]);
}
