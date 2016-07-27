/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Vocab.h"
#include "Types.h"

#include <memory>

#include <boost/filesystem.hpp>
#include "DB.h"


using namespace sto;

TEST(VocabTests, insertion) {
  Vocab<SrcToken> sv;

  SrcToken apple = sv["apple"];   // insert apple
  SrcToken orange = sv["orange"]; // insert orange

  EXPECT_EQ(apple, sv.at("apple")) << "comparing apple and apple";
  EXPECT_NE(apple, orange) << "comparing apple and orange";

  EXPECT_EQ("apple", sv.at(apple)) << "first Token must be apple";

  ASSERT_THROW(sv.at("banana"), std::out_of_range) << "out-of-range access must throw an exception";
}

TEST(VocabTests, load) {
  // file built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o vocab
  Vocab<SrcToken> sv("res/vocab.tdx");

  EXPECT_EQ(0, sv.at("NULL").vid);
  EXPECT_EQ(1, sv.at("UNK").vid);
  EXPECT_TRUE(sv.at("apple").vid > 0);
  EXPECT_TRUE(sv.at("orange").vid > 0);
  EXPECT_TRUE(sv.at("and").vid > 0);
  EXPECT_TRUE(sv.at("pear").vid > 0);

  ASSERT_THROW(sv.at("banana"), std::out_of_range) << "out-of-range access must throw an exception";
}

std::string getBasePath() {
  return "res/VocabTests";
}

std::string getCleanBasePath() {
  std::string basePath = getBasePath();

  using namespace boost::filesystem;
  boost::system::error_code ec;
  path base(basePath);
  remove_all(base, ec); // ensure no leftovers

  return basePath;
}

void removeTestBase() {
  getCleanBasePath();
}

TEST(VocabTests, persist) {
  SrcToken apple, orange, ref_end;

  // fill an empty vocabulary with some words
  {
    std::shared_ptr<DB<SrcToken>> db(new DB<SrcToken>(getCleanBasePath()));
    Vocab<SrcToken> vocab(db);

    apple = vocab["apple"];   // insert apple
    orange = vocab["orange"]; // insert orange

    ref_end = vocab.end();

    // close and re-open existing DB
    std::cerr << "ref count before close = " << db.use_count() << std::endl;
    // aah! vocab still uses it...
    db.reset(); // close must come before re-opening
    std::cerr << "close should have come here. ref count = " << db.use_count() << std::endl;
    // vocab must go out of scope (to release the db reference) so the db is closed
  }

  // verify if the vocab can be loaded from DB

  std::shared_ptr<DB<SrcToken>> db(new DB<SrcToken>(getBasePath()));

  Vocab<SrcToken> sv(db); // load vocabulary from DB

  EXPECT_EQ(apple, sv.at("apple")) << "comparing apple and apple";
  EXPECT_NE(apple, orange) << "comparing apple and orange";

  EXPECT_EQ("apple", sv.at(apple)) << "first Token must be apple";

  EXPECT_EQ(ref_end, sv.end()) << "vocabulary sizes should match";

  ASSERT_THROW(sv.at("banana"), std::out_of_range) << "out-of-range access must throw an exception";

  // clean up:

  // TODO: need a test fixture / RAII-finally which cleans up despite the throw above

  //delete db; // done by shared_ptr
  // removeTestBase(); // for debug: comment this to leave the DB in filesystem
}
