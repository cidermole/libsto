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

  // ensure </s> exists in vocab, get its vid
  SrcToken eos = vocab["</s>"];

  ProcessTextFileLines(ifs, [&corpus, &vocab, eos](std::vector<std::string> &words) {
    assert(words.size() <= static_cast<size_t>(static_cast<Corpus<SrcToken>::Offset>(-1))); // ensure sentence length fits
    std::vector<SrcToken> sent;
    for(auto w : words)
      sent.push_back(vocab[w]);
    sent.push_back(eos);
    corpus.AddSentence(sent);
  }, nlines);
}

std::string kTextFile = "/home/david/mmt/data/training/real/ep/train.clean.en";

TEST_F(BenchmarkTests, asdf) {
  // ~/mmt/data/training/real/ep/train.clean.en has 750k lines.
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);
  ReadTextFile(corpus, vocab, kTextFile, /* nlines = */ 1);
  std::cerr << "[size=" << corpus.sentence(0).size() << "] " << corpus.sentence(0).surface() << std::endl;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> len_dist(1,0);
  size_t i = len_dist(gen);
  std::cerr << "len_dist(1,0) sampled = " << i << std::endl;
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

    assert(sent.size() > 1); // because no empty sents, and </s>
    assert(pos.offset < sent.size());
    size_t max_len = std::min<size_t>(5, sent.size() - static_cast<size_t>(pos.offset) - 1); // -1: never query </s> by itself
    if(max_len == 0) {
      // sample another position/sentence instead (never query </s> by itself)
      i--;
      continue;
    }

    std::uniform_int_distribution<size_t> len_dist(1, max_len);
    size_t len = len_dist(gen);
    std::vector<SrcToken> query;
    for(size_t j = 0; j < len; j++)
      query.push_back(pos.add(j, *tokenIndex.corpus()).token(*tokenIndex.corpus()));

    queries.push_back(query);
  }
}

TEST_F(BenchmarkTests, index_100k) {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);

  std::string textFile = kTextFile;
  const size_t nlines = 100000;

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

  ///////////////////////////

  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 10000);

  benchmark_time([&corpus, &tokenIndex](){
    for(size_t i = 0; i < corpus.size(); i++) {
      if(i % 1000 == 0)
        std::cerr << "tokenIndex @ AddSentence(i=" << i << ")..." << std::endl;
      tokenIndex.AddSentence(corpus.sentence(i));
    }
  }, "build_index");
  util::PrintUsage(std::cerr);

  ///////////////////////////

  std::vector<std::vector<SrcToken>> queries;
  create_random_queries(tokenIndex, queries, /* num = */ 100000);
  size_t sample = 1000;

  benchmark_time([&corpus, &tokenIndex, &queries, sample](){
    std::random_device rd;
    std::mt19937 gen(rd());
    size_t dummy = 0, nsamples_total = 0;

    for(auto query : queries) {
      IndexSpan<SrcToken> span = tokenIndex.span();
      for(auto token : query)
        EXPECT_GT(span.narrow(token), 0) << "queries for existing locations must succeed"; // since we just randomly sampled them, they must be in the corpus.

      // at each query span, sample multiple occurrences from random locations
      std::uniform_int_distribution<size_t> sample_dist(0, span.size()-1);
      size_t nsamples = std::min(sample, span.size());
      for(size_t i = 0; i < nsamples; i++)
        dummy += span[sample_dist(gen)].offset;
      nsamples_total += nsamples;
    }

    std::cerr << "nsamples_total = " << nsamples_total << " dummy = " << dummy << std::endl;
  }, "query_index");
}


TEST_F(BenchmarkTests, nosplit_eos) {
  // verify that </s> does not get split even if we add more sentences than maxLeafSize.
  // TokenIndex would fail with an assertion if the split should happen.

  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);

  std::string textFile = kTextFile;
  const size_t nlines = 1000;

  ReadTextFile(corpus, vocab, textFile, nlines);

  ///////////////////////////

  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 100);

  for(size_t i = 0; i < corpus.size(); i++) {
    tokenIndex.AddSentence(corpus.sentence(i));
  }
}
