/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include "Vocab.h"
#include "Corpus.h"
#include "Types.h"

using namespace sto;

TEST(CorpusTests, load_v2) {
  // files built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o corpus
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc("res/corpus.mct", &sv);
}

TEST(CorpusTests, empty_add) {
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc(&sv);

  std::vector<std::string> surface = {"orange", "apple", "and", "pear"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv.at(s)); // vocabulary lookup

  EXPECT_EQ(0, sc.size()) << "empty Corpus must have size() == 0";
  sc.AddSentence(sentence);
  EXPECT_EQ(1, sc.size()) << "after adding single Sentence, Corpus must have size() == 1";

  // retrieve Sentence from Corpus
  Sentence<SrcToken> sent = sc.sentence(0);
  EXPECT_EQ("orange apple and pear", sent.surface()) << "ability to retrieve a dyn stored Sentence";
}

static void remove_all(const std::string &p) {
  using namespace boost::filesystem;
  boost::system::error_code ec;
  path base(p);
  boost::filesystem::remove_all(base, ec); // ensure no leftovers
}

TEST(CorpusTests, write_read_append) {
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc(&sv);

  std::vector<std::string> surface = {"orange", "apple", "and", "pear"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv.at(s)); // vocabulary lookup

  EXPECT_EQ(0, sc.size()) << "empty Corpus must have size() == 0";
  sc.AddSentence(sentence);
  EXPECT_EQ(1, sc.size()) << "after adding single Sentence, Corpus must have size() == 1";

  // retrieve Sentence from Corpus
  Sentence<SrcToken> sent = sc.sentence(0);
  EXPECT_EQ("orange apple and pear", sent.surface()) << "ability to retrieve a dyn stored Sentence";

  remove_all("res/CorpusTests");
  boost::filesystem::create_directory("res/CorpusTests");

  sc.Write("res/CorpusTests/write_read_append.trk");


  {
    // load corpus from disk
    Corpus<SrcToken> loaded("res/CorpusTests/write_read_append.trk", &sv);

    // retrieve Sentence from Corpus
    EXPECT_EQ(1, loaded.size()) << "after loading from disk, Corpus must have size() == 1";
    Sentence<SrcToken> sent2 = loaded.sentence(0);
    EXPECT_EQ("orange apple and pear", sent2.surface()) << "ability to retrieve a dyn stored Sentence from disk";


    // this corpus now supports appending
    std::vector<std::string> surface2 = {"the", "orange", "fox", "painted", "grey", "by", "the", "hazy", "fog"};
    std::vector<SrcToken> sentence2;
    for(auto s : surface2)
      sentence2.push_back(sv[s]); // vocabulary insert
    loaded.AddSentence(sentence2);

    // for binary comparison, write in a different way as well:
    //loaded.Write("res/CorpusTests/ref.trk"); // note: will not currently diff correctly, because of uninitialized trailing bytes in structs
  } // Corpus loaded; goes out of scope here -> file closed

  // load corpus from disk
  Corpus<SrcToken> loaded2("res/CorpusTests/write_read_append.trk", &sv);

  // retrieve Sentence from Corpus
  EXPECT_EQ(2, loaded2.size()) << "after 2nd loading from disk, Corpus must have size() == 2";
  Sentence<SrcToken> sent3 = loaded2.sentence(1);
  EXPECT_EQ("the orange fox painted grey by the hazy fog", sent3.surface()) << "ability to retrieve an appended Sentence from disk";


  //remove_all("res/CorpusTests"); // clean up
}


TEST(CorpusTests, sentence_index_operator) {
  Vocab<SrcToken> sv;
  Corpus<SrcToken> sc(&sv);

  std::vector<std::string> surface = {"this", "is", "an", "example"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv[s]); // vocabulary insert/lookup

  sc.AddSentence(sentence);

  // retrieve Sentence from Corpus
  Sentence<SrcToken> sent = sc.sentence(0);
  EXPECT_EQ("is", sv[sent[1]]) << "proper working of Sentence::operator[]()";
}

TEST(CorpusTests, word_alignment_corpus) {
  Corpus<AlignmentLink> ac;

  std::vector<AlignmentLink> links = {{0,0}, {0,1}, {3,4}};

  ac.AddSentence(links);

  // retrieve Sentence from Corpus
  Sentence<AlignmentLink> sent = ac.sentence(0);
  EXPECT_EQ(AlignmentLink(3,4), sent[2]) << "proper working of Sentence::operator[]()";
}

TEST(CorpusTests, word_alignment_load) {
  assert(sizeof(aln_link_t) == 2); // important for loading existing v2 mmsapt word alignment

  // file built like this:
  // $ echo "0-0 0-1 3-4" | symal2mam align.mam
  Corpus<AlignmentLink> ac("res/align.mam");

  std::vector<AlignmentLink> expected_links = {{0,0}, {0,1}, {3,4}};

  // retrieve Sentence from Corpus
  Sentence<AlignmentLink> sent = ac.sentence(0);

  EXPECT_EQ(expected_links.size(), sent.size()) << "number of alignment links in Sentence(0)";

  for(size_t i = 0; i < expected_links.size(); i++)
    EXPECT_EQ(expected_links[i], sent[i]) << "correct entry in loaded word alignment, offset " << i;
}

// like write_read_append for word alignment
TEST(CorpusTests, alignment_write_read_append) {
  Corpus<AlignmentLink> sc;

  std::vector<AlignmentLink> links = {{0,0}, {0,1}, {3,4}};

  EXPECT_EQ(0, sc.size()) << "empty Corpus must have size() == 0";
  sc.AddSentence(links);
  EXPECT_EQ(1, sc.size()) << "after adding single Sentence, Corpus must have size() == 1";

  // retrieve Sentence from Corpus
  Sentence<AlignmentLink> sent = sc.sentence(0);

  EXPECT_EQ(links.size(), sent.size()) << "should have the same number of alignment links";
  for(size_t i = 0; i < links.size(); i++)
    EXPECT_EQ(links[i], sent[i]) << "correct entry in word alignment, offset " << i;

  remove_all("res/CorpusTests");
  boost::filesystem::create_directory("res/CorpusTests");

  sc.Write("res/CorpusTests/alignment_write_read_append.trk");

  std::vector<AlignmentLink> links2 = {{0,3}, {2,5}, {3,1}, {4,4}};


  {
    // load corpus from disk
    Corpus<AlignmentLink> loaded("res/CorpusTests/alignment_write_read_append.trk");

    // retrieve Sentence from Corpus
    EXPECT_EQ(1, loaded.size()) << "after loading from disk, Corpus must have size() == 1";

    Sentence<AlignmentLink> sent2 = loaded.sentence(0);
    EXPECT_EQ(links.size(), sent2.size()) << "should have the same number of alignment links";
    for(size_t i = 0; i < links.size(); i++)
      EXPECT_EQ(links[i], sent2[i]) << "correct entry in v3 word alignment loaded from disk, offset " << i;


    // this corpus now supports appending
    loaded.AddSentence(links2);

    // for binary comparison, write in a different way as well:
    //loaded.Write("res/CorpusTests/ref.trk"); // note: will not currently diff correctly, because of uninitialized trailing bytes in structs


    // check in-memory retainment of disk-persistent Corpus
    EXPECT_EQ(2, loaded.size()) << "after 2nd append, Corpus must have size() == 2";

    Sentence<AlignmentLink> sent3x = loaded.sentence(1);
    EXPECT_EQ(links2.size(), sent3x.size()) << "should have the same number of alignment links";
    for(size_t i = 0; i < links2.size(); i++)
      EXPECT_EQ(links2[i], sent3x[i]) << "correct entry in v3 word alignment append, offset " << i;



  } // Corpus loaded; goes out of scope here -> file closed

  // load corpus from disk
  Corpus<AlignmentLink> loaded2("res/CorpusTests/alignment_write_read_append.trk");

  // retrieve Sentence from Corpus
  EXPECT_EQ(2, loaded2.size()) << "after 2nd loading from disk, Corpus must have size() == 2";

  Sentence<AlignmentLink> sent3 = loaded2.sentence(1);
  EXPECT_EQ(links2.size(), sent3.size()) << "should have the same number of alignment links";
  for(size_t i = 0; i < links2.size(); i++)
    EXPECT_EQ(links2[i], sent3[i]) << "correct entry in v3 word alignment loaded from disk, offset " << i;

  //remove_all("res/CorpusTests"); // clean up
}

/** constraint that has to be met by operator< */
TEST(CorpusTests, operator_less_position_equality) {
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> corpus(&sv);

  std::vector<std::string> surface = {"orange", "apple", "and", "pear"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv.at(s)); // vocabulary lookup
  corpus.AddSentence(sentence);

  // comparing apples and apples
  Position<SrcToken> apple{0, 1};
  EXPECT_EQ("apple", apple.surface(corpus));

  PosComp<SrcToken> comp(corpus, 0);
  EXPECT_FALSE(comp(apple, apple)) << "comp(a, b) means a < b. EQUALITY must mean LESS is false, since !(a < b) && !(b < a)  <=>  (a == b)";
}

/** constraints that have to be met by operator< */
TEST(CorpusTests, operator_less_orderings) {
  Vocab<SrcToken> sv;
  Corpus<SrcToken> corpus(&sv);

  // insert vids in this order
  sv["and"];
  sv["apple"];
  sv["orange"];
  sv["pear"];

  std::vector<std::string> surface = {"orange", "apple", "and", "pear"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv.at(s)); // vocabulary lookup
  corpus.AddSentence(sentence);

  // comparing apples and oranges
  Position<SrcToken> apple{0, 1};
  Position<SrcToken> orange{0, 0};
  Position<SrcToken> pear{0, 3};

  // true statements about our test setup
  EXPECT_EQ("apple", apple.surface(corpus));
  EXPECT_EQ("orange", orange.surface(corpus));
  EXPECT_TRUE(apple.vid(corpus) < orange.vid(corpus));
  EXPECT_TRUE(apple.vid(corpus) < pear.vid(corpus));

  // testing PosComp
  PosComp<SrcToken> comp(corpus, 0);

  EXPECT_TRUE(comp(apple, orange)) << "comp(a, b) means a < b.  (apple < orange) expected.  plain single-vid ordering test.";


  std::vector<std::string> surface2 = {"apple", "and", "apple"};
  std::vector<SrcToken> sentence2;
  for(auto s : surface2)
    sentence2.push_back(sv.at(s)); // vocabulary lookup
  corpus.AddSentence(sentence2);

  Position<SrcToken> apple_and_apple{1, 0};
  Position<SrcToken> apple_and_pear{0, 1};

  // apple and apple < apple and pear
  EXPECT_TRUE(comp(apple_and_apple, apple_and_pear)) << "comp(a, b) means a < b.  (apple and apple < apple and pear) expected.  ordering at depth=2 test.";

  Position<SrcToken> apple_eos{1, 2};

  // apple </s> < apple and pear </s>
  EXPECT_TRUE(comp(apple_eos, apple_and_pear)) << "comp(a, b) means a < b.  (apple </s> < apple and ...) expected.  EOS and shorter-sequence ordering test.";
}
