/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <sstream>
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

  void fill_tree_2level_common_prefix_the(TokenIndex<SrcToken> &tokenIndex);
  void tree_2level_common_prefix_the_m(size_t maxLeafSize);

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
  tokenIndex.DebugPrint(std::cerr);
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  std::vector<size_t>      expect_suffix_array_offset  = {8,      2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"</s>", "bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], span[i].surface(corpus)) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }

  //tokenIndex.DebugPrint(std::cerr);

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

TEST_F(TokenIndexTests, tree_common_prefix) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 8);

  //                                      0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};

  sentence = AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2      3
  std::vector<std::string> sent1_words = {"the", "dog", "bit", "</s>"};

  Sentence<SrcToken> sentence1 = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);


  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=13 is_leaf=false
* '</s>' vid=1
  TreeNode size=2 is_leaf=true
  * [sid=0 offset=8]
  * [sid=1 offset=3]
* 'bit' vid=2
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=2]
  * [sid=0 offset=2]
* 'cat' vid=3
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=4]
* 'dog' vid=4
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=1]
  * [sid=0 offset=1]
* 'mat' vid=5
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=7]
* 'on' vid=6
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=5]
* 'the' vid=7
  TreeNode size=4 is_leaf=true
  * [sid=0 offset=3]
  * [sid=1 offset=0]
  * [sid=0 offset=0]
  * [sid=0 offset=6]
)";

  EXPECT_EQ(expected_tree, actual_tree.str()) << "tree structure";
}

void TokenIndexTests::fill_tree_2level_common_prefix_the(TokenIndex<SrcToken> &tokenIndex) {

  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)

  //                                      0      1      2      3      4      5     6      7      8
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat", "</s>"};

  sentence = AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2      3
  std::vector<std::string> sent1_words = {"the", "dog", "bit", "</s>"};

  Sentence<SrcToken> sentence1 = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);

  // a leaf </s> attached to 'the' which itself should be split:

  //                                      0      1
  std::vector<std::string> sent2_words = {"the", "</s>"};

  Sentence<SrcToken> sentence2 = AddSentence(sent2_words);
  tokenIndex.AddSentence(sentence2);
}

TEST_F(TokenIndexTests, tree_2level_common_prefix_the) {
  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 4);
  fill_tree_2level_common_prefix_the(tokenIndex);

  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=15 is_leaf=false
* '</s>' vid=1
  TreeNode size=3 is_leaf=true
  * [sid=0 offset=8]
  * [sid=1 offset=3]
  * [sid=2 offset=1]
* 'bit' vid=2
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=2]
  * [sid=0 offset=2]
* 'cat' vid=3
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=4]
* 'dog' vid=4
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=1]
  * [sid=0 offset=1]
* 'mat' vid=5
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=7]
* 'on' vid=6
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=5]
* 'the' vid=7
  TreeNode size=5 is_leaf=false
  * '</s>' vid=1
    TreeNode size=1 is_leaf=true
    * [sid=2 offset=0]
  * 'cat' vid=3
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=3]
  * 'dog' vid=4
    TreeNode size=2 is_leaf=true
    * [sid=1 offset=0]
    * [sid=0 offset=0]
  * 'mat' vid=5
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=6]
)";

  EXPECT_EQ(expected_tree, actual_tree.str()) << "tree structure";
}

void TokenIndexTests::tree_2level_common_prefix_the_m(size_t maxLeafSize) {
  TokenIndex<SrcToken> tokenIndex(corpus, maxLeafSize);
  fill_tree_2level_common_prefix_the(tokenIndex);

  std::vector<Position<SrcToken>> expected_pos = {
      {0, 8},
      {1, 3},
      {2, 1},
      {1, 2},
      {0, 2},
      {0, 4},
      {1, 1},
      {0, 1},
      {0, 7},
      {0, 5},
      {2, 0},
      {0, 3},
      {1, 0},
      {0, 0},
      {0, 6}
  };

  std::vector<Position<SrcToken>> actual_pos;
  IndexSpan<SrcToken> span = tokenIndex.span();
  // IndexSpan could support iteration...
  for(size_t i = 0; i < span.size(); i++)
    actual_pos.push_back(span[i]);

  EXPECT_EQ(expected_pos, actual_pos) << "flattened suffix array equality with maxLeafSize = " << maxLeafSize;
}


TEST_F(TokenIndexTests, tree_2level_common_prefix_the_4) {
  tree_2level_common_prefix_the_m(/* maxLeafSize = */ 4);
}

TEST_F(TokenIndexTests, tree_2level_common_prefix_the_5) {
  // check invariance: maxLeafSize = 4 and maxLeafSize = 5 (without the 'the' split) should behave exactly the same
  tree_2level_common_prefix_the_m(/* maxLeafSize = */ 5);
}

TEST_F(TokenIndexTests, tree_2level_common_prefix_the_15) {
  // check invariance: maxLeafSize = 15 (without any split; common SA) should behave exactly the same
  tree_2level_common_prefix_the_m(/* maxLeafSize = */ 15);
}
