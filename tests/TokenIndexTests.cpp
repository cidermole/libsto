/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Vocab.h"
#include "Corpus.h"
#include "TokenIndex.h"
#include "Types.h"

using namespace sto;

/**
 * Test Fixture for testing TokenIndex.
 */
struct TokenIndexTests : testing::Test {
  Vocab<SrcToken> vocab;
  Corpus<SrcToken> corpus;
  Sentence<SrcToken> sentence;

  TokenIndexTests() : corpus(vocab) {
    std::vector<std::string> surface = {"this", "is", "an", "example"};
    std::vector<SrcToken> sent;
    for(auto s : surface)
      sent.push_back(vocab[s]); // vocabulary insert/lookup

    corpus.AddSentence(sent);

    // retrieve Sentence from Corpus
    sentence = corpus.sentence(0);
  }
  virtual ~TokenIndexTests() {}
};

// demo Test Fixture
TEST_F(TokenIndexTests, get_word) {
  EXPECT_EQ(vocab[sentence[0]], "this") << "retrieving a word from Sentence";
}

TEST_F(TokenIndexTests, asdf) {
  EXPECT_EQ(vocab[sentence[0]], "this") << "retrieving a word from Sentence";
}
