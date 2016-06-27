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

  /**
   * Random access to a position within the selected span.
   * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   */
  Position<Token> operator[](size_t rel);

  /**
   * Access to a position within the selected span. As opposed to operator[],
   * this guarantees the order, but is O(V * log(n/k)) with vocabulary size V.
   */
  Position<Token> At(size_t rel);

  /** Number of token positions spanned in the index. */
  size_t size() const;

  /** Length of lookup sequence, or the number of times narrow() has been called. */
  size_t depth() const;

private:
  TokenIndex<Token> *index_;

  // these 3 are only kept for debugging; for bookkeeping, we only need tree_path_.back()
  std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
  std::vector<TreeNode<Token> *> tree_path_; /** first part of path from root through the tree */
  std::vector<Range> array_path_; /** second part of path from leaf through the suffix array. These Ranges always index relative to the specific suffix array. */

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
 * Map from vids to TreeNode children.
 * Additionally carries along partial sums for child sizes.
 */
template<class Token>
class TreeChildMap {
public:
  typedef typename Token::Vid Vid;
  typedef RBTree<Vid, TreeNode<Token> *> ChildMap;
  //typedef typename ChildMap::iterator Iterator;

  TreeChildMap();

  //Iterator begin() { return children_.begin(); }
  //Iterator end() { return children_.end(); }

  bool empty() const { return children_.Count() == 0; }

  /** finds the TreeNode for vid, or inserts a new empty TreeNode. */
  TreeNode<Token> *&operator[](Vid vid) {
    return children_.FindOrInsert(vid, /* add_size = */ 0);
  }

  void AddSize(Vid vid, size_t add_size) {
    children_.AddSize(vid, add_size);
  }

  TreeNode<Token> *&FindOrInsert(Vid vid, size_t add_size) {
    return children_.FindOrInsert(vid, add_size);
  }

  bool Find(const Vid& key, TreeNode<Token> **val = nullptr) const {
    return children_.Find(key, val);
  }

  void Print() {
    children_.Print();
  }

  size_t size() const {
    return children_.Size();
  }

  size_t ChildSize(const Vid& key) const {
    return children_.ChildSize(key);
  }

  /** Walk tree in-order and apply func(key, value) to each node. */
  template<typename Func>
  void Walk(Func func) {
    children_.Walk(func);
  }

  /** returns iterator to specified element, or end() if not found. */
  //Iterator find(Vid vid);

  /** Update the size partial sums of our children after vid. Default is update everything. Returns total sum of children. */
  //size_t UpdateChildSizeSums(Vid vid = Token::kInvalidVid);

  /**
   * random access into the spanned range.
   */
  Position<Token> AtUnordered(size_t offset);

  /**
   * Access to a position within the selected span. As opposed to AtUnordered(),
   * this guarantees the order, but is O(V) with vocabulary size at each node.
   */
  Position<Token> At(size_t offset, const Vocab<Token> &vocab);

private:
  ChildMap children_;
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
   * Insert the existing Corpus Sentence into this index.
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is an
   *
   * O(l * (k + log(n)))
   *
   * operation, with l = sent.size(), k = TreeNode<Token>::kMaxArraySize
   * and n = span().size() aka the full index size.
   *
   * Thread safety: given some memory model assumptions (Total Store Ordering) about
   * the target architecture (x86/x64), writes concurrent to multiple reading threads
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
  //typedef std::map<Vid, TreeNode *> ChildMap;
  typedef TreeChildMap<Token> ChildMap;
  typedef std::vector<Position<Token>> SuffixArray;

  /** Constructs an empty TreeNode, i.e. a leaf with a SuffixArray. */
  TreeNode(size_t maxArraySize = 100000);
  ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return is_leaf_.load(); }

  /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  size_t size() const;

  /** random access
   * in O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   */
  Position<Token> AtUnordered(size_t offset);

  /**
   * Access to a position within the selected span. As opposed to operator[],
   * this guarantees the order, but is O(V) with vocabulary size.
   */
  Position<Token> At(size_t offset, const Vocab<Token> &vocab);

  void DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth = 0);

private:
  std::atomic<bool> is_leaf_; /** whether this is a suffix array (leaf node) */
  ChildMap children_; /** node children, empty if leaf node */
  std::shared_ptr<SuffixArray> array_; /** suffix array, only if is_leaf_ == true */

  /**
   * maximum size of suffix array leaf, larger sizes are split up into TreeNodes.
   * NOTE: the SA leaf of </s> may grow above kMaxArraySize, see AddPosition_() implementation.
   */
  const size_t kMaxArraySize;

  /**
   * Insert the existing Corpus Position into this index.
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is an
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   *
   * Exclusively for adding to a SA (leaf node). Does NOT increment size_.
   *
   * depth: distance of TreeNode from the root of this tree, used in splits
   */
  void AddPosition_(const Sentence<Token> &sent, Offset start, size_t depth);

  /** Call SplitNode() if this node needs to be split. Returns true if it was. */
  bool CheckSplitNode(const Sentence<Token> &sent, Offset start, Offset depth);

  /** Split this leaf node (suffix array) into a proper TreeNode with children.
   * depth: distance of TreeNode from the root of this tree
   * */
  void SplitNode(const Corpus<Token> &corpus, Offset depth);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
