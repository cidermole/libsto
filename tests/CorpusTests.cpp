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
  // TODO: test Sentence::operator[]
}
