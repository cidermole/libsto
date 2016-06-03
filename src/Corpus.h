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

template<class Token> class Sentence;

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
  typedef typename Token::Vid Vid; /** vocabulary ID type */

  /** Create empty corpus */
  Corpus();

  /** Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
  Corpus(const std::string &filename, const Vocab<Token> &vocab);

  /**
   * begin of sentence (points into sequence of vocabulary IDs in corpus track)
   * Has a trailing sentinel so you can query begin(sid+1) for the end) position
   */
  Vid *begin(Sid sid) const;
  // should be friended to Sentence

  // should be friended to Sentence
  //Vocab<Token> *vocab() { return vocab_; }

  Sentence<Token> sentence(Sid sid) const;

private:
  const Vocab<Token> *vocab_;
  std::unique_ptr<MappedFile> track_;     /** mapping starts from beginning of file, includes header */
  std::unique_ptr<MappedFile> sentIndex_; /** mapping starts from index start, *excludes* header */
  Vid *trackTokens_;
  SentIndexEntry *sentIndexEntries_;

  CorpusTrackHeader trackHeader_;
  SentIndexHeader sentIndexHeader_;
};

template<class Token> class Position;

template<class Token>
class Sentence {
public:
  typedef typename Corpus<Token>::Sid Sid;
  friend class Position<Token>;

  Sentence(const Corpus<Token> &corpus, Sid sid);

  Sentence(const Sentence<Token> &other);
  Sentence(const Sentence<Token> &&other);
  // to do: move assignment operator

  /** get Token `i` of this Sentence */
  Token operator[](size_t i) const;

  /** number of tokens */
  size_t size() const { return size_; }

  /** sentence ID */
  Sid sid() const { return sid_; }

  const Corpus<Token> &corpus() const { return *corpus_; }

private:
  typedef typename Token::Vid Vid;

  const Corpus<Token> *corpus_;
  Sid sid_;     /** sentence ID */
  Vid *begin_;  /** corpus track begin of sentence */
  size_t size_; /** number of tokens */
};

/**
 * Position of a Token within a Corpus.
 */
template<class Token>
struct Position {
public:
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Offset Offset;

  Sid sid; /** sentence ID */
  Offset offset; /** offset within sentence */

  /** like operator<(this, other) */
  bool compare(const Position<Token> &other, const Corpus<Token> &corpus) const;
};

} // namespace sto

#endif //STO_CORPUS_H
