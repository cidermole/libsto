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
  typedef uint32_t Sid; /** sentence ID type */
  typedef uint8_t Offset; /** type of token offset within sentence */

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

/**
 * Position of a Token within a Corpus. Compares in lexicographic order
 * until the end of sentence.
 */
template<class Token>
class Position {
public:
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Offset Offset;

  Position(Corpus<Token> &corpus, Sid sid, Offset offset);

  bool operator<(const Position<Token> &other);

private:
  Corpus<Token> *corpus_;
  Sid sid_; /** sentence ID */
  Offset offset_; /** offset within sentence */
};

} // namespace sto

#endif //STO_CORPUS_H
