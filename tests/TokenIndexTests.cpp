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
  TokenIndex<SrcToken> tokenIndex;

  Sentence<SrcToken> AddSentence(const std::vector<std::string> &surface) {
    std::vector<SrcToken> sent;
    for(auto s : surface)
      sent.push_back(vocab[s]); // vocabulary insert/lookup

    corpus.AddSentence(sent);

    // retrieve Sentence from Corpus
    return corpus.sentence(corpus.size() - 1);
  }

  TokenIndexTests() : corpus(vocab), tokenIndex(corpus) {}
  virtual ~TokenIndexTests() {}
};

// demo Test Fixture
TEST_F(TokenIndexTests, get_word) {
  sentence = AddSentence({"this", "is", "an", "example"});
  EXPECT_EQ(vocab[sentence[0]], "this") << "retrieving a word from Sentence";
}

TEST_F(TokenIndexTests, add_sentence) {
  sentence = AddSentence({"this", "is", "an", "example"});
  tokenIndex.AddSentence(sentence);
  IndexSpan<SrcToken> span = tokenIndex.span();
  EXPECT_EQ(span.size(), 4) << "the Sentence should have added 4 tokens to the IndexSpan";
}

TEST_F(TokenIndexTests, paper_example_suffix_array) {
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)

  EXPECT_EQ(vocab["</s>"].vid, 1);
  EXPECT_LT(vocab["dog"].vid, vocab["the"].vid);

  // '", "'.join(['"'] + 'the dog bit the cat on the mat </s>'.split() + ['"'])
  //                                      0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};
  sentence = AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);
  IndexSpan<SrcToken> span = tokenIndex.span();
  EXPECT_EQ(span.size(), sent_words.size()) << "the Sentence should have added its tokens to the IndexSpan";

  // ideas:
  // * add sanity check function for verifying partial sums
  // * add slow AtUnordered() that doesn't use partial sums?
  // * add slow At() which sorts the hash map before access (completely ordered)

  // as long as we're accessing the SA, we are properly ordered.
  //assert(tokenIndex.root_->is_leaf()); // private member...

  Position<SrcToken> pos{/* sid = */ 0, /* offset = */ 2};
  EXPECT_EQ(span[1], pos) << "verifying token position for 'bit'";
  EXPECT_EQ(span[1], (Position<SrcToken>{/* sid = */ 0, /* offset = */ 2})) << "verifying token position for 'bit'";

  std::vector<size_t>      expect_suffix_array_offset  = { 8,      2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"</s>", "bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(span[i].surface(corpus), expect_suffix_array_surface[i]) << "verifying surface @ SA position " << i;
    EXPECT_EQ(span[i].offset, expect_suffix_array_offset[i]) << "verifying offset @ SA position " << i;
  }
}