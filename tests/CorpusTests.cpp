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

TEST(Corpus, load_v2) {
  // files built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o corpus
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc("res/corpus.mct", sv);
}

TEST(Corpus, empty_add) {
  // files built like this:
  // $ echo "apple and orange and pear and apple and orange" | mtt-build -i -o corpus
  Vocab<SrcToken> sv("res/vocab.tdx");
  Corpus<SrcToken> sc(sv);

  std::vector<std::string> surface = {"orange", "apple", "and", "pear"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv.at(s)); // vocabulary lookup

  sc.AddSentence(sentence);

  // retrieve Sentence from Corpus
  Sentence<SrcToken> sent = sc.sentence(0);
  EXPECT_EQ(sent.surface(), "orange apple and pear") << "ability to retrieve a dyn stored Sentence";
}

TEST(Corpus, sentence_index_operator) {
  Vocab<SrcToken> sv;
  Corpus<SrcToken> sc(sv);

  std::vector<std::string> surface = {"this", "is", "an", "example"};
  std::vector<SrcToken> sentence;
  for(auto s : surface)
    sentence.push_back(sv[s]); // vocabulary insert/lookup

  sc.AddSentence(sentence);

  // retrieve Sentence from Corpus
  Sentence<SrcToken> sent = sc.sentence(0);
  EXPECT_EQ(sv[sent[1]], "is") << "proper working of Sentence::operator[]()";
}
