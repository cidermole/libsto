/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TYPES_H
#define STO_TYPES_H

#include <cstdint>

namespace sto {

template<class Token> class Vocab;
template<class Token> class DummyVocab;

/** Vocabulary ID type for internal use. The external interface is SrcToken/TrgToken. */
typedef uint32_t vid_t;

typedef uint32_t sid_t; /** sentence ID type */
typedef uint8_t offset_t; /** type of token offset within sentence */

struct SrcToken {
  typedef Vocab<SrcToken> Vocabulary;
  typedef vid_t Vid; /** vocabulary ID type */
  static constexpr Vid kInvalidVid = 0;

  Vid vid; /** vocabulary ID */

  /** construct invalid token */
  constexpr SrcToken(): vid(0) {}

  SrcToken(Vid v): vid(v) {}

  bool operator==(const SrcToken &other) const { return vid == other.vid; }
  bool operator!=(const SrcToken &other) const { return vid != other.vid; }
  bool operator<(const SrcToken &other) const { return vid < other.vid; }

  // these two make us iterable
  SrcToken &operator++() { ++vid; return *this; }
  Vid &operator*() { return vid; }
};

struct TrgToken {
  typedef Vocab<TrgToken> Vocabulary;
  typedef vid_t Vid; /** vocabulary ID type */
  static constexpr Vid kInvalidVid = 0;

  Vid vid; /** vocabulary ID */

  /** construct invalid token */
  constexpr TrgToken(): vid(0) {}

  TrgToken(Vid v): vid(v) {}

  bool operator==(const TrgToken &other) const { return vid == other.vid; }
  bool operator!=(const TrgToken &other) const { return vid != other.vid; }
  bool operator<(const TrgToken &other) const { return vid < other.vid; }

  // these two make us iterable
  TrgToken &operator++() { ++vid; return *this; }
  Vid &operator*() { return vid; }
};

/** Link type for internal use. The external interface is AlignmentLink. */
struct aln_link_t {
  offset_t src; /** token offset in source sentence */
  offset_t trg; /** token offset in target sentence */

  aln_link_t(offset_t s, offset_t t): src(s), trg(t) {}

  // these are used in tests
  bool operator==(const aln_link_t &other) const { return src == other.src && trg == other.trg; }
  bool operator!=(const aln_link_t &other) const { return src != other.src || trg != other.trg; }
};

/**
 * Alignment link compatible with Corpus and Sentence,
 * so we can load and handle word alignments with the same implementation.
 */
struct AlignmentLink {
  typedef DummyVocab<AlignmentLink> Vocabulary;
  typedef aln_link_t Vid; /** vocabulary ID type */
  static constexpr offset_t kInvalidOffset = static_cast<offset_t>(-1);

  Vid vid; /** alignment link (called Vid for compatibility with the remaining Corpus/Sentence implementation.) */

  /** construct invalid link */
  AlignmentLink(): vid(kInvalidOffset, kInvalidOffset) {}

  AlignmentLink(Vid v): vid(v) {}

  AlignmentLink(offset_t src_offset, offset_t trg_offset): vid(src_offset, trg_offset) {}

  // these are used in tests
  bool operator==(const AlignmentLink &other) const { return vid == other.vid; }
  bool operator!=(const AlignmentLink &other) const { return vid != other.vid; }
};


} // namespace sto

#endif //STO_TYPES_H
