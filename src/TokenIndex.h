/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include <unordered_map>
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
  // TODO: c++11 move constructor, move assignment
public:
  friend class TokenIndex<Token>;

  // use TokenIndex::span() instead
  IndexSpan(TokenIndex<Token> &index);

  /**
   * Narrow the span by adding a token to the end of the lookup sequence.
   * Returns new span size.
   * If the token was not found at all, returns zero without modifying the
   * IndexSpan.
   */
  size_t narrow(Token t);

  /** Random access to a position within the selected span. */
  Position<Token> operator[](size_t rel);

  /** Number of token positions spanned in the index. */
  size_t size() const;

private:
  TokenIndex<Token> *index_;

  // these 3 are only kept for debugging; for bookkeeping, we only need tree_path_.back()
  std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
  std::vector<TreeNode<Token> *> tree_path_; /** first part of path from root through the tree */
  std::vector<Range> array_path_; /** second part of path from leaf through the suffix array */

  /** narrow() in suffix array. */
  size_t narrow_array_(Token t);

  /** find the bounds of an existing Token or insertion point of a new one */
  Range find_bounds_array_(Token t);

  /** narrow() in tree. */
  size_t narrow_tree_(Token t);

  /** true if span reaches into a suffix array leaf. */
  bool in_array_() const;
};

/**
 * Indexes a Corpus. The index is implemented as a hybrid suffix tree/array.
 *
 * How does a subsequence end at an internal tree node?
 * (implicit: if tree node's count is larger than the sum of its children)
 * should we add explicit </s> sentinel values?
 */
template<class Token>
class TokenIndex {
public:
  friend class IndexSpan<Token>;
  typedef typename Corpus<Token>::Offset Offset;

  TokenIndex(Corpus<Token> &corpus);
  ~TokenIndex();

  /** Returns the whole span of the entire index (empty lookup sequence). */
  IndexSpan<Token> span();

  Corpus<Token> *corpus() { return corpus_; }

  /**
   * Insert the existing Corpus Sentence into this index.
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is an
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   * (TODO) fix the formula
   */
  void AddSentence(const Sentence<Token> &sent);

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

  static constexpr size_t kMaxArraySize = 100000; /** maximum size of suffix array leaf, larger sizes are split up into TreeNodes. */

  /** Constructs an empty TreeNode, i.e. a leaf with a SuffixArray. */
  TreeNode();
  ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return children_.empty(); }

  /** Number of token positions in the index. */
  size_t size() const { return size_; }

private:
  std::unordered_map<Vid, TreeNode *> children_; /** node children, empty if leaf node */
  std::vector<Position<Token>> array_; /** suffix array, only if children_.empty() */
  size_t size_; /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */

  /**
   * Insert the existing Corpus Position into this index.
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is an
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   *
   * Exclusively for adding to a SA (leaf node). Does NOT increment size_.
   */
  void AddPosition_(const Sentence<Token> &sent, Offset start);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
