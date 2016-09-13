/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TREENODEMEMORY_H
#define STO_TREENODEMEMORY_H

#include <memory>
#include <cassert>

#include "TreeNode.h"
#include "SuffixArrayMemory.h"
#include "ITokenIndex.h"

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
  typedef typename TreeNode<Token, SuffixArray>::Iterator Iterator;

  /**
   * Constructs an empty TreeNode, i.e. a leaf with a SuffixArray.
   * @param filename  load mtt-build *.sfa file if specified
   */
  TreeNodeMemory(ITokenIndex<Token> &index, size_t maxArraySize, std::string filename, std::shared_ptr<void>, ITreeNode<Token> *parent = nullptr, Vid vid = Token::kInvalidVid);

  virtual ~TreeNodeMemory() = default;

  // not implemented
  void Merge(IndexSpan<Token> &spanMemory, IndexSpan<Token> &spanUs) { assert(0); }

  /**
   * Insert the existing Corpus Position into this leaf node (SuffixArray).
   * This potentially splits the SuffixArray into individual TreeNodes,
   * and inserts a Position entry into the suffix array. Hence, it is an
   * O(k + log(n)) operation, with k = TreeNode<Token>::kMaxArraySize
   *
   * Exclusively for adding to a SA (leaf node).
   */
  void AddPosition(const Sentence<Token> &sent, Offset start);

  /** Add an empty leaf node (SuffixArray) as a child. */
  void AddLeaf(Vid vid);

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNodeMemory<Token> **child = nullptr);

  /** Finalize an update with seqNum. Flush writes to DB and apply a new persistence sequence number. */
  virtual void Ack(seq_t seqNum) {}
  /** current persistence sequence number */
  virtual seq_t seqNum() const { return 1; /* for legacy data, to make tests happy */ }

private:
  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   */
  void SplitNode(const Corpus<Token> &corpus);

  /**
   * Load this leaf node (SuffixArray) from mtt-build *.sfa file on disk.
   */
  void LoadArray(const std::string &filename);

  /** factory function for TreeNode::SplitNode() */
  TreeNodeMemory<Token> *make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus);
};

} // namespace sto

#endif //STO_TREENODEMEMORY_H
