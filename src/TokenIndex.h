/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include <iostream>
#include <vector>

#include "IndexSpan.h"
#include "Corpus.h"
#include "util/rbtree.hpp"

namespace sto {

template<class Token> class TreeNode;

/**
 * Indexes a Corpus. The index is implemented as a hybrid suffix tree/array.
 *
 * Vocab note: vid of explicit sentence end delimiting symbol </s> must be at the very beginning of all vocab symbols
 * (sort order matters, shorter sequences must come first)!
 */
template<class Token>
class TokenIndex {
public:
  friend class IndexSpan<Token>;
  typedef typename Corpus<Token>::Offset Offset;

  /**
   * Load TokenIndex from mtt-build *.sfa file for the associated corpus.
   *
   * Currently, this creates one big suffix array (not memory mapped) regardless of maxLeafSize, but subsequent calls to
   * AddSentence() will honor maxLeafSize and split where necessary (so the first additions will be expensive).
   */
  TokenIndex(const std::string &filename, Corpus<Token> &corpus, size_t maxLeafSize = 10000);

  /** Construct an empty TokenIndex, i.e. this does not index the Corpus by itself. */
  TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize = 10000);
  ~TokenIndex();

  /** Returns the whole span of the entire index (empty lookup sequence). */
  IndexSpan<Token> span() const;

  Corpus<Token> *corpus() const { return corpus_; }

  /**
   * Insert the existing Corpus Sentence into this index. Last token must be the EOS symbol </s>.
   *
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is roughly an
   *
   * O(l * (k + log(n)))
   *
   * operation, with l = sent.size(), k = TreeNode<Token>::kMaxArraySize
   * and n = span().size() aka the full index size.
   *
   * Thread safety: writes concurrent to multiple reading threads
   * do not result in invalid state being read.
   */
  void AddSentence(const Sentence<Token> &sent);

  void DebugPrint(std::ostream &os);

private:
  Corpus<Token> *corpus_;
  TreeNode<Token> *root_; /** root of the index tree */

  /** Insert the subsequence from start into this index. Potentially splits. */
  void AddSubsequence_(const Sentence<Token> &sent, Offset start);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
