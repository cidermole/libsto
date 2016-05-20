/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Vocab.h"
#include "Types.h"

using namespace sto;

TEST(Vocab, insertion) {
  Vocab<SrcToken> sv;

  SrcToken apple = sv["apple"];   // insert apple
  SrcToken orange = sv["orange"]; // insert orange

  EXPECT_EQ(apple, sv.at("apple")) << "comparing apple and apple";
  EXPECT_NE(apple, orange) << "comparing apple and orange";

  ASSERT_THROW(sv.at("banana"), std::out_of_range) << "out-of-range access must throw an exception";
}

TEST(Vocab, load) {
  // file built like this:
  // $ echo "apple orange and pear" | mtt-build -i -o vocab.tdx
  Vocab<SrcToken> sv("res/vocab.tdx");

  EXPECT_EQ(sv.at("NULL").vid, 0);
  EXPECT_EQ(sv.at("UNK").vid, 1);
  EXPECT_TRUE(sv.at("apple").vid > 0);
  EXPECT_TRUE(sv.at("orange").vid > 0);
  EXPECT_TRUE(sv.at("and").vid > 0);
  EXPECT_TRUE(sv.at("pear").vid > 0);

  ASSERT_THROW(sv.at("banana"), std::out_of_range) << "out-of-range access must throw an exception";
}
