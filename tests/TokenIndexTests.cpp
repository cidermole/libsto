/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <sstream>
#include <utility>
#include <unordered_set>

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

  TokenIndexTests() : corpus(&vocab), tokenIndex(corpus) {}
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
  //                                     0      1      2      3      4      5     6      7
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};
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

  //tokenIndex.DebugPrint(std::cerr);

  Position<SrcToken> pos{/* sid = */ 0, /* offset = */ 2};
  EXPECT_EQ(pos, span[0]) << "verifying token position for 'bit'";
  EXPECT_EQ((Position<SrcToken>{/* sid = */ 0, /* offset = */ 2}), span[0]) << "verifying token position for 'bit'";

  std::vector<size_t>      expect_suffix_array_offset  = {2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], span[i].surface(corpus)) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }
}

TEST_F(TokenIndexTests, load_v2) {
  // 'index.sfa' file built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o index
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc("res/corpus.mct", &sv);
  TokenIndex<SrcToken> staticIndex("res/index.sfa", sc); // built with mtt-build

  TokenIndex<SrcToken> dynamicIndex(sc); // building dynamically
  dynamicIndex.AddSentence(sc.sentence(0));

  IndexSpan<SrcToken> staticSpan = staticIndex.span();
  IndexSpan<SrcToken> dynamicSpan = dynamicIndex.span();

  EXPECT_EQ(staticSpan.size(), dynamicSpan.size()) << "two ways of indexing the same corpus must be equivalent";

  size_t num_pos = staticSpan.size();
  for(size_t i = 0; i < num_pos; i++) {
    EXPECT_EQ(staticSpan[i], dynamicSpan[i]) << "Position entry " << i << " must match between static and dynamic TokenIndex";
  }
}

TEST_F(TokenIndexTests, suffix_array_split) {
  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    vocab[s]; // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 7);

  //                                     0      1      2      3      4      5     6      7
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  sentence = AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);

  // because of maxLeafSize=8, these accesses go via a first TreeNode level.
  // however, the hash function % ensures that our vids are still in order, even though the API doesn't guarantee this.
  // so this is not a good test.

  IndexSpan<SrcToken> span = tokenIndex.span();
  //tokenIndex.DebugPrint(std::cerr);
  // we should check here if it's really split, i.e. root_->is_leaf() == false.
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  std::vector<size_t>      expect_suffix_array_offset  = {2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"bit", "cat", "dog", "mat", "on", "the", "the", "the"};

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


  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 7);

  //                                      0      1      2      3      4      5     6      7
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  sentence = AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2
  std::vector<std::string> sent1_words = {"the", "dog", "bit"};

  Sentence<SrcToken> sentence1 = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);


  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=11 is_leaf=false
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

  //                                      0      1      2      3      4      5     6      7
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  sentence = AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2
  std::vector<std::string> sent1_words = {"the", "dog", "bit"};

  Sentence<SrcToken> sentence1 = AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);

  // a leaf </s> attached to 'the' which itself should be split:

  std::stringstream nil;
  tokenIndex.DebugPrint(nil); // print to nowhere, but still run size asserts etc.

  //                                      0
  std::vector<std::string> sent2_words = {"the"};

  Sentence<SrcToken> sentence2 = AddSentence(sent2_words);
  tokenIndex.AddSentence(sentence2);

  tokenIndex.DebugPrint(nil); // print to nowhere, but still run size asserts etc.
}

TEST_F(TokenIndexTests, tree_2level_common_prefix_the) {
  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 4);
  fill_tree_2level_common_prefix_the(tokenIndex);

  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=12 is_leaf=false
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

namespace std {
template <> struct hash<Position<SrcToken>> {
  std::size_t operator()(const Position<SrcToken>& pos) const {
    return ((hash<typename Position<SrcToken>::Offset>()(pos.offset))
             ^ (hash<typename Position<SrcToken>::Sid>()(pos.sid) << 8));
  }
};
} // namespace std

TEST_F(TokenIndexTests, static_vs_dynamic_eim) {
  Vocab<SrcToken> sv("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<SrcToken> sc("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &sv);
  TokenIndex<SrcToken> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", sc); // built with mtt-build

  TokenIndex<SrcToken> dynamicIndex(sc); // building dynamically

  std::cerr << "building dynamicIndex..." << std::endl;
  for(size_t i = 0; i < sc.size(); i++)
    dynamicIndex.AddSentence(sc.sentence(i));
  std::cerr << "building dynamicIndex done." << std::endl;

  IndexSpan<SrcToken> staticSpan = staticIndex.span();
  IndexSpan<SrcToken> dynamicSpan = dynamicIndex.span();

  EXPECT_EQ(staticSpan.size(), dynamicSpan.size()) << "two ways of indexing the same corpus must be equivalent";

/*
  auto print = [&sc](Position<SrcToken> pos){
    std::cerr << "[sid=" << int(pos.sid) << " offset=" << int(pos.offset) << "]";
    for(size_t j = 0; j < 5 && j + pos.offset <= sc.sentence(pos.sid).size(); j++)
      std::cerr << " '" << pos.add(j, sc).surface(sc) << "'";
    std::cerr << std::endl;
  };

 // we are not equal position by position. check why:
  for(size_t i = 0; i < 5; i++) {
    Position<SrcToken> sp = staticSpan[i], dp = dynamicSpan[i];
    std::cerr << "static  "; print(sp);
    std::cerr << "dynamic "; print(dp);
    std::cerr << std::endl;
  }
*/
  auto surface = [&sc](Position<SrcToken> pos){
    std::stringstream ss;
    for(size_t j = 0; j + pos.offset <= sc.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << pos.add(j, sc).surface(sc);
    return ss.str();
  };

  size_t numPos = staticSpan.size();
  std::unordered_set<Position<SrcToken>> staticBucket, dynamicBucket;
  std::string currentSurface = "";
  size_t j = 0, numPrintTop = 0; // numPrintTop = 5;
  for(size_t i = 0; i < numPos; i++) {
    if(surface(staticSpan[i]) != currentSurface) {
      if(j < numPrintTop)
        std::cerr << "comparing '" << currentSurface << "' with bucket size " << staticBucket.size() << std::endl;

      // compare buckets, empty them
      EXPECT_EQ(staticBucket, dynamicBucket);
      staticBucket.clear();
      dynamicBucket.clear();
      currentSurface = surface(staticSpan[i]);
      j++;
    }

    staticBucket.insert(staticSpan[i]);
    dynamicBucket.insert(dynamicSpan[i]);
    //EXPECT_EQ(staticSpan[i], dynamicSpan[i]) << "Position entry " << i << " must match between static and dynamic TokenIndex"; // position by position, this is not true (sort stability?)
  }
  EXPECT_EQ(staticBucket, dynamicBucket);
}
