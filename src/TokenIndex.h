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

#include "Corpus.h"
#include "qhashmap.hpp"

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
  typedef typename TreeNode<Token>::ChildMap::Iterator TreeNodeChildMapIter;
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
   * WARNING: order depends on underlying hash maps.
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

  /** called by narrow() for each TreeNode visited. Override as needed. */
  virtual void tree_node_visit_(TreeNode<Token> &node, TreeNodeChildMapIter child) {}

  /** narrow() in suffix array. */
  size_t narrow_array_(Token t);

  /** find the bounds of an existing Token or insertion point of a new one */
  Range find_bounds_array_(Token t);

  /** narrow() in tree. */
  size_t narrow_tree_(Token t);

  /** true if span reaches into a suffix array leaf. */
  bool in_array_() const;
};

template<class Token>
struct TreeChildMapKeyTraits {
  typedef typename Token::Vid Vid;

  static unsigned hash(Vid vid, size_t capacity_) {
    return vid % capacity_;
  }

  static bool equals(Vid a, Vid b) {
    return a == b;
  }

  static constexpr Vid null() {
    return Token().vid; // invalid Token VID
  }
};

/**
 * Map from vids to TreeNode children.
 * Additionally carries along partial sums for child sizes.
 */
template<class Token>
class TreeChildMap {
public:
  typedef typename Token::Vid Vid;
  typedef QHashMap<Vid, TreeNode<Token> *, size_t, TreeChildMapKeyTraits<Token>> ChildMap;
  //typedef TreeChildMapIterator<Token, typename ChildMap::Entry> Iterator;
  typedef typename ChildMap::iterator Iterator;

  TreeChildMap();

  Iterator begin() { return children_.begin(); }
  Iterator end() { return children_.end(); }

  bool empty() const { return children_.size() == 0; }

  TreeNode<Token> *&operator[](Vid vid);

  /** returns iterator to specified element, or end() if not found. */
  Iterator find(Vid vid);

  /** Update the size partial sums of our children after vid. Default is update everything. Returns total sum of children. */
  size_t UpdateChildSizeSums(Vid vid = Token::kInvalidVid);

  /**
   * random access into the spanned range.
   * WARNING: the order of our children depends on the population order
   * of the underlying hash map.
   */
  Position<Token> AtUnordered(size_t offset);

  /**
   * Access to a position within the selected span. As opposed to AtUnordered(),
   * this guarantees the order, but is O(V) with vocabulary size at each node.
   */
  Position<Token> At(size_t offset, const Vocab<Token> &vocab);

private:
  typedef typename ChildMap::Entry Entry;

  ChildMap children_;

  /**
   * Obtains the child containing the position at 'offset' and the
   * relative offset into that child.
   * WARNING: the order of our children depends on the population order
   * of the underlying hash map.
   * Returns values in (vid, child_offset).   // if child_offset == offset, then we're at a leaf and vid was not written.
   */
  void FindBoundUnordered(size_t offset, Vid &vid, size_t &child_offset);
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
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   * (TODO) fix the formula
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

  /** Constructs an empty TreeNode, i.e. a leaf with a SuffixArray. */
  TreeNode(size_t maxArraySize = 100000);
  ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return children_.empty(); }

  /** Number of token positions in the index. */
  size_t size() const { return size_; }

  /** random access
   * in O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   * WARNING: order depends on underlying hash map. */
  Position<Token> AtUnordered(size_t offset);

  /**
   * Access to a position within the selected span. As opposed to operator[],
   * this guarantees the order, but is O(V) with vocabulary size.
   */
  Position<Token> At(size_t offset, const Vocab<Token> &vocab);

  void DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth = 0);

private:
  ChildMap children_; /** node children, empty if leaf node */
  std::vector<Position<Token>> array_; /** suffix array, only if children_.empty() */
  size_t size_; /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  size_t partial_size_sum_; /** partial sum of all sizes on this tree level to our left (so leftmost child has 0 here) */

  const size_t kMaxArraySize; /** maximum size of suffix array leaf, larger sizes are split up into TreeNodes. */

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

  /** Split this leaf node (suffix array) into a proper TreeNode with children.
   * depth: distance of TreeNode from the root of this tree
   * */
  void SplitNode(const Corpus<Token> &corpus, Offset depth);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
