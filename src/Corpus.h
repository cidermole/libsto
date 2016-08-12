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
#include <atomic>
#include <cstdio>

#include "Vocab.h"
#include "MappedFile.h"
#include "Types.h"
#include "CorpusTypes.h"

namespace sto {

template<class Token> class Sentence;

/**
 * Memory-mapped corpus.
 */
template<class Token>
class Corpus {
public:
  typedef sid_t Sid; /** sentence ID type */
  typedef offset_t Offset; /** type of token offset within sentence */
  typedef typename Token::Vid Vid; /** vocabulary ID type */
  typedef typename Token::Vocabulary Vocabulary; /** Vocab<Token> */

  /** Create empty corpus */
  Corpus(const Vocabulary *vocab = nullptr);

  /** Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
  Corpus(const std::string &filename, const Vocabulary *vocab = nullptr);

  ~Corpus();

  /**Begin of sentence (points into sequence of vocabulary IDs in the corpus track) */
  const Vid *begin(Sid sid) const;
  // should be friended to Sentence

  /**End of sentence (points past the last vocabulary ID of the sentence in the corpus track) */
  const Vid *end(Sid sid) const;
  // should be friended to Sentence

  /** retrieve Sentence, a lightweight reference to a sentence's location. */
  Sentence<Token> sentence(Sid sid) const;

  /**
   * Add a sentence to the dynamic part.
   * If writable (v3), this method will actually append to the corpus on disk.
   */
  void AddSentence(const std::vector<Token> &sent);

  const Vocabulary &vocab() const;

  /** number of sentences */
  Sid size() const;

  /** total number of tokens in the entire corpus */
  size_t numTokens() const;

  /** write out the entire corpus in v3 format. */
  void Write(const std::string &filename);

private:
  const Vocabulary *vocab_;
  std::unique_ptr<MappedFile> track_;     /** mapping starts from beginning of file, includes header */
  std::unique_ptr<MappedFile> sentIndex_; /** mapping starts from index start, *excludes* header */
  Vid *trackTokens_;                      /** static corpus track */
  SentIndexEntry *sentIndexEntries_;      /** indexes sentence start positions in trackTokens_, includes trailing sentinel */
  size_t sentIndexEntrySize_;             /** divide each entry in sentIndexEntries_ by this (hack for byte counts in word alignment, see CorpusIndexAccounting in Types.h) */
  bool writable_;                         /** true for v3 corpus and index: track_ and sentIndex_ are writable */
  FILE *ftrack_ = nullptr;                /** write access to track */
  FILE *findex_ = nullptr;                /** write access to index */

  CorpusTrackHeader trackHeader_;
  SentIndexHeader sentIndexHeader_;

  std::vector<Vid> dyn_track_; /** dynamic corpus track, located after the last static sentence ID. */
  std::vector<SentIndexEntry> dyn_sentIndex_; /** indexes sentence start positions in dyn_track_, includes trailing sentinel */

  /**
   * Append the most recently added dynamic sentence to the v3 corpus on disk.
   * There is (currently) no buffering; the append is guaranteed to be completed by the time we return.
   */
  void WriteSentence();

  /** set sentIndexEntrySize_ according to the index type */
  void init_index_type();
};

template<class Token> class Position;

/**
 * Lightweight reference to a sentence within a Corpus.
 */
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

  /** get Token `i` of this Sentence. Token at i==size() is implicit </s>. */
  Token operator[](size_t i) const;

  /** number of tokens, excluding the implicit </s> at the end. */
  size_t size() const { return size_; }

  /** sentence ID */
  Sid sid() const { return sid_; }

  const Corpus<Token> &corpus() const { return *corpus_; }

  /** surface form for debugging. */
  std::string surface() const;

private:
  typedef typename Token::Vid Vid;

  const Corpus<Token> *corpus_;
  Sid sid_;           /** sentence ID */
  const Vid *begin_;  /** corpus track begin of sentence */
  size_t size_;       /** number of tokens */
};

template<class Token> struct AtomicPosition;

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

  Position() noexcept: sid(0), offset(0) {}
  Position(Sid s, size_t o): sid(s), offset(static_cast<Offset>(o)) {}

  Position(const Position &other) = default;
  Position(Position &&other) = default;

  Position &operator=(Position &&other) = default;
  Position &operator=(const Position &other) = default;

  Position(const AtomicPosition<Token> &atomic_pos) : Position(atomic_pos.load()) {}

  /** like operator>(this, other) */
  bool compare(const Position<Token> &other, const Corpus<Token> &corpus) const;

  /** directly compares (sid,offset) pair */
  bool operator==(const Position& other) const;

  std::string surface(const Corpus<Token> &corpus) const;
  /** vocabulary ID of the token at this corpus position. Used by TokenIndex. */
  Vid vid(const Corpus<Token> &corpus) const;
  Token token(const Corpus<Token> &corpus) const;

  /** add offset. */
  Position add(size_t offset, const Corpus<Token> &corpus) const;
};


/**
 * Atomic Position<Token> wrapper to provide safe concurrent access to individual entries of a vector of these.
 */
template<class Token>
struct AtomicPosition {
  static constexpr std::memory_order order = std::memory_order_relaxed;
  
  AtomicPosition(const Position<Token> &p) {
    pos.store(p, order);
  }
  AtomicPosition(const AtomicPosition<Token> &&p) {
    pos.store(p.pos.load(), order);
  }
  AtomicPosition(const AtomicPosition<Token> &p): pos() {
    pos.store(p.pos.load(), order);
  }
  AtomicPosition<Token> &operator=(const AtomicPosition<Token> &p) {
    pos.store(p.pos.load(), order);
    return *this;
  }
  AtomicPosition<Token> &operator=(AtomicPosition<Token> &&p) {
    pos.store(p.pos.load(), order);
    return *this;
  }

  Position<Token> load() const { return pos.load(order); }

  std::atomic<Position<Token>> pos;
};

/**
 * Comparator for Position<Token> objects.
 */
template<class Token>
struct PosComp {
public:
  /** compares Positions of the given 'corpus', from 'depth' onwards */
  PosComp(const Corpus<Token> &corpus, size_t depth = 0): corpus_(corpus), depth_(depth) {}

  bool operator()(const Position<Token> &a, const Position<Token> &b) {
    typedef typename Corpus<Token>::Offset Offset;
    // for some reason, we defined Position::compare() as an operator >
    return !a.add(static_cast<Offset>(depth_), corpus_).compare(b.add(static_cast<Offset>(depth_), corpus_), corpus_);
  }

private:
  const Corpus<Token> &corpus_;
  size_t depth_;
};

} // namespace sto

#endif //STO_CORPUS_H
