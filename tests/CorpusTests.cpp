/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

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
