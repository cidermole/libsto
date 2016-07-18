/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TREENODEMEMORY_H
#define STO_TREENODEMEMORY_H

#include "TreeNode.h"
//#include "TokenIndex.h"
#include "SuffixArrayMemory.h"

namespace sto {

struct IndexTypeMemory;
struct IndexTypeDisk;

/**
 * See TreeNode: an internal representation used by TokenIndex,
 * represents a word and its possible suffix extensions.
 */
template<class Token>
class TreeNodeMemory : public TreeNode<Token, SuffixArrayMemory<Token>> {
  typedef SuffixArrayMemory<Token> SuffixArray;
  typedef typename TreeNode<Token, SuffixArray>::Vid Vid;
  typedef typename Corpus<Token>::Offset Offset;

public:
  /**
   * Constructs an empty TreeNode, i.e. a leaf with a SuffixArray.
   * @param filename  load mtt-build *.sfa file if specified
   */
  TreeNodeMemory(std::string filename, size_t maxArraySize = 10000);

  /*
  template<class IndexSpanMemory, class IndexSpanDisk>
  void Merge(const IndexSpanMemory &spanMemory, IndexSpanDisk &spanDisk) { assert(0); }
   */

  // not implemented
  void Merge(const typename TokenIndex<Token, IndexTypeMemory>::Span &spanMemory, typename TokenIndex<Token, IndexTypeMemory>::Span &spanUs);

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

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNodeMemory<Token> **child = nullptr);

private:
  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   * depth: distance of TreeNode from the root of this tree
   */
  void SplitNode(const Corpus<Token> &corpus, Offset depth);

  /**
   * Load this leaf node (SuffixArray) from mtt-build *.sfa file on disk.
   */
  void LoadArray(const std::string &filename);

  /** factory function for TreeNode::SplitNode() */
  TreeNodeMemory<Token> *make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus, Offset depth);
};

} // namespace sto

#endif //STO_TREENODEMEMORY_H
