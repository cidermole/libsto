/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TREENODE_H
#define STO_TREENODE_H

#include <memory>
#include <atomic>
#include <algorithm>
#include <unordered_map>

#include "Range.h"
#include "Corpus.h"
#include "util/rbtree.hpp"

#include "SuffixArrayDisk.h"
#include "ITreeNode.h"

namespace sto {

template<class Token> class IndexSpan;
template<class Token> class ITokenIndex;
template<class Token> class ITokenIndexSpan;


/**
 * Defines a strategy for merging leaves. Can be passed to Merge(), to implement filtering for domains.
 */
template<class Token, class SuffixArray>
class LeafMerger {
public:
  virtual std::shared_ptr<SuffixArray> MergeLeafArray(std::shared_ptr<SuffixArray> curSpan, const ITokenIndexSpan<Token> &addSpan) = 0;
};

/**
 * A TreeNode belongs to a TokenIndex and represents a word and its possible
 * suffix extensions.
 *
 * Each leaf is implemented as a suffix array, which itself encodes part of
 * the tree (with potentially arbitrary depth). This helps to keep the RAM
 * size low.
 */
template<class Token, class SuffixArray>
class TreeNode : public ITreeNode<Token>, public LeafMerger<Token, SuffixArray> {
public:
  typedef typename Corpus<Token>::Vid Vid;
  typedef typename Corpus<Token>::Offset Offset;
  typedef RBTree<Vid, TreeNode<Token, SuffixArray> *> ChildMap;
  typedef typename ChildMap::Iterator Iterator;
  typedef SuffixArray SuffixArrayT;

  // TreeNode() is protected: construct a TreeNodeMemory() or TreeNodeDisk()
  virtual ~TreeNode();

  /** true if this is a leaf, i.e. a suffix array. */
  bool is_leaf() const { return is_leaf_.load(); }

  /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  size_t size() const;

  /** distance from the root */
  virtual size_t depth() const { return depth_; }

  /** Returns a Span over all Positions with this prefix (path from root to this node). */
  virtual IndexSpan<Token> span();

  /**
   * Access to a position within the selected span
   * in O(log(n/k)) with k = TreeNode<Token>::kMaxArraySize.
   */
  Position<Token> At(size_t abs_offset, size_t rel_offset);

  void DebugPrint(std::ostream &os, const std::unordered_map<Vid, std::string> &id2surface, size_t depth = 0);


  /** Merge 'spanSource' into this TreeNode. 'spanTarget' must be a span over this TreeNode. */
  void Merge(IndexSpan<Token> &spanSource, IndexSpan<Token> &spanTarget, LeafMerger<Token, SuffixArray> &merger);

  /**
   * Merge 'addSpan' into this leaf.
   * Creates a temporary suffix array first, before moving it to replace the old suffix array.
   *
   * Assumes there is at most one writer at all times (one process, and only one writing thread).
   *
   * @param addSpan  TreeNode span to be merged in (span over the same vid)
   * @param merger   leaf merging strategy
   */
  virtual void MergeLeaf(const ITokenIndexSpan<Token> &addSpan, LeafMerger<Token, SuffixArray> &merger) = 0;
  //virtual std::shared_ptr<SuffixArray> MergeLeafArray(std::shared_ptr<SuffixArray> curSpan, const ITokenIndexSpan<Token> &addSpan) override;

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
  // only in TreeNodeMemory:
  virtual void AddPosition(const Sentence<Token> &sent, Offset start) = 0;

  /** Add an empty leaf node (SuffixArray) as a child. */
  //virtual void AddLeaf(Vid vid) = 0;

  /** Increase the given vid child's size. */
  void AddSize(Vid vid, size_t add_size);

  /** find the bounds of an existing Token or insertion point of a new one */
  Range find_bounds_array_(Corpus<Token> &corpus, Range prev_bounds, Token t, size_t depth);

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNode<Token, SuffixArray> **child = nullptr);

  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   */
  virtual void SplitNode(const Corpus<Token> &corpus) = 0;

  /** iterator over vids of children of internal nodes */
  IVidIterator<Token> begin() const { return IVidIterator<Token>(std::shared_ptr<ITreeNodeIterator<typename Token::Vid>>(children_.begin().copy())); }
  IVidIterator<Token> end() const { return IVidIterator<Token>(std::shared_ptr<ITreeNodeIterator<typename Token::Vid>>(children_.end().copy())); }

  virtual void EnsureSorted(const Corpus<Token> &corpus) override {}

  /** word type common to all Positions at depth-1; invalid for root */
  virtual Vid vid() const { return vid_; }
  /** parent of this node, nullptr for root */
  virtual ITreeNode<Token> *parent() const { return parent_; }
  virtual const ITokenIndex<Token> &index() const { return index_; }

  /** Write vids of children to persistent storage. Only implemented for TreeNodeDisk. */
  virtual void WriteChildren() {}

  /** check whether all Positions have our vid at depth. */
  virtual void DebugCheckVidConsistency() const override;

  /** for collecting profiling information */
  virtual std::shared_ptr<DB<Token>> GetDB() override { return std::shared_ptr<DB<Token>>(); }

  virtual void Flush(StreamVersions streamVersions) {}
  virtual StreamVersions streamVersions() const { /* stub! streamVersions_ is never updated. */ return streamVersions_; }

protected:
  ITokenIndex<Token> &index_;           /** TokenIndex that this TreeNode belongs to */
  std::atomic<bool> is_leaf_;           /** whether this is a suffix array (leaf node) */
  ChildMap children_;                   /** TreeNode children, empty if is_leaf. Additionally carries along partial sums for child sizes. */
  std::shared_ptr<SuffixArray> array_;  /** suffix array, only if is_leaf */
  ITreeNode<Token> *parent_;            /** parent of this node, nullptr for root */
  size_t depth_;                        /** distance from the root */
  Vid vid_;                             /** word type common to all Positions at depth-1; invalid for root */
  StreamVersions streamVersions_;       /** persistence sequence numbers */

  /**
   * maximum size of suffix array leaf, larger sizes are split up into TreeNodes.
   * NOTE: the SA leaf of </s> may grow above kMaxArraySize, see AddPosition() implementation.
   */
  const size_t kMaxArraySize;

  /**
   * Constructs an empty TreeNode, i.e. a leaf with a SuffixArray.
   * Used by TreeNodeDisk() and TreeNodeMemory()
   *
   * @param index         TokenIndex that this TreeNode belongs to
   * @param parent        suffix trie parent; nullptr for root
   * @param vid           word type common to all Positions at depth_-1
   * @param maxArraySize  maximum size of suffix array leaf
   */
  TreeNode(ITokenIndex<Token> &index, size_t maxArraySize = 100000, ITreeNode<Token> *parent = nullptr, Vid vid = Token::kInvalidVid);

  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   */
  template<class NodeFactory>
  void SplitNode(const Corpus<Token> &corpus, NodeFactory factory);
};


template<class Token, class SuffixArray>
template<class NodeFactory>
void TreeNode<Token, SuffixArray>::SplitNode(const Corpus<Token> &corpus, NodeFactory factory) {
  typedef typename SuffixArray::iterator iter;

  assert(this->is_leaf()); // this method works only on suffix arrays

  size_t depth = depth_;

  auto comp = [&corpus, depth](const Position<Token> &a, const Position<Token> &b) {
    // the suffix array at this depth should only contain positions that continue long enough without the sentence ending
    return a.add(depth, corpus).vid(corpus) < b.add(depth, corpus).vid(corpus);
  };

  std::pair<iter, iter> vid_range;
  std::shared_ptr<SuffixArray> array = this->array_;
  size_t size = array->size();
  Position<Token> pos = size ? Position<Token>((*array)[0]) : Position<Token>(); // first position with first vid

  // thread safety: we build the TreeNode while is_leaf_ == true, so children_ is not accessed while being modified

  // for each top-level word, find the suffix array range and populate individual split arrays
  if(size) {
    while(true) {
      vid_range = std::equal_range(array->begin(), array->end(), pos, comp);
      Vid vid = pos.add(depth, corpus).vid(corpus);

      // copy each range into its own suffix array
      TreeNode<Token, SuffixArray> *new_child = factory(vid, vid_range.first, vid_range.second, corpus, depth); // Span would be easier...
      size_t new_size = vid_range.second - vid_range.first;
      //children_[vid] = new_child;
      this->children_.FindOrInsert(vid, /* add_size = */ new_size) = new_child;

#ifndef NDEBUG
      TreeNode<Token, SuffixArray> *n = nullptr;
      assert(this->children_.Find(vid, &n));
      assert(n == new_child);
      assert(this->children_.ChildSize(vid) == new_size);
#endif

      if(vid_range.second != array->end())
        pos = *vid_range.second; // position with next vid
      else
        break;
    }
  }
  assert(this->children_.Size() == array->size());

  // release: ensure prior writes to children_ get flushed before the atomic operation
  this->is_leaf_.store(false, std::memory_order_release);

  // destroy the suffix array (last reader will clean up)
  this->array_.reset();
  // note: array_ null check could replace is_leaf_
}


} // namespace sto

#endif //STO_TREENODE_H
