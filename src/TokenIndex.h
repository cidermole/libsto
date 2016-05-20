/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include "Corpus.h"

namespace sto {

template<class Token> class TokenIndex;

/**
 * IndexSpan represents the matched locations of a partial lookup sequence
 * within TokenIndex.
 *
 * You start with the empty lookup sequence from TokenIndex::span() and
 * keep adding tokens to the lookup via narrow().
 */
template<class Token>
class IndexSpan {
  // TODO: c++11 move constructor
public:
  IndexSpan(TokenIndex<Token> &index);

  /**
   * Narrow the span by adding a token to the end of the lookup sequence.
   * Returns new span size.
   */
  size_t narrow(Token t);

  /** Random access to a position within the selected span. */
  Position<Token> operator[](size_t rel);

  /** Span size, or the number of token positions spanned in the index. */
  size_t size();

private:
  TokenIndex<Token> *index_;
};

/**
 * Indexes a Corpus. The index is implemented as a suffix array.
 */
template<class Token>
class TokenIndex {
public:
  TokenIndex(Corpus<Token> &corpus);

  /** Returns the whole span of the entire index (empty lookup sequence). */
  IndexSpan<Token> span();

  Corpus<Token> *corpus() { return corpus_; }

private:
  Corpus<Token> *corpus_;
};

} // namespace sto

#endif //STO_TOKENINDEX_H
