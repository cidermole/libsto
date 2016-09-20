/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Vocab.h"
#include "Types.h"

#include <memory>

#include "filesystem.h"
#include "DB.h"


using namespace sto;

/*
 * apple  10
 * orange 11
 * banana 12
 * and    13
 * pear   14
 */

TEST(VocabTests, insertion) {
  Vocab<SrcToken> sv;

  SrcToken w10 = sv["10"];   // insert 10
  SrcToken w11 = sv["11"]; // insert 11

  EXPECT_EQ(w10, sv.at("10")) << "comparing 10 and 10";
  EXPECT_NE(w10, w11) << "comparing 10 and 11";

  EXPECT_EQ("10", sv.at(10)) << "first Token must be 10";

  // we can even expect the unknown word to convert properly - stoi()!
  EXPECT_EQ(12, sv.at("12").vid); // previously expected: Vocab<SrcToken>::kUnkVid
}

/*
 * TODO: build ID vocab like that.
 *
TEST(VocabTests, load) {
  // file built like this:
  // $ echo "10 13 11 13 14 13 10 13 11" | mtt-build -i -o vocab
  Vocab<SrcToken> sv("res/vocab.tdx");

  EXPECT_EQ(0, sv.at("0").vid); // NULL
  EXPECT_EQ(2, sv.at("2").vid); // UNK
  EXPECT_TRUE(sv.at("10").vid > 0);
  EXPECT_TRUE(sv.at("11").vid > 0);
  EXPECT_TRUE(sv.at("13").vid > 0);
  EXPECT_TRUE(sv.at("14").vid > 0);

  //ASSERT_THROW(sv.at("12"), std::out_of_range) << "out-of-range access must throw an exception";
  EXPECT_EQ(Vocab<SrcToken>::kUnkVid, sv.at("12").vid);
}
 */

TEST(VocabTests, persist) {
  SrcToken w10, w11;
  size_t vocab_size;

  std::string basePath = "res/VocabTests";
  remove_all(basePath); // ensure no leftovers

  // fill an empty vocabulary with some words
  {
    std::shared_ptr<DB<SrcToken>> db(new DB<SrcToken>(basePath));
    Vocab<SrcToken> vocab(db);

    w10 = vocab["10"];   // insert 10
    w11 = vocab["11"]; // insert 11

    vocab_size = vocab.size();

    // close and re-open existing DB
    std::cerr << "ref count before close = " << db.use_count() << std::endl;
    // aah! vocab still uses it...
    db.reset(); // close must come before re-opening
    std::cerr << "close should have come here. ref count = " << db.use_count() << std::endl;
    // vocab must go out of scope (to release the db reference) so the db is closed
  }

  // verify if the vocab can be loaded from DB

  std::shared_ptr<DB<SrcToken>> db(new DB<SrcToken>(basePath));

  Vocab<SrcToken> sv(db); // load vocabulary from DB

  EXPECT_EQ(w10, sv.at("10")) << "comparing 10 and 10";
  EXPECT_NE(w10, w11) << "comparing 10 and 11";

  EXPECT_EQ("10", sv.at(10)) << "first Token must be 10";

  EXPECT_EQ(vocab_size, sv.size()) << "vocabulary sizes should match";

  // we can even expect the unknown word to convert properly - stoi()!
  EXPECT_EQ(12, sv.at("12").vid);

  //delete db; // done by shared_ptr

  //remove_all(basePath);
}
