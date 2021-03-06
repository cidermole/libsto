/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TREENODE_H
#define STO_TREENODE_H

#include <memory>
#include <atomic>

#include "Range.h"
#include "Corpus.h"
#include "util/rbtree.hpp"

#include "SuffixArrayDisk.h"

namespace sto {

template<class Token> class IndexSpan;
template<class Token> class TokenIndex;

/**
 * A TreeNode belongs to a TokenIndex and represents a word and its possible
 * suffix extensions.
 *
 * Each leaf is implemented as a suffix array, which itself encodes part of
 * the tree (with potentially arbitrary depth). This helps to keep the RAM
 * size low.
 */
template<class Token, class SuffixArray>
class TreeNode {
public:
  typedef typename Corpus<Token>::Vid Vid;
  typedef typename Corpus<Token>::Offset Offset;
  typedef RBTree<Vid, TreeNode<Token, SuffixArray> *> ChildMap;
  typedef SuffixArray SuffixArrayT;

  ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return is_leaf_.load(); }

  /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  size_t size() const;

  /**
   * Access to a position within the selected span
   * in O(log(n/k)) with k = TreeNode<Token>::kMaxArraySize.
   */
  Position<Token> At(size_t abs_offset, size_t rel_offset);

  void DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth = 0);

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
  // in TreeNodeMemory:
  //void AddPosition(const Sentence<Token> &sent, Offset start, size_t depth);

  /** Add an empty leaf node (SuffixArray) as a child. */
  void AddLeaf(Vid vid) { assert(0); }

  /** Increase the given vid child's size. */
  void AddSize(Vid vid, size_t add_size);

  /** TODO: is there a nicer way? maybe via constructor? (but only TreeNodeMemory) */
  void SetArray(std::shared_ptr<SuffixArray> array) { array_ = array; }

  /** find the bounds of an existing Token or insertion point of a new one */
  Range find_bounds_array_(Corpus<Token> &corpus, Range prev_bounds, Token t, size_t depth);

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNode<Token, SuffixArray> **child = nullptr);

protected:
  std::atomic<bool> is_leaf_; /** whether this is a suffix array (leaf node) */
  ChildMap children_; /** TreeNode children, empty if is_leaf. Additionally carries along partial sums for child sizes. */
  std::shared_ptr<SuffixArray> array_; /** suffix array, only if is_leaf */

  /**
   * maximum size of suffix array leaf, larger sizes are split up into TreeNodes.
   * NOTE: the SA leaf of </s> may grow above kMaxArraySize, see AddPosition() implementation.
   */
  const size_t kMaxArraySize;

  /**
   * Constructs an empty TreeNode, i.e. a leaf with a SuffixArray.
   * Used by TreeNodeDisk() and TreeNodeMemory()
   */
  TreeNode(size_t maxArraySize = 100000);

};

} // namespace sto

#endif //STO_TREENODE_H
