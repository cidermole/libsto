/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <iostream>
#include <gtest/gtest.h>

#include "Bitext.h"

#include "util/Time.h"
#include "filesystem.h"

using namespace sto;

/** makes a few Bitext members accessible for testing */
class TestBitext : public Bitext {
public:
  TestBitext(const std::string &l1, const std::string &l2) : Bitext(l1, l2) {}
  TestBitext(const std::string &base, const std::string &l1, const std::string &l2) : Bitext(base, l1, l2) {}

  sto::Corpus<sto::AlignmentLink>& Align() { return *this->align_; }
  sto::BitextSide<sto::SrcToken>& Src() { return *this->src_; }
  sto::BitextSide<sto::TrgToken>& Trg() { return *this->trg_; }
};


TEST(BitextTests, create_empty_write) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  Bitext bitext("fr", "en");
  bitext.Write(dirname + "/bitext."); // write an empty Bitext

  //remove_all(dirname);
}

TEST(BitextTests, create_empty_write_read) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  TestBitext bitext("fr", "en");
  EXPECT_EQ(0, bitext.Align().size());
  bitext.Write(base); // write an empty Bitext

  // just a rudimentary test to ensure loading from disk does not fail;
  // class Bitext does not provide read access, so the remaining tests must be from moses.

  TestBitext read(base, "fr", "en");
  EXPECT_EQ(0, read.Align().size());

  //remove_all(dirname);
}


TEST(BitextTests, add_memory) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  TestBitext bitext("fr", "en");

  // test add to memory-only Bitext
  bitext.Add(
      mmt::updateid_t{static_cast<stream_t>(-1), 1},
      1, // domain
      std::vector<mmt::wid_t>{14, 15}, // source words
      std::vector<mmt::wid_t>{24, 25, 26}, // target sentence words
      std::vector<std::pair<mmt::length_t, mmt::length_t>>{{0,0}, {1,2}}
  );
  EXPECT_EQ(1, bitext.Align().size());
}

TEST(BitextTests, create_add_write_read) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  Bitext bitext("fr", "en");

  bitext.Write(base); // write an empty Bitext


  {
    Bitext writable(base, "fr", "en");

    // add to already existing, empty, persisted Bitext
    writable.Add(
        mmt::updateid_t{static_cast<stream_t>(-1), 1},
        1, // domain
        std::vector<mmt::wid_t>{14, 15}, // source words
        std::vector<mmt::wid_t>{24, 25, 26}, // target sentence words
        std::vector<std::pair<mmt::length_t, mmt::length_t>>{{0,0}, {1,2}}
    );
  }


  // just a rudimentary test to ensure loading from disk does not fail

  TestBitext updated(base, "fr", "en");
  EXPECT_EQ(1, updated.Align().size());

  // try adding another sent pair

  updated.Add(
      mmt::updateid_t{static_cast<stream_t>(-1), 1},
      1, // domain
      std::vector<mmt::wid_t>{14, 17}, // source words
      std::vector<mmt::wid_t>{24, 25, 26, 28}, // target sentence words
      std::vector<std::pair<mmt::length_t, mmt::length_t>>{{0,0}, {1,3}}
  );
  EXPECT_EQ(2, updated.Align().size());

  //remove_all(dirname);
}


/*
 * Convert MMT demo corpus and load it up. Similar to what StoWriteTests.persistent_add should do in Moses wrapper
 */
TEST(BitextTests, convert_eim_small) {
  TestBitext bitext1("en", "it");

  // load legacy bitext
  bitext1.Open("/home/david/MMT/vendor/../engines/default/models/phrase_tables/model.");


  std::string base = "res/BitextTests";
  remove_all(base);
  create_directory(base);

  // Convert legacy bitext to v3 (persistent incremental) format
  bitext1.Write(base + "/bitext.");


  TestBitext new_bitext("en", "it");

  // load v3 bitext just written
  new_bitext.Open(base + "/bitext.");

  EXPECT_EQ(bitext1.Align().size(), new_bitext.Align().size());

  // test full alignment equality
  for(size_t isent = 0; isent < bitext1.Align().size(); isent++) {
    auto sent1 = bitext1.Align().sentence(isent);
    auto sent2 = new_bitext.Align().sentence(isent);
    EXPECT_EQ(sent1.size(), sent2.size()) << "size equality of sent " << isent;
    for(size_t i = 0; i < sent1.size(); i++)
      EXPECT_EQ(sent1[i], sent2[i]) << "equality of sent " << isent << " alignment pair " << i;
  }
}


/*
 * Convert MMT demo corpus and load it up. Then, add a sentence pair (should be persisted to disk).
 * Then, load again and query.
 *
 * Similar to what StoWriteTests.persistent_add does last in Moses wrapper
 */
TEST(BitextTests, convert_append_eim_small) {
  TestBitext bitext1("en", "it");

  mmt::domain_t europarl = 1;

  std::vector<mmt::wid_t> src_sent = {10, 11};

  // load legacy bitext
  bitext1.Open("/home/david/MMT/vendor/../engines/default/models/phrase_tables/model.");


  std::string base = "res/BitextTests";
  remove_all(base);
  create_directory(base);

  std::cerr << "legacy span size before: src=" << bitext1.Src().index()->span().size() << " trg=" << bitext1.Trg().index()->span().size() << std::endl;

  // Convert legacy bitext to v3 (persistent incremental) format
  // note: shouldn't we split it first?
  std::cerr << "Write() converting legacy bitext to v3..." << std::endl;
  bitext1.Write(base + "/bitext.");
  std::cerr << "Write() done." << std::endl;



  // test full alignment equality
  auto test_alignment_equality = [&bitext1](TestBitext &new_bitext) {
    //std::cerr << "test full alignment equality..." << std::endl;
    for(size_t isent = 0; isent < bitext1.Align().size(); isent++) {
      auto sent1 = bitext1.Align().sentence(isent);
      auto sent2 = new_bitext.Align().sentence(isent);
      EXPECT_EQ(sent1.size(), sent2.size()) << "size equality of sent " << isent;
      for(size_t i = 0; i < sent1.size(); i++)
        EXPECT_EQ(sent1[i], sent2[i]) << "equality of sent " << isent << " alignment pair " << i;
    }
    //std::cerr << "done." << std::endl;
  };

  size_t size_before = 0;

  {
    TestBitext new_bitext("en", "it");

    // load v3 bitext just written
    new_bitext.Open(base + "/bitext.");

    EXPECT_EQ(bitext1.Align().size(), new_bitext.Align().size());
    size_before = bitext1.Align().size();

    test_alignment_equality(new_bitext);

    std::cerr << "index span size before: src=" << new_bitext.Src().index()->span().size() << " trg=" << new_bitext.Trg().index()->span().size() << std::endl;
    std::cerr << "domain-specific index span size before: src=" << new_bitext.Src().domain_indexes[europarl]->span().size() << std::endl;

    // add sentence pair (to both corpora)

    auto add_sentence_pair = [&](Bitext &bitxt) {
      std::cerr << "Bitext::Add()..." << std::endl;

      bitxt.Add(
          mmt::updateid_t{static_cast<stream_t>(-1), 1},
          europarl, // to existing domain (europarl)
          src_sent,
          std::vector<mmt::wid_t>{20, 21, 22, 23},
          std::vector<std::pair<mmt::length_t, mmt::length_t>>{{0,0}, {1,3}}
      );
      std::cerr << "Done." << std::endl;
    };

    benchmark_time([&bitext1, &add_sentence_pair]() {
      add_sentence_pair(bitext1);
    }, "add_sentence_pair(bitext1)");

    benchmark_time([&new_bitext, &add_sentence_pair]() {
      add_sentence_pair(new_bitext);
    }, "add_sentence_pair(new_bitext)");

    // new_bitext goes out of scope (cleans up, closes DB)
  }

  TestBitext updated_bitext("en", "it");

  // load v3 bitext just written
  updated_bitext.Open(base + "/bitext.");

  EXPECT_EQ(size_before + 1, updated_bitext.Align().size());
  EXPECT_EQ(bitext1.Align().size(), updated_bitext.Align().size());

  // test full alignment equality, again
  test_alignment_equality(updated_bitext);

  // test token index

  std::cerr << "index span size after: src=" << updated_bitext.Src().index()->span().size() << " trg=" << updated_bitext.Trg().index()->span().size() << std::endl;
  std::cerr << "domain-specific index span size after: src=" << updated_bitext.Src().domain_indexes[1]->span().size() << std::endl;

  EXPECT_EQ(bitext1.Src().index()->span().size(), updated_bitext.Src().index()->span().size());
  EXPECT_EQ(bitext1.Trg().index()->span().size(), updated_bitext.Trg().index()->span().size());

  // test src token index contents

  sto::IndexSpan<sto::SrcToken> span1 = bitext1.Src().index()->span();
  sto::IndexSpan<sto::SrcToken> new_span = updated_bitext.Src().index()->span();
  EXPECT_EQ(span1.size(), new_span.size());
  for(size_t i = 0; i < span1.size(); i++) {
    // maybe we should not expect each position to be equal (the current test assumes the sort is stable, or at least same between the two Bitext). use buckets?!
    EXPECT_EQ(span1[i], new_span[i]);
  }

  // test domain index

  EXPECT_EQ(bitext1.Src().domain_indexes[europarl]->span().size(), updated_bitext.Src().domain_indexes[europarl]->span().size());

  // test vocabulary

  std::vector<mmt::wid_t> retrieved_src_words;
  auto sent = bitext1.Src().corpus->sentence(bitext1.Src().corpus->size()-1);
  for(size_t i = 0; i < sent.size(); i++)
    retrieved_src_words.push_back(sent[i].vid);

  EXPECT_EQ(src_sent, retrieved_src_words);
}
