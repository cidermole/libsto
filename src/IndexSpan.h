/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_INDEXSPAN_H
#define STO_INDEXSPAN_H

#include <cstddef>
#include <vector>

#include "Corpus.h"

namespace sto {

template<class Token> class TokenIndex;
template<class Token> class TreeNode;

struct Range {
  size_t begin;
  size_t end;

  size_t size() const { return end - begin; }
};

/**
 * IndexSpan represents the matched locations of a partial lookup sequence
 * within TokenIndex.
 *
 * You start with the empty lookup sequence from TokenIndex::span() and
 * keep adding tokens to the lookup via narrow().
 */
template<class Token>
class IndexSpan {
public:
  friend class TokenIndex<Token>;

  // use TokenIndex::span() instead
  IndexSpan(const TokenIndex<Token> &index);

  IndexSpan(IndexSpan<Token> &other) = default;
  IndexSpan<Token>& operator=(IndexSpan<Token> &other) = default;

  IndexSpan(IndexSpan<Token> &&other) = default;
  IndexSpan<Token>& operator=(IndexSpan<Token> &&other) = default;

  /**
   * Narrow the span by adding a token to the end of the lookup sequence.
   * Returns new span size.
   * If the token was not found at all, returns zero without modifying the
   * IndexSpan.
   * If an empty SuffixArray leaf was found, returns zero while
   * still modifying the IndexSpan.
   */
  size_t narrow(Token t);

  /**
   * Random access to a position within the selected span.
   * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   *
   * When reading a SuffixArray leaf that is being written to,
   * returned Position values will always be valid, but
   * may be to the left of 'rel'.
   */
  Position<Token> operator[](size_t rel) const;

  // for testing!
  Position<Token> at_unchecked(size_t rel) const;

  /**
   * Number of token positions spanned in the index.
   *
   * Returns a size cached when narrow() was called.
   */
  size_t size() const;

  /** Length of lookup sequence, or the number of times narrow() has been called. */
  size_t depth() const;

  /** Distance from the root in number of TreeNodes. */
  size_t tree_depth() const;

  /** TreeNode at current depth. */
  TreeNode<Token> *node();

  /** first part of path from root through the tree, excluding suffix array range choices */
  const std::vector<TreeNode<Token> *>& tree_path() const { return tree_path_; }

  /** true if span reaches into a suffix array leaf. */
  bool in_array() const;

  /** partial lookup sequence so far, as appended by narrow() */
  const std::vector<Token>& sequence() const { return sequence_; }

private:
  static constexpr size_t STO_NOT_FOUND = static_cast<size_t>(-1);

  const TokenIndex<Token> *index_;

  // these 3 are only kept for debugging; for bookkeeping, we only need tree_path_.back()
  std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
  std::vector<TreeNode<Token> *> tree_path_; /** first part of path from root through the tree */
  std::vector<Range> array_path_; /** second part of path from leaf through the suffix array. These Ranges always index relative to the specific suffix array. */

  /** narrow() in suffix array.
   * returns > 0 on success, NOT_FOUND on failure: for consistency with narrow_tree_() */
  size_t narrow_array_(Token t);

  /** find the bounds of an existing Token or insertion point of a new one */
  Range find_bounds_array_(Token t);

  /** narrow() in tree.
   * returns >= 0 on success, NOT_FOUND on failure. (=0 stepping into empty SuffixArray leaf)
   */
  size_t narrow_tree_(Token t);
};

} // namespace sto

#endif //STO_INDEXSPAN_H
