/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_CORPUS_H
#define STO_CORPUS_H

#include <string>
#include <memory>

#include "Vocab.h"
#include "MappedFile.h"
#include "CorpusTypes.h"

namespace sto {

/**
 * Memory-mapped corpus.
 *
 * Read-only for now.
 */
template<class Token>
class Corpus {
public:
  /** Create empty corpus */
  Corpus();

  /** Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
  Corpus(const std::string &filename, const Vocab<Token> &vocab);

private:
  const Vocab<Token> *vocab_;
  std::unique_ptr<MappedFile> track_;     /** mapping starts from beginning of file, includes header */
  std::unique_ptr<MappedFile> sentIndex_; /** mapping starts from index start, *excludes* header */

  CorpusTrackHeader trackHeader_;
  SentIndexHeader sentIndexHeader_;
};

} // namespace sto

#endif //STO_CORPUS_H
