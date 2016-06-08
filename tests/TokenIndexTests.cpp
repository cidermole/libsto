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
  EXPECT_EQ("this", vocab[sentence[0]]) << "retrieving a word from Sentence";
}

TEST_F(TokenIndexTests, add_sentence) {
  sentence = AddSentence({"this", "is", "an", "example"});
  tokenIndex.AddSentence(sentence);
  IndexSpan<SrcToken> span = tokenIndex.span();
  EXPECT_EQ(4, span.size()) << "the Sentence should have added 4 tokens to the IndexSpan";
}

TEST_F(TokenIndexTests, suffix_array_paper_example) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)

  EXPECT_EQ(1, vocab["</s>"].vid);
  EXPECT_LT(vocab["dog"].vid, vocab["the"].vid);

  // '", "'.join(['"'] + 'the dog bit the cat on the mat </s>'.split() + ['"'])
  //                                     0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};
  sentence = AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);
  IndexSpan<SrcToken> span = tokenIndex.span();
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  // ideas:
  // * add sanity check function for verifying partial sums
  // * add slow AtUnordered() that doesn't use partial sums?
  // * add slow At() which sorts the hash map before access (completely ordered)

  // as long as we're accessing the SA, we are properly ordered.
  //assert(tokenIndex.root_->is_leaf()); // private member...

  Position<SrcToken> pos{/* sid = */ 0, /* offset = */ 2};
  EXPECT_EQ(pos, span[1]) << "verifying token position for 'bit'";
  EXPECT_EQ((Position<SrcToken>{/* sid = */ 0, /* offset = */ 2}), span[1]) << "verifying token position for 'bit'";

  std::vector<size_t>      expect_suffix_array_offset  = {8,      2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"</s>", "bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], span[i].surface(corpus)) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }
}

TEST_F(TokenIndexTests, suffix_array_split) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 8);

  //                                     0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};

  sentence = AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);

  // because of maxLeafSize=8, these accesses go via a first TreeNode level.
  // however, the hash function % ensures that our vids are still in order, even though the API doesn't guarantee this.
  // so this is not a good test.

  IndexSpan<SrcToken> span = tokenIndex.span();
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  std::vector<size_t>      expect_suffix_array_offset  = {8,      2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"</s>", "bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], span[i].surface(corpus)) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }

  tokenIndex.DebugPrint();

  span.narrow(vocab["bit"]);
  EXPECT_EQ(1, span.size()) << "'bit' range size check";
  span.narrow(vocab["the"]);
  EXPECT_EQ(1, span.size()) << "'bit the' range size check";
  size_t newsz = span.narrow(vocab["dog"]);
  EXPECT_EQ(0, newsz) << "'bit the dog' must be size 0, i.e. not found";

  EXPECT_EQ(1, span.size()) << "failed call must not narrow the span";
  EXPECT_EQ(1, span.narrow(vocab["cat"])) << "IndexSpan must now behave as if the failed narrow() call had not happened";

  span = tokenIndex.span();
  EXPECT_EQ(3, span.narrow(vocab["the"])) << "'the' range size check";
  EXPECT_EQ(1, span.narrow(vocab["cat"])) << "'the' range size check";
  EXPECT_EQ(1, span.size()) << "span size";
}

TEST_F(TokenIndexTests, suffix_array_common_prefix) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 8);

  //                                      0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent1_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};

  sentence = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2      3
  std::vector<std::string> sent2_words = {"the", "dog", "bit", "</s>"};

  Sentence<SrcToken> sentence2 = AddSentence(sent2_words);
  tokenIndex.AddSentence(sentence2);

  tokenIndex.DebugPrint();
}

TEST_F(TokenIndexTests, suffix_array_common_prefix_the) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 4);

  //                                      0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent1_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};

  sentence = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2      3
  std::vector<std::string> sent2_words = {"the", "dog", "bit", "</s>"};

  Sentence<SrcToken> sentence2 = AddSentence(sent2_words);
  tokenIndex.AddSentence(sentence2);

  // a leaf </s> attached to 'the' which itself should be split:

  //                                      0      1
  std::vector<std::string> sent3_words = {"the", "</s>"};

  Sentence<SrcToken> sentence3 = AddSentence(sent3_words);
  tokenIndex.AddSentence(sentence3);


  tokenIndex.DebugPrint();
}
