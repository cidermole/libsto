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
#include <unordered_set>
#include <gtest/gtest.h>

#include "Vocab.h"
#include "Corpus.h"
#include "TokenIndex.h"
#include "IndexBuffer.h"
#include "Types.h"
#include "util/Time.h"
#include "util/usage.h"

#include "filesystem.h"
#include "DB.h"
#include "ITokenIndex.h"

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
    //sent.push_back(eos); // this is now implicit in Sentence::operator[]
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
  auto span = tokenIndex.span();
  std::uniform_int_distribution<size_t> dist(0, span.size()-1); // spans all corpus positions

  for(size_t i = 0; i < num; i++) {
    // pick corpus positions at random
    Position<SrcToken> pos = span[dist(gen)];
    Sentence<SrcToken> sent = tokenIndex.corpus()->sentence(pos.sid);

    assert(sent.size() > 0); // no empty sents
    assert(pos.offset <= sent.size()); // implicit </s> may be at offset=sent.size()
    size_t max_len = std::min<size_t>(5, sent.size() - static_cast<size_t>(pos.offset)); // note: never query </s> by itself
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

static std::string basePath = "res/BenchmarkTests";

namespace std {
template<>
template<typename Token>
struct hash<Position<Token>> {
  std::size_t operator()(const Position<Token>& pos) const {
    return ((hash<typename Position<Token>::Offset>()(pos.offset))
            ^ (hash<typename Position<Token>::Sid>()(pos.sid) << 8));
  }
};
} // namespace std

TEST_F(BenchmarkTests, dynamic_memory_vs_disk) {
  typedef SrcToken Token;

  Vocab<Token> sv("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<Token> sc("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &sv);
  TokenIndex<Token, IndexTypeMemory> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa",
                                                 sc); // built with mtt-build

  TokenIndex<Token, IndexTypeMemory> dynamicIndexMemory("", sc); // building dynamically

  benchmark_time([&sc, &dynamicIndexMemory]() {
    std::cerr << "building dynamicIndex..." << std::endl;
    // TODO: implementation cannot currently add remainder of batches (currently 1000), see AddSentenceImpl::kBatchSize in TokenIndex.cpp
    for (size_t i = 0; i < sc.size() - sc.size() % 1000; i++) {
      if (i % 1000 == 0)
        std::cerr << "dynamicIndex @ AddSentence(i=" << i << ")..." << std::endl;
      dynamicIndexMemory.AddSentence(sc.sentence(i));
    }
    std::cerr << "building dynamicIndex (memory) done." << std::endl;
  }, "dynamicIndexMemory");

  remove_all(basePath); // ensure no leftovers

  std::shared_ptr<DB<Token>> db(new DB<Token>(basePath));

  TokenIndex<Token, IndexTypeDisk> dynamicIndexDisk(basePath, sc, db); // building dynamically

  benchmark_time([&sc, &dynamicIndexDisk]() {
    std::cerr << "building dynamicIndex..." << std::endl;
    // TODO: remainder of batches
    for (size_t i = 0; i < sc.size() - sc.size() % 1000; i++) {
      if (i % 1000 == 0)
        std::cerr << "dynamicIndex @ AddSentence(i=" << i << ")..." << std::endl;
      dynamicIndexDisk.AddSentence(sc.sentence(i));
    }
    std::cerr << "building dynamicIndex (disk) done." << std::endl;
  }, "dynamicIndexDisk");

  auto staticSpan = dynamicIndexMemory.span();
  auto dynamicSpan = dynamicIndexDisk.span();

  /* // fails... sort stability?
  for(size_t i = 0; i < staticSpan.size(); i++)
    EXPECT_EQ(staticSpan[i], dynamicSpan[i]) << "position i=" << i << " expected to match between disk and memory indexes";
    */


  auto surface = [&sc](Position<Token> pos){
    std::stringstream ss;
    for(size_t j = 0; j + pos.offset <= sc.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << pos.add(j, sc).surface(sc);
    return ss.str();
  };

  // there is no equality position by position (sort stability?)
  // however, for each surface form, there must be equality among their positions

  size_t numPos = staticSpan.size();
  std::unordered_set<Position<Token>> staticBucket, dynamicBucket;
  std::string currentSurface = "";
  size_t j = 0, numPrintTop = 10; // numPrintTop = 5;
  for(size_t i = 0; i < numPos; i++) {
    if(surface(staticSpan[i]) != currentSurface) {
      if(j < numPrintTop)
        std::cerr << "comparing '" << currentSurface << "' with bucket size " << staticBucket.size() << std::endl;

      // compare buckets, empty them
      EXPECT_EQ(staticBucket, dynamicBucket) << "comparison of '" << currentSurface << "' failed";
      staticBucket.clear();
      dynamicBucket.clear();
      currentSurface = surface(staticSpan[i]);
      j++;
    }

    staticBucket.insert(staticSpan[i]);
    dynamicBucket.insert(dynamicSpan[i]);
  }
  EXPECT_EQ(staticBucket, dynamicBucket);

  //remove_all(basePath);
}

TEST_F(BenchmarkTests, index_eim_small) {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);

  std::string textFile = "/home/david/tmp/eim-small.en"; // the training corpus included with MMT
  const size_t nlines = 100000; // not applicable limit

  /*
   * $ wc /tmp/eim-small.en
   * 5494 120370 654764 /tmp/eim-small.en
   *
   * 5.5 k lines, 120 k tokens
   */

  util::PrintUsage(std::cerr);

  benchmark_time([&corpus, &vocab, &textFile, &nlines](){
    ReadTextFile(corpus, vocab, textFile, nlines);
  }, "read");
  util::PrintUsage(std::cerr);

  ///////////////////////////

  TokenIndex<SrcToken> tokenIndex(corpus); // maxLeafSize = default (100 k)

  benchmark_time([&corpus, &vocab, &tokenIndex](){
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
      auto span = tokenIndex.span();
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
  //static_assert(sizeof(SrcToken) == 4); // -> ~ 1 MB Corpus track (ep.10k)

  // Sid (4), Offset (1) is padded up in the struct.
  //static_assert(sizeof(Position<SrcToken>) == 8); // -> ~2 MB TokenIndex, if purely SA based (ep.10k)

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
      auto span = tokenIndex.span();
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


TEST_F(BenchmarkTests, index_100k_disk) {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);

  std::string textFile = kTextFile;
  const size_t nlines = 100000;

  util::PrintUsage(std::cerr);

  benchmark_time([&corpus, &vocab, &textFile, &nlines](){
    ReadTextFile(corpus, vocab, textFile, nlines);
  }, "read");
  util::PrintUsage(std::cerr);

  ///////////////////////////

  remove_all(basePath); // ensure no leftovers

  std::shared_ptr<DB<SrcToken>> db(new DB<SrcToken>(basePath));

  TokenIndex<SrcToken, IndexTypeDisk> tokenIndex(basePath, corpus, db, /* maxLeafSize = */ 10000);

  BatchIndexBuffer<SrcToken> buffer(tokenIndex, /* batchSize = */ 1000);

  double time = benchmark_time([&corpus, &buffer](){
    for(size_t i = 0; i < corpus.size(); i++) {
      if(i % 1000 == 0)
        std::cerr << "tokenIndex @ AddSentence(i=" << i << ")..." << std::endl;
      buffer.AddSentence(corpus.sentence(i), i + 1);
    }
    buffer.Flush();
  }, "build_index");
  util::PrintUsage(std::cerr);

  std::cerr << (tokenIndex.span().size() / time) << " tokens/sec. indexed" << std::endl;
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


TEST_F(BenchmarkTests, query_positions_valid_eim_small) {
  Vocab<SrcToken> vocab("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<SrcToken> corpus("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &vocab);
  TokenIndex<SrcToken> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", corpus); // built with mtt-build

  std::string textFile = "/home/david/tmp/eim-small.en"; // the training corpus included with MMT

  /*
   * $ wc /tmp/eim-small.en
   * 5494 120370 654764 /tmp/eim-small.en
   *
   * 5.5 k lines, 120 k tokens
   */

  util::PrintUsage(std::cerr);

  ///////////////////////////

  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 130000); // ensure no split

  benchmark_time([&corpus, &vocab, &tokenIndex](){
    for(size_t i = 0; i < corpus.size(); i++) {
      if(i % 1000 == 0)
        std::cerr << "tokenIndex @ AddSentence(i=" << i << ")..." << std::endl;
      tokenIndex.AddSentence(corpus.sentence(i));
    }
  }, "build_index");
  util::PrintUsage(std::cerr);

  ///////////////////////////

  auto surface = [&corpus](Position<SrcToken> pos, size_t len_limit = -1){
    std::stringstream ss;
    for(size_t j = 0; j < len_limit && j + pos.offset <= corpus.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << pos.add(j, corpus).surface(corpus);
    return ss.str();
  };


  std::vector<std::vector<SrcToken>> queries;
  create_random_queries(tokenIndex, queries, /* num = */ 100000);


  std::random_device rd;
  std::mt19937 gen(rd());

  auto fullSpan = tokenIndex.span();

  size_t j = 0, numPrint = 0; // numPrint = 10

  for(auto query : queries) {
    //std::string querySurface = "";
    std::stringstream querySurface;
    for(size_t i = 0; i < query.size(); i++)
      querySurface << (i == 0 ? "" : " ") << vocab.at(query[i]);

    auto span = tokenIndex.span();
    for(auto token : query)
      EXPECT_GT(span.narrow(token), 0) << "queries for existing locations must succeed"; // since we just randomly sampled them, they must be in the corpus.

    for(size_t i = 0; i < span.size(); i++)
      EXPECT_EQ(querySurface.str(), surface(span[i], query.size())) << "the entire range must have the same surface prefix as the query";

    // note: out-of-bounds access only works if the underlying SA is not split, and the actually mapped range does not start at beginning/end of SA.

    if(!(span[0] == fullSpan[0]))
      EXPECT_NE(querySurface.str(), surface(span.at_unchecked(-1), query.size())) << "just before the range, there must not be the same surface form";
    else
      EXPECT_EQ("the", vocab.at(query[0])) << "range begins at SA begin, we expect the first word to be 'the'";

    if(!(span[span.size()-1] == fullSpan[fullSpan.size()-1]))
      EXPECT_NE(querySurface.str(), surface(span.at_unchecked(span.size()), query.size())) << "just after the range, there must not be the same surface form";

    if(j < numPrint) {
      std::cerr << "verified span for query '" << querySurface.str() << "', span size = " << span.size() << std::endl;
      if(!(span[0] == fullSpan[0]))
        std::cerr << " surface before: '" << surface(span.at_unchecked(-1), query.size()) << "'" << std::endl;
      if(!(span[span.size()-1] == fullSpan[fullSpan.size()-1]))
        std::cerr << " surface after: '" << surface(span.at_unchecked(span.size()), query.size()) << "'" << std::endl;
      j++;
    }
  }
}

TEST_F(BenchmarkTests, add_to_loaded_eim_small) {
  Vocab<SrcToken> vocab("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<SrcToken> corpus("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &vocab);
  TokenIndex<SrcToken> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", corpus); // built with mtt-build

  std::vector<std::string> src = {"magyarul", "egesz", "biztosan", "nem", "tudsz"};
  std::vector<SrcToken> sent;
  for(auto w : src)
    sent.push_back(vocab[w]);
  corpus.AddSentence(sent);

  staticIndex.AddSentence(corpus.sentence(corpus.size()-1));
}


// takes 700 s - 900 s, using a lot of data; so disabled for now.
TEST_F(BenchmarkTests, DISABLED_index_b11) {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus(&vocab);

  std::string textFile = "/home/david/mmt/data/benchmark-1.1-cat/05-dumped/train.en";

  /*
   * $ wc benchmark-1.1-cat/05-dumped/train.en
   * 6390894 103373600 591699147 benchmark-1.1-cat/05-dumped/train.en
   *
   * 6.39 M lines, 103 M tokens
   */
  //static_assert(sizeof(SrcToken) == 4);

  // Sid (4), Offset (1) is padded up in the struct.
  //static_assert(sizeof(Position<SrcToken>) == 8);

  util::PrintUsage(std::cerr);

  benchmark_time([&corpus, &vocab, &textFile](){
    ReadTextFile(corpus, vocab, textFile);
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
      auto span = tokenIndex.span();
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
