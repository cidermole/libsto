/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include <iostream>
#include <map>
#include <vector>
#include <memory>
#include <atomic>

#include "Corpus.h"
#include "util/rbtree.hpp"

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
  IndexSpan(TokenIndex<Token> &index);

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
  Position<Token> operator[](size_t rel);

  /**
   * Number of token positions spanned in the index.
   *
   * Returns a size cached when narrow() was called.
   */
  size_t size() const;

  /** Length of lookup sequence, or the number of times narrow() has been called. */
  size_t depth() const;

  /** TreeNode at current depth. */
  TreeNode<Token> *node();

  /** first part of path from root through the tree, excluding suffix array range choices */
  const std::vector<TreeNode<Token> *>& tree_path() const { return tree_path_; }

  /** true if span reaches into a suffix array leaf. */
  bool in_array() const;

  /** partial lookup sequence so far, as appended by narrow() */
  const std::vector<Token>& sequence() const { return sequence_; }

private:
  static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

  TokenIndex<Token> *index_;

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

/**
 * Indexes a Corpus. The index is implemented as a hybrid suffix tree/array.
 *
 * Vocab note: explicit sentence end delimiting symbol </s> must be indexed at the very beginning of all vocab symbols (sort order matters, shorter sequences must come first)!
 */
template<class Token>
class TokenIndex {
public:
  friend class IndexSpan<Token>;
  typedef typename Corpus<Token>::Offset Offset;

  /** Constructs an empty TokenIndex, i.e. this does not index the Corpus by itself. */
  TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize = 100000);
  ~TokenIndex();

  /** Returns the whole span of the entire index (empty lookup sequence). */
  IndexSpan<Token> span();

  Corpus<Token> *corpus() { return corpus_; }

  /**
   * Insert the existing Corpus Sentence into this index. Last token must be the EOS symbol </s>.
   *
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is an
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

/**
 * A TreeNode belongs to a TokenIndex and represents a word and its possible
 * suffix extensions.
 *
 * Each leaf is implemented as a suffix array, which itself encodes part of
 * the tree (with potentially arbitrary depth). This helps to keep the RAM
 * size low.
 */
template<class Token>
class TreeNode {
public:
  friend class IndexSpan<Token>;
  friend class TokenIndex<Token>;

  typedef typename Corpus<Token>::Vid Vid;
  typedef typename Corpus<Token>::Offset Offset;
  typedef RBTree<Vid, TreeNode<Token> *> ChildMap;
  typedef std::vector<AtomicPosition<Token>> SuffixArray;

  /** Constructs an empty TreeNode, i.e. a leaf with a SuffixArray. */
  TreeNode(size_t maxArraySize = 100000);
  ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return is_leaf_.load(); }

  /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  size_t size() const;

  /**
   * Access to a position within the selected span
   * in O(log(n/k)) with k = TreeNode<Token>::kMaxArraySize.
   */
  Position<Token> At(size_t offset);

  void DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth = 0);

private:
  std::atomic<bool> is_leaf_; /** whether this is a suffix array (leaf node) */
  ChildMap children_; /** TreeNode children, empty if is_leaf. Additionally carries along partial sums for child sizes. */
  std::shared_ptr<SuffixArray> array_; /** suffix array, only if is_leaf */

  /**
   * maximum size of suffix array leaf, larger sizes are split up into TreeNodes.
   * NOTE: the SA leaf of </s> may grow above kMaxArraySize, see AddPosition() implementation.
   */
  const size_t kMaxArraySize;

  /**
   * Insert the existing Corpus Position into this leaf node (SuffixArray).
   * This potentially splits the SuffixArray into individual TreeNodes,
   * and inserts a Position entry into the suffix array. Hence, it is an
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   *
   * Exclusively for adding to a SA (leaf node).
   *
   * depth: distance of TreeNode from the root of this tree, used in splits
   */
  void AddPosition(const Sentence<Token> &sent, Offset start, size_t depth);

  /** Add an empty leaf node (SuffixArray) as a child. */
  void AddLeaf(Vid vid);

  /** Increase the given vid child's size. */
  void AddSize(Vid vid, size_t add_size);

  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   * depth: distance of TreeNode from the root of this tree
   */
  void SplitNode(const Corpus<Token> &corpus, Offset depth);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
