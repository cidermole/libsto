/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_CORPUS_H
#define STO_CORPUS_H

#include <vector>
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
  Corpus(const Vocab<Token> &vocab);

  /** Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
  Corpus(const std::string &filename, const Vocab<Token> &vocab);

  /**
   * begin of sentence (points into sequence of vocabulary IDs in corpus track)
   * Has a trailing sentinel so you can query begin(sid+1) for the end) position
   */
  const Vid *begin(Sid sid) const;
  // should be friended to Sentence

  // should be friended to Sentence
  //Vocab<Token> *vocab() { return vocab_; }

  Sentence<Token> sentence(Sid sid) const;

  /** add a sentence to the dynamic part. */
  void AddSentence(const std::vector<Token> &sent);

  const Vocab<Token> &vocab() const { return *vocab_; }

  /** number of sentences */
  Sid size() const;

private:
  const Vocab<Token> *vocab_;
  std::unique_ptr<MappedFile> track_;     /** mapping starts from beginning of file, includes header */
  std::unique_ptr<MappedFile> sentIndex_; /** mapping starts from index start, *excludes* header */
  Vid *trackTokens_;
  SentIndexEntry *sentIndexEntries_;

  CorpusTrackHeader trackHeader_;
  SentIndexHeader sentIndexHeader_;

  std::vector<Vid> dyn_track_; /** dynamic corpus track, located after the last static sentence ID. */
  std::vector<SentIndexEntry> dyn_sentIndex_; /** indexes sentence start positions in dyn_track_, includes trailing sentinel */
};

template<class Token> class Position;

template<class Token>
class Sentence {
public:
  typedef typename Corpus<Token>::Sid Sid;
  friend class Position<Token>;

  /** use Corpus::sentence(sid) instead */
  Sentence(const Corpus<Token> &corpus, Sid sid);

  /** Create invalid Sentence. */
  Sentence();

  Sentence(const Sentence<Token> &other);
  Sentence(const Sentence<Token> &&other);
  Sentence &operator=(const Sentence<Token> &other) = default;

  /** get Token `i` of this Sentence */
  Token operator[](size_t i) const;

  /** number of tokens */
  size_t size() const { return size_; }

  /** sentence ID */
  Sid sid() const { return sid_; }

  const Corpus<Token> &corpus() const { return *corpus_; }

  /** surface form for debugging. */
  std::string surface() const;

private:
  typedef typename Token::Vid Vid;

  const Corpus<Token> *corpus_;
  Sid sid_;     /** sentence ID */
  const Vid *begin_;  /** corpus track begin of sentence */
  size_t size_; /** number of tokens */
};

/**
 * Position of a Token within a Corpus.
 */
template<class Token>
struct Position {
public:
  typedef typename Corpus<Token>::Vid Vid;
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Offset Offset;

  Sid sid; /** sentence ID */
  Offset offset; /** offset within sentence */

  /** like operator<(this, other) */
  bool compare(const Position<Token> &other, const Corpus<Token> &corpus) const;

  /** directly compares (sid,offset) pair */
  bool operator==(const Position& other) const;

  std::string surface(const Corpus<Token> &corpus) const;
  Vid vid(const Corpus<Token> &corpus) const;
};

} // namespace sto

#endif //STO_CORPUS_H
