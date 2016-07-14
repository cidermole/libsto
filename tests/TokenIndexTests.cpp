/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <random>
#include <sstream>
#include <utility>
#include <unordered_set>

#include <gtest/gtest.h>

#include "Vocab.h"
#include "Corpus.h"
#include "TokenIndex.h"
#include "Types.h"

#include "util/Time.h"
#include "util/usage.h"

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

  void create_random_queries(TokenIndex<SrcToken> &tokenIndex, std::vector<std::vector<SrcToken>> &queries, size_t num = 100000);
};

// demo Test Fixture
TEST_F(TokenIndexTests, get_word) {
  sentence = AddSentence({"this", "is", "an", "example"});
  EXPECT_EQ("this", vocab[sentence[0]]) << "retrieving a word from Sentence";
}

TEST_F(TokenIndexTests, add_sentence) {
  sentence = AddSentence({"this", "is", "an", "example"});
  tokenIndex.AddSentence(sentence);
  TokenIndex<SrcToken>::Span span = tokenIndex.span();
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
  TokenIndex<SrcToken>::Span span = tokenIndex.span();
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

  TokenIndex<SrcToken>::Span staticSpan = staticIndex.span();
  TokenIndex<SrcToken>::Span dynamicSpan = dynamicIndex.span();

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

  TokenIndex<SrcToken>::Span span = tokenIndex.span();
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
  TokenIndex<SrcToken>::Span span = tokenIndex.span();
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

  TokenIndex<SrcToken>::Span staticSpan = staticIndex.span();
  TokenIndex<SrcToken>::Span dynamicSpan = dynamicIndex.span();

  EXPECT_EQ(staticSpan.size(), dynamicSpan.size()) << "two ways of indexing the same corpus must be equivalent";

  auto surface = [&sc](Position<SrcToken> pos){
    std::stringstream ss;
    for(size_t j = 0; j + pos.offset <= sc.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << pos.add(j, sc).surface(sc);
    return ss.str();
  };

  // there is no equality position by position (sort stability?)
  // however, for each surface form, there must be equality among their positions

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
  }
  EXPECT_EQ(staticBucket, dynamicBucket);
}


// TODO DRY BenchmarkTests
void TokenIndexTests::create_random_queries(TokenIndex<SrcToken> &tokenIndex, std::vector<std::vector<SrcToken>> &queries, size_t num) {
  std::random_device rd;
  std::mt19937 gen(rd());
  TokenIndex<SrcToken>::Span span = tokenIndex.span();
  std::uniform_int_distribution<size_t> dist(0, span.size()-1); // spans all corpus positions

  for(size_t i = 0; i < num; i++) {
    // pick corpus positions at random
    Position<SrcToken> pos = span[dist(gen)];
    Sentence<SrcToken> sent = tokenIndex.corpus()->sentence(pos.sid);

    assert(sent.size() > 0); // no empty sents
    assert(pos.offset <= sent.size()); // implicit </s> may be at offset=sent.size()
    size_t max_len = std::min<size_t>(5, sent.size() - static_cast<size_t>(pos.offset)); // note: never query </s> by itself
    if(max_len == 0) {
      // sample another position/sentence instead (never query </s> by itself)
      i--;
      continue;
    }

    std::uniform_int_distribution<size_t> len_dist(1, max_len);
    size_t len = len_dist(gen);
    std::vector<SrcToken> query;
    for(size_t j = 0; j < len; j++)
      query.push_back(pos.add(j, *tokenIndex.corpus()).token(*tokenIndex.corpus()));

    queries.push_back(query);
  }
}


TEST_F(TokenIndexTests, query_positions_valid_eim_small) {
  Vocab<SrcToken> vocab("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<SrcToken> corpus("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &vocab);
  TokenIndex<SrcToken> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", corpus); // built with mtt-build

  std::string textFile = "/home/david/tmp/eim-small.en"; // the training corpus included with MMT

  /*
   * $ wc /tmp/eim-small.en
   * 5494 120370 654764 /tmp/eim-small.en
   *
   * 5.5 k lines, 120 k tokens
   */

  util::PrintUsage(std::cerr);

  ///////////////////////////

  TokenIndex<SrcToken> tokenIndex(corpus, /* maxLeafSize = */ 130000); // ensure no split

  benchmark_time([&corpus, &vocab, &tokenIndex](){
    for(size_t i = 0; i < corpus.size(); i++) {
      if(i % 1000 == 0)
        std::cerr << "tokenIndex @ AddSentence(i=" << i << ")..." << std::endl;
      tokenIndex.AddSentence(corpus.sentence(i));
    }
  }, "build_index");
  util::PrintUsage(std::cerr);

  ///////////////////////////

  auto surface = [&corpus](Position<SrcToken> pos, size_t len_limit = -1){
    std::stringstream ss;
    for(size_t j = 0; j < len_limit && j + pos.offset <= corpus.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << pos.add(j, corpus).surface(corpus);
    return ss.str();
  };


  std::vector<std::vector<SrcToken>> queries;
  create_random_queries(tokenIndex, queries, /* num = */ 100000);


  std::random_device rd;
  std::mt19937 gen(rd());

  TokenIndex<SrcToken>::Span fullSpan = tokenIndex.span();

  size_t j = 0, numPrint = 0; // numPrint = 10

  for(auto query : queries) {
    //std::string querySurface = "";
    std::stringstream querySurface;
    for(size_t i = 0; i < query.size(); i++)
      querySurface << (i == 0 ? "" : " ") << vocab.at(query[i]);

    TokenIndex<SrcToken>::Span span = tokenIndex.span();
    for(auto token : query)
      EXPECT_GT(span.narrow(token), 0) << "queries for existing locations must succeed"; // since we just randomly sampled them, they must be in the corpus.

    for(size_t i = 0; i < span.size(); i++)
      EXPECT_EQ(querySurface.str(), surface(span[i], query.size())) << "the entire range must have the same surface prefix as the query";

    // note: out-of-bounds access only works if the underlying SA is not split, and the actually mapped range does not start at beginning/end of SA.

    if(!(span[0] == fullSpan[0]))
      EXPECT_NE(querySurface.str(), surface(span.at_unchecked(-1), query.size())) << "just before the range, there must not be the same surface form";
    else
      EXPECT_EQ("the", vocab.at(query[0])) << "range begins at SA begin, we expect the first word to be 'the'";

    if(!(span[span.size()-1] == fullSpan[fullSpan.size()-1]))
      EXPECT_NE(querySurface.str(), surface(span.at_unchecked(span.size()), query.size())) << "just after the range, there must not be the same surface form";

    if(j < numPrint) {
      std::cerr << "verified span for query '" << querySurface.str() << "', span size = " << span.size() << std::endl;
      if(!(span[0] == fullSpan[0]))
        std::cerr << " surface before: '" << surface(span.at_unchecked(-1), query.size()) << "'" << std::endl;
      if(!(span[span.size()-1] == fullSpan[fullSpan.size()-1]))
        std::cerr << " surface after: '" << surface(span.at_unchecked(span.size()), query.size()) << "'" << std::endl;
      j++;
    }
  }
}

TEST_F(TokenIndexTests, add_to_loaded_eim_small) {
  Vocab<SrcToken> vocab("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<SrcToken> corpus("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct", &vocab);
  TokenIndex<SrcToken> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", corpus); // built with mtt-build

  std::vector<std::string> src = {"magyarul", "egesz", "biztosan", "nem", "tudsz"};
  std::vector<SrcToken> sent;
  for(auto w : src)
    sent.push_back(vocab[w]);
  corpus.AddSentence(sent);

  staticIndex.AddSentence(corpus.sentence(corpus.size()-1));
}

#include "TreeNodeDisk.h"

TEST_F(TokenIndexTests, TreeNodeDisk) {
  // TODO SuffixArray type
  //typedef std::vector<AtomicPosition<SrcToken>> SuffixArray;
  typedef SuffixArrayDisk<SrcToken> SuffixArray;
  // macros and multiple template parameters don't like each other in the C++ parser?!
  std::string csp1 = TreeNodeDisk<SrcToken, SuffixArray>::child_sub_path(0x7a120);
  std::string csp2 = TreeNodeDisk<SrcToken, SuffixArray>::child_sub_path(1);
  EXPECT_EQ("0007a/0007a120", csp1);
  EXPECT_EQ("00000/00000001", csp2);
}
