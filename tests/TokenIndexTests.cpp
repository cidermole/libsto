/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <memory>
#include <random>
#include <sstream>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <type_traits>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include "Vocab.h"
#include "Corpus.h"
#include "TokenIndex.h"
#include "Types.h"
#include "DB.h"

#include "util/Time.h"
#include "util/usage.h"
#include "ITokenIndex.h"


using namespace sto;

template<typename TypeTagT> std::string getBasePath();
template<typename TypeTagT> std::string getCleanBasePath();

/**
 * Test Fixture for testing TokenIndex.
 *
 * NOTE: The template argument TokenIndexType becomes TypeParam in our subclass from Google Test.
 *
 * See <https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md#typed-tests>
 * for a detailed description of typed tests.
 */
template<typename TokenIndexType = TokenIndex<SrcToken>>
struct TokenIndexTests : ::testing::Test {
  typedef typename TokenIndexType::TokenT Token;
  typedef typename TokenIndexType::TypeTagT TypeTag;
  typedef TokenIndexType TokenIndexTypeT;
  typedef typename Corpus<Token>::Vid Vid;

  std::string basePath;
  std::unordered_map<std::string, Vid> vocab; // now from surface forms -> string IDs (for passing into various stuff)
  std::unordered_map<Vid, std::string> id2surface;
  Corpus<Token> corpus;
  std::shared_ptr<DB<Token>> db;
  size_t next_vid = 4;

  void PutVocab(std::string surface) {
    auto id = next_vid;
    if(vocab.find(surface) == vocab.end()) {
      vocab[surface] = id;
      id2surface[id] = surface;
      next_vid++;
    }
  }

  Sentence<Token> AddSentence(const std::vector<std::string> &surface) {
    std::vector<Token> sent;
    for(auto s : surface) {
       // vocabulary insert
      PutVocab(s);
      sent.push_back(vocab[s]);
    }

    corpus.AddSentence(sent);

    // retrieve Sentence from Corpus
    return corpus.sentence(corpus.size() - 1);
  }

  void fill_tree_2level_common_prefix_the(TokenIndexType &tokenIndex);
  void tree_2level_common_prefix_the_m(size_t maxLeafSize);

  TokenIndexTests() : basePath(getCleanBasePath<TypeTag>()), corpus(), db(nullptr) {
    vocab["</s>"] = Vocab<Token>::kEosVid;
    id2surface[Vocab<Token>::kEosVid] = "</s>";
    
    // note: cleaning of basePath needs to happen before tokenIndex construction, hence getCleanBasePath()
    openDatabase();
  }
  virtual ~TokenIndexTests() {
    //removeTestBase(); // comment this for debugging a single test
  }
  void removeTestBase() {
    getCleanBasePath<TypeTag>();
  }
  void openDatabase() {
    // db=nullptr for RAM-based index
    if(basePath != "")
      db.reset(new DB<Token>(basePath));
  }
};

template<typename TypeTagT>
std::string getCleanBasePath() {
  std::string basePath = getBasePath<TypeTagT>();

  using namespace boost::filesystem;
  boost::system::error_code ec;
  path base(basePath);
  remove_all(base, ec); // ensure no leftovers

  return basePath;
}

template<> std::string getBasePath<IndexTypeMemory>() { return ""; }
template<> std::string getBasePath<IndexTypeDisk>() { return "res/TokenIndexTests"; }


typedef ::testing::Types<
    TokenIndex<SrcToken, IndexTypeMemory>,
    TokenIndex<SrcToken, IndexTypeDisk>
> TokenIndexTypes;
TYPED_TEST_CASE(TokenIndexTests, TokenIndexTypes);

TYPED_TEST(TokenIndexTests, get_word) {
  typedef typename TypeParam::TokenT Token;
  Sentence<Token> sentence = this->AddSentence({"this", "is", "an", "example"});
  EXPECT_EQ("this", this->id2surface[sentence[0].vid]) << "retrieving a word from Sentence";
}

TYPED_TEST(TokenIndexTests, add_sentence) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db);
  Sentence<Token> sentence = this->AddSentence({"this", "is", "an", "example"});
  tokenIndex.AddSentence(sentence);
  auto span = tokenIndex.span();
  EXPECT_EQ(4, span.size()) << "the Sentence should have added 4 tokens to the IndexSpan";
}

TYPED_TEST(TokenIndexTests, suffix_array_paper_example) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;
  auto &vocab = this->vocab;

  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    this->PutVocab(s); // vocabulary insert (in this ID order, so sort by vid is intuitive)

  EXPECT_EQ(Vocab<Token>::kEosVid, vocab["</s>"]);
  EXPECT_LT(vocab["dog"], vocab["the"]);

  // '", "'.join(['"'] + 'the dog bit the cat on the mat </s>'.split() + ['"'])
  //                                     0      1      2      3      4      5     6      7
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};
  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db);
  Sentence<Token> sentence = this->AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);
  auto span = tokenIndex.span();
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  // ideas:
  // * add sanity check function for verifying partial sums
  // * add slow AtUnordered() that doesn't use partial sums?
  // * add slow At() which sorts the hash map before access (completely ordered)

  // as long as we're accessing the SA, we are properly ordered.
  //assert(tokenIndex.root_->is_leaf()); // private member...

  //tokenIndex.DebugPrint(std::cerr, this->id2surface);

  Position<Token> pos{/* sid = */ 0, /* offset = */ 2};
  EXPECT_EQ(pos, span[0]) << "verifying token position for 'bit'";
  EXPECT_EQ((Position<Token>{/* sid = */ 0, /* offset = */ 2}), span[0]) << "verifying token position for 'bit'";

  std::vector<size_t>      expect_suffix_array_offset  = {2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], this->id2surface[span[i].vid(this->corpus)]) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }
}

/*
TYPED_TEST(TokenIndexTests, load_v2) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  // 'index.sfa' file built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o index
  //Vocab<Token> sv("res/vocab.tdx");
  Corpus<Token> sc("res/corpus.mct");
  TokenIndex<Token, IndexTypeMemory> staticIndex("res/index.sfa", sc); // built with mtt-build

  TokenIndexType dynamicIndex(this->basePath, sc, this->db); // building dynamically
  dynamicIndex.AddSentence(sc.sentence(0));

  auto staticSpan = staticIndex.span();
  auto dynamicSpan = dynamicIndex.span();

  EXPECT_EQ(staticSpan.size(), dynamicSpan.size()) << "two ways of indexing the same corpus must be equivalent";

  size_t num_pos = staticSpan.size();
  for(size_t i = 0; i < num_pos; i++) {
    EXPECT_EQ(staticSpan[i], dynamicSpan[i]) << "Position entry " << i << " must match between static and dynamic TokenIndex";
  }
}
*/

TYPED_TEST(TokenIndexTests, suffix_array_split) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;
  auto &vocab = this->vocab;

  //                                      1       2      3      4      5      6     7
  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    this->PutVocab(s); // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 7);

  //                                     0      1      2      3      4      5     6      7
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  Sentence<Token> sentence = this->AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);

  // because of maxLeafSize=8, these accesses go via a first TreeNode level.
  // however, the hash function % ensures that our vids are still in order, even though the API doesn't guarantee this.
  // so this is not a good test.

  auto span = tokenIndex.span();
  //tokenIndex.DebugPrint(std::cerr, this->id2surface);
  EXPECT_FALSE(span.in_array()) << "the TreeNode root of TokenIndex should have been split";
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  std::vector<size_t>      expect_suffix_array_offset  = {2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], this->id2surface[span[i].vid(this->corpus)]) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }

  //tokenIndex.DebugPrint(std::cerr, this->id2surface);

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

TYPED_TEST(TokenIndexTests, tree_common_prefix) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    this->PutVocab(s); // vocabulary insert (in this ID order, so sort by vid is intuitive)


  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 7);

  //                                      0      1      2      3      4      5     6      7
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  Sentence<Token> sentence = this->AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2
  std::vector<std::string> sent1_words = {"the", "dog", "bit"};

  Sentence<Token> sentence1 = this->AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);


  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree, this->id2surface);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=11 is_leaf=false
* 'bit' vid=4
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=2]
  * [sid=0 offset=2]
* 'cat' vid=5
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=4]
* 'dog' vid=6
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=1]
  * [sid=0 offset=1]
* 'mat' vid=7
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=7]
* 'on' vid=8
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=5]
* 'the' vid=9
  TreeNode size=4 is_leaf=true
  * [sid=0 offset=3]
  * [sid=1 offset=0]
  * [sid=0 offset=0]
  * [sid=0 offset=6]
)";

  EXPECT_EQ(expected_tree, actual_tree.str()) << "tree structure";
}

template<typename TokenIndexType>
void TokenIndexTests<TokenIndexType>::fill_tree_2level_common_prefix_the(TokenIndexType &tokenIndex) {

  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    this->PutVocab(s); // vocabulary insert (in this ID order, so sort by vid is intuitive)

  //                                      0      1      2      3      4      5     6      7
  std::vector<std::string> sent0_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  Sentence<Token> sentence = this->AddSentence(sent0_words);
  tokenIndex.AddSentence(sentence);

  //                                      0      1      2
  std::vector<std::string> sent1_words = {"the", "dog", "bit"};

  Sentence<Token> sentence1 = this->AddSentence(sent1_words);
  tokenIndex.AddSentence(sentence1);

  // a leaf </s> attached to 'the' which itself should be split:

  std::stringstream nil;
  tokenIndex.DebugPrint(nil, this->id2surface); // print to nowhere, but still run size asserts etc.

  //                                      0
  std::vector<std::string> sent2_words = {"the"};

  Sentence<Token> sentence2 = this->AddSentence(sent2_words);
  tokenIndex.AddSentence(sentence2);

  tokenIndex.DebugPrint(nil, this->id2surface); // print to nowhere, but still run size asserts etc.
}

TYPED_TEST(TokenIndexTests, tree_2level_common_prefix_the) {
  typedef TypeParam TokenIndexType;

  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 4);
  this->fill_tree_2level_common_prefix_the(tokenIndex);

  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree, this->id2surface);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=12 is_leaf=false
* 'bit' vid=4
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=2]
  * [sid=0 offset=2]
* 'cat' vid=5
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=4]
* 'dog' vid=6
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=1]
  * [sid=0 offset=1]
* 'mat' vid=7
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=7]
* 'on' vid=8
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=5]
* 'the' vid=9
  TreeNode size=5 is_leaf=false
  * '</s>' vid=2
    TreeNode size=1 is_leaf=true
    * [sid=2 offset=0]
  * 'cat' vid=5
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=3]
  * 'dog' vid=6
    TreeNode size=2 is_leaf=true
    * [sid=1 offset=0]
    * [sid=0 offset=0]
  * 'mat' vid=7
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=6]
)";

  EXPECT_EQ(expected_tree, actual_tree.str()) << "tree structure";
}

template<typename TokenIndexType>
void TokenIndexTests<TokenIndexType>::tree_2level_common_prefix_the_m(size_t maxLeafSize) {
  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, maxLeafSize);
  fill_tree_2level_common_prefix_the(tokenIndex);

  std::vector<Position<Token>> expected_pos = {
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

  std::vector<Position<Token>> actual_pos;
  auto span = tokenIndex.span();
  // IndexSpan could support iteration...
  for(size_t i = 0; i < span.size(); i++)
    actual_pos.push_back(span[i]);

  EXPECT_EQ(expected_pos, actual_pos) << "flattened suffix array equality with maxLeafSize = " << maxLeafSize;
}


TYPED_TEST(TokenIndexTests, tree_2level_common_prefix_the_4) {
  this->tree_2level_common_prefix_the_m(/* maxLeafSize = */ 4);
}

TYPED_TEST(TokenIndexTests, tree_2level_common_prefix_the_5) {
  // check invariance: maxLeafSize = 4 and maxLeafSize = 5 (without the 'the' split) should behave exactly the same
  this->tree_2level_common_prefix_the_m(/* maxLeafSize = */ 5);
}

TYPED_TEST(TokenIndexTests, tree_2level_common_prefix_the_15) {
  // check invariance: maxLeafSize = 15 (without any split; common SA) should behave exactly the same
  this->tree_2level_common_prefix_the_m(/* maxLeafSize = */ 15);
}


TYPED_TEST(TokenIndexTests, tree_2level_no_eos_leaf_split) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 2);
  this->fill_tree_2level_common_prefix_the(tokenIndex);

  std::vector<std::string> sent3_words = {"the", "mat", "on", "the"};
  Sentence<Token> sentence = this->AddSentence(sent3_words);
  tokenIndex.AddSentence(sentence);

  std::vector<std::string> sent4_words = {"dog", "on", "the"};
  Sentence<Token> sentence4 = this->AddSentence(sent4_words);
  tokenIndex.AddSentence(sentence4);

  /*
   * * 'on' vid=8
   *   TreeNode size=3 is_leaf=false
   *   * 'the' vid=9
   *     TreeNode size=3 is_leaf=true
   *     * [sid=3 offset=2]  <<< shorter sentences come first
   *     * [sid=4 offset=1]
   *     * [sid=0 offset=5]  <<< this is longer
   *
   * So if a leaf does not support splitting...
   */

  // maybe we (incorrectly) do not split if appending a </s> style Position to the beginning?
  // let's see if it is split if we add a longer sentence.

  std::vector<std::string> sent5_words = {"cat", "on", "the", "dog"};
  Sentence<Token> sentence5 = this->AddSentence(sent5_words);
  tokenIndex.AddSentence(sentence5);

  // yep, it was split now:

  /*
   * * 'on' vid=8
   *   TreeNode size=4 is_leaf=false
   *   * 'the' vid=9
   *     TreeNode size=4 is_leaf=false
   *     * '</s>' vid=2
   *       TreeNode size=2 is_leaf=true
   *       * [sid=3 offset=2]
   *       * [sid=4 offset=1]
   *     * 'dog' vid=6
   *       TreeNode size=1 is_leaf=true
   *       * [sid=5 offset=1]
   *     * 'mat' vid=7
   *       TreeNode size=1 is_leaf=true
   *       * [sid=0 offset=5]
   */
  
  // => TODO: allow_split computation is sometimes too strict

  tokenIndex.DebugPrint(std::cerr, this->id2surface);

  std::stringstream actual_tree;
  tokenIndex.DebugPrint(actual_tree, this->id2surface);

  // hardcoding the tree structure in ASCII is pretty crude, I know.
  std::string expected_tree = R"(TreeNode size=12 is_leaf=false
* 'bit' vid=4
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=2]
  * [sid=0 offset=2]
* 'cat' vid=5
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=4]
* 'dog' vid=6
  TreeNode size=2 is_leaf=true
  * [sid=1 offset=1]
  * [sid=0 offset=1]
* 'mat' vid=7
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=7]
* 'on' vid=8
  TreeNode size=1 is_leaf=true
  * [sid=0 offset=5]
* 'the' vid=9
  TreeNode size=5 is_leaf=false
  * '</s>' vid=2
    TreeNode size=1 is_leaf=true
    * [sid=2 offset=0]
  * 'cat' vid=5
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=3]
  * 'dog' vid=6
    TreeNode size=2 is_leaf=true
    * [sid=1 offset=0]
    * [sid=0 offset=0]
  * 'mat' vid=7
    TreeNode size=1 is_leaf=true
    * [sid=0 offset=6]
)";

  EXPECT_EQ(expected_tree, actual_tree.str()) << "tree structure";


  // array is sorted in ascending order
  auto span = tokenIndex.span();
  for(size_t i = 0; i + 1 < span.size(); i++) {
    Position<Token> p = span[i], q = span[i+1];
    //assert(p <= q) == assert(!(p > q)); // equivalent formula if we had > operator
    EXPECT_TRUE(!q.compare(p, this->corpus, /* pos_order_dupes = */ false)); // ascending order (tolerates old v2 mtt-build style order)
    EXPECT_TRUE(!(p == q)); // ensure no duplicates
  }
}


namespace std {
template<>
template<typename Token>
struct hash<Position<Token>> {
  std::size_t operator()(const Position<Token>& pos) const {
    return ((hash<typename Position<Token>::Offset>()(pos.offset))
             ^ (hash<typename Position<Token>::Sid>()(pos.sid) << 8));
  }
};
} // namespace std

/*
TYPED_TEST(TokenIndexTests, static_vs_dynamic_eim) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  //Vocab<Token> sv("/home/david/MMT/engines/default/models/phrase_tables/model.en.tdx");
  Corpus<Token> sc("/home/david/MMT/engines/default/models/phrase_tables/model.en.mct");
  TokenIndex<Token, IndexTypeMemory> staticIndex("/home/david/MMT/engines/default/models/phrase_tables/model.en.sfa", sc); // built with mtt-build

  TokenIndexType dynamicIndex(this->basePath, sc, this->db); // building dynamically

  std::cerr << "building dynamicIndex..." << std::endl;
  for(size_t i = 0; i < sc.size(); i++) {
    if(i % 1000 == 0)
      std::cerr << "dynamicIndex @ AddSentence(i=" << i << ")..." << std::endl;
    dynamicIndex.AddSentence(sc.sentence(i));
  }
  std::cerr << "building dynamicIndex done." << std::endl;

  auto staticSpan = staticIndex.span();
  auto dynamicSpan = dynamicIndex.span();

  EXPECT_EQ(staticSpan.size(), dynamicSpan.size()) << "two ways of indexing the same corpus must be equivalent";

  auto surface = [&](Position<Token> pos){
    std::stringstream ss;
    for(size_t j = 0; j + pos.offset <= sc.sentence(pos.sid).size(); j++)
      ss << (j == 0 ? "" : " ") << this->id2surface[pos.add(j, sc).vid(sc)];
    return ss.str();
  };

  // there is no equality position by position (sort stability?)
  // however, for each surface form, there must be equality among their positions

  size_t numPos = staticSpan.size();
  std::unordered_set<Position<Token>> staticBucket, dynamicBucket;
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
*/

TYPED_TEST(TokenIndexTests, test_persistence) {
  typedef typename TypeParam::TokenT Token;
  typedef TokenIndex<Token, IndexTypeDisk> TokenIndexType;

  // run this test only on IndexTypeDisk
  if(!std::is_same<typename TypeParam::TypeTagT, IndexTypeDisk>::value)
    return;

  // for now, like suffix_array_split
  auto &vocab = this->vocab;

  std::vector<std::string> vocab_id_order{"</s>", "bit", "cat", "dog", "mat", "on", "the"};
  for(auto s : vocab_id_order)
    this->PutVocab(s); // vocabulary insert (in this ID order, so sort by vid is intuitive)

  //                                     0      1      2      3      4      5     6      7
  std::vector<std::string> sent_words = {"the", "dog", "bit", "the", "cat", "on", "the", "mat"};

  //{
  TokenIndexType tokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 7);

  Sentence<Token> sentence = this->AddSentence(sent_words);
  tokenIndex.AddSentence(sentence);

  // tokenIndex goes out of scope, DB is closed etc.
  //}

  // use the updates directly from memory
  TokenIndexType& loadedTokenIndex = tokenIndex;

  // load persisted index from disk/DB
  //TokenIndexType loadedTokenIndex(this->basePath, this->corpus, this->db, /* maxLeafSize = */ 7);

  // because of maxLeafSize=8, these accesses go via a first TreeNode level.
  // however, the hash function % ensures that our vids are still in order, even though the API doesn't guarantee this.
  // so this is not a good test.

  auto span = loadedTokenIndex.span();
  loadedTokenIndex.DebugPrint(std::cerr, this->id2surface);
  EXPECT_FALSE(span.in_array()) << "the TreeNode root of TokenIndex should have been split";
  EXPECT_EQ(sent_words.size(), span.size()) << "the Sentence should have added its tokens to the IndexSpan";

  std::vector<size_t>      expect_suffix_array_offset  = {2,     4,     1,     7,     5,    3,     0,     6 };
  std::vector<std::string> expect_suffix_array_surface = {"bit", "cat", "dog", "mat", "on", "the", "the", "the"};

  for(size_t i = 0; i < expect_suffix_array_surface.size(); i++) {
    EXPECT_EQ(expect_suffix_array_surface[i], this->id2surface[span[i].vid(this->corpus)]) << "verifying surface @ SA position " << i;
    EXPECT_EQ(expect_suffix_array_offset[i], span[i].offset) << "verifying offset @ SA position " << i;
  }

  //loadedTokenIndex.DebugPrint(std::cerr, this->id2surface);

  span.narrow(vocab["bit"]);
  EXPECT_EQ(1, span.size()) << "'bit' range size check";
  span.narrow(vocab["the"]);
  EXPECT_EQ(1, span.size()) << "'bit the' range size check";
  size_t newsz = span.narrow(vocab["dog"]);
  EXPECT_EQ(0, newsz) << "'bit the dog' must be size 0, i.e. not found";

  EXPECT_EQ(1, span.size()) << "failed call must not narrow the span";
  EXPECT_EQ(1, span.narrow(vocab["cat"])) << "IndexSpan must now behave as if the failed narrow() call had not happened";

  span = loadedTokenIndex.span();
  EXPECT_EQ(3, span.narrow(vocab["the"])) << "'the' range size check";
  EXPECT_EQ(1, span.narrow(vocab["cat"])) << "'the' range size check";
  EXPECT_EQ(1, span.size()) << "span size";
}

/*
template<class Token, class SuffixArray>
class BreakLeafMerger : public LeafMerger<Token, SuffixArray> {
public:
  virtual std::shared_ptr<SuffixArray> MergeLeafArray(std::shared_ptr<SuffixArray> curSpan, const ITokenIndexSpan<Token> &addSpan) override {
    Corpus<Token> &corpus = *addSpan.corpus();
    size_t newSize = curSpan->size() + addSpan.size();
    std::shared_ptr<SuffixArray> newArray = std::make_shared<SuffixArray>(newSize);
    return newArray; // breaks the test by returning a zero-initialized SuffixArray
  }
};
 */

TYPED_TEST(TokenIndexTests, test_merge) {
  typedef TypeParam TokenIndexType;
  typedef typename TypeParam::TokenT Token;

  TokenIndex<Token, IndexTypeMemory> tokenIndex(this->corpus);
  //BreakLeafMerger<Token, typename TokenIndexType::SuffixArray> breaker;

  TokenIndexType indexTarget(this->basePath, this->corpus, this->db);

  Sentence<Token> sentence = this->AddSentence({"this", "is", "an", "example"});

  tokenIndex.AddSentence(sentence);
  indexTarget.Merge(tokenIndex); // merge of 'sentence' into empty TokenIndex
  //indexTarget.Merge(tokenIndex, breaker); // merge of 'sentence' into empty TokenIndex
  //indexTarget.AddSentence(sentence);

  auto span = indexTarget.span();
  EXPECT_EQ(4, span.size()) << "the Sentence should have added 4 tokens to the IndexSpan";

  auto refSpan = tokenIndex.span();
  for(size_t i = 0; i < span.size(); i++) {
    EXPECT_EQ(refSpan[i], span[i]) << "index entry at i=" << i << " should be equal to reference";
  }


  Sentence<Token> sent2 = this->AddSentence({"this", "is", "not", "an", "example"});
  TokenIndex<Token, IndexTypeMemory> index2(this->corpus);
  tokenIndex.AddSentence(sent2);
  //indexTarget.AddSentence(sent2);
  index2.AddSentence(sent2);
  indexTarget.Merge(index2);

  EXPECT_EQ(9, indexTarget.span().size());

  // this check happens to work because equal positions get appended in both cases, so the order is stable
  // otherwise, we would have to check buckets
  auto refSpan2 = tokenIndex.span();
  span = indexTarget.span();
  EXPECT_EQ(refSpan2.size(), span.size());
  for(size_t i = 0; i < span.size(); i++) {
    EXPECT_EQ(refSpan2[i], span[i]) << "index entry at i=" << i << " should be equal to reference";
  }
}

TYPED_TEST(TokenIndexTests, test_merge_duplicates) {
  typedef typename TypeParam::TokenT Token;

  // run this test only on IndexTypeDisk
  if(!std::is_same<typename TypeParam::TypeTagT, IndexTypeDisk>::value)
    return;

  TokenIndex<Token, IndexTypeMemory> tokenIndex(this->corpus);

  TokenIndex<Token, IndexTypeDisk> indexDisk(this->basePath, this->corpus, this->db);

  Sentence<Token> sentence = this->AddSentence({"this", "is", "an", "example"});

  tokenIndex.AddSentence(sentence);
  //indexDisk.Merge(tokenIndex); // merge of 'sentence' into empty TokenIndex
  indexDisk.AddSentence(sentence);

  size_t j = 1;
  do {
    auto span = indexDisk.span();
    EXPECT_EQ(4, span.size()) << "the Sentence should have added 4 tokens to the IndexSpan";

    auto refSpan = tokenIndex.span();
    for (size_t i = 0; i < span.size(); i++) {
      EXPECT_EQ(refSpan[i], span[i]) << "index entry at i=" << i << " should be equal to reference";
    }

    // repeated AddSentence() should merge, avoiding duplicates -- even if seqNum is newer!
    indexDisk.AddSentence(sentence, /* seqNum = */ sto_updateid_t{kInvalidStream, j + 1});
  } while(j++ < 2);
}
