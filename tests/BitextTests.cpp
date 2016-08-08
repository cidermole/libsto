/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Bitext.h"

#include "filesystem.h"

using namespace sto;

/** makes a few Bitext members accessible for testing */
class TestBitext : public Bitext {
public:
  TestBitext(const std::string &l1, const std::string &l2) : Bitext(l1, l2) {}
  TestBitext(const std::string &base, const std::string &l1, const std::string &l2) : Bitext(base, l1, l2) {}

  sto::Corpus<sto::AlignmentLink>& Align() { return *this->align_; }
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
  bitext.AddSentencePair(
      std::vector<std::string>{"source", "words"},
      std::vector<std::string>{"target", "sentence", "words"},
      std::vector<std::pair<size_t, size_t>>{{0,0}, {1,2}},
      "dom1"
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
    writable.AddSentencePair(
        std::vector<std::string>{"source", "words"},
        std::vector<std::string>{"target", "sentence", "words"},
        std::vector<std::pair<size_t, size_t>>{{0,0}, {1,2}},
        "dom1"
    );
  }


  // just a rudimentary test to ensure loading from disk does not fail

  TestBitext updated(base, "fr", "en");
  EXPECT_EQ(1, updated.Align().size());

  // try adding another sent pair

  updated.AddSentencePair(
      std::vector<std::string>{"source", "words2"},
      std::vector<std::string>{"target", "side", "lang", "words2"},
      std::vector<std::pair<size_t, size_t>>{{0,0}, {1,3}},
      "dom1"
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

  sto::IncrementalBitext *bitext = &bitext1;

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
