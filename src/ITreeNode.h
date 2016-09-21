/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_ITREENODE_H
#define STO_ITREENODE_H

#include <memory>

#include "Corpus.h"
#include "StreamVersions.h"

namespace sto {

template<class Token> class IndexSpan;
template<class Token> class ITokenIndex;

/**
 * Internal interface for TreeNode::Iterator
 */
template<typename Vid>
class ITreeNodeIterator {
public:
  virtual ~ITreeNodeIterator() {}

  virtual const Vid &operator*() = 0;
  virtual ITreeNodeIterator &operator++() = 0;
  virtual bool operator!=(const ITreeNodeIterator &other) const = 0;

  /** allocate a new instance that is a copy of this instance. */
  virtual ITreeNodeIterator *copy() const = 0;
};

/**
 * Iterates over vocabulary IDs of internal TreeNodes.
 *
 * This is a proxy object taking an ITreeNodeIterator.
 */
template<typename Token>
class IVidIterator {
public:
  typedef typename Token::Vid Vid;

  IVidIterator(std::shared_ptr<ITreeNodeIterator<Vid>> it) : it_(it) {}
  IVidIterator(const IVidIterator &other) : it_(other.it_->copy()) {}
  ~IVidIterator() = default;

  const Vid &operator*() { return it_->operator*(); }
  IVidIterator &operator++() { it_->operator++(); return *this; }
  bool operator!=(const IVidIterator &other) { return it_->operator!=(*other.it_); }

private:
  std::shared_ptr<ITreeNodeIterator<Vid>> it_;
};

/** Internal interface for TreeNode */
template<typename Token>
class ITreeNode {
public:
  typedef typename Token::Vid Vid;

  /** true if this is a leaf, i.e. a suffix array. */
  virtual bool is_leaf() const = 0;

  /** Number of token positions. cumulative length in inner nodes, array_.size() in leaf nodes */
  virtual size_t size() const = 0;

  /** distance from the root */
  virtual size_t depth() const = 0;

  /** Returns the Span over all Positions with this prefix (path from root to this node). */
  virtual IndexSpan<Token> span() = 0;

  /**
   * Access to a position within the selected span
   * in O(log(n/k)) with k = TreeNode<Token>::kMaxArraySize.
   */
  virtual Position<Token> At(size_t abs_offset, size_t rel_offset) = 0;

  /** Merge the entire IndexSpan spanMemory into this leaf. spanDisk must be a path to this TreeNode. */
  //virtual void Merge(IndexSpan<Token> &spanMemory, IndexSpan<Token> &spanDisk) = 0;

  /** Sorts the leaf node, for a special leaf node type TreeNodeMemBuf, does nothing for others */
  virtual void EnsureSorted(const Corpus<Token> &corpus) = 0;

  /** Add an empty leaf node (SuffixArray) as a child. */
  virtual void AddLeaf(Vid vid) = 0;

  /** Finalize an update with seqNum. Flush writes to DB and apply a new persistence sequence number. */
  virtual void Flush(StreamVersions streamVersions) = 0;
  /** current persistence sequence number */
  virtual StreamVersions streamVersions() const = 0;

  /** iterator over vids of children of internal nodes */
  virtual IVidIterator<Token> begin() const = 0;
  virtual IVidIterator<Token> end() const = 0;

  /** word type common to all Positions at depth-1; invalid for root */
  virtual Vid vid() const = 0;
  /** parent of this node, nullptr for root */
  virtual ITreeNode<Token> *parent() const = 0;
  virtual const ITokenIndex<Token> &index() const = 0;

  /** check whether all Positions have our vid at depth. */
  virtual void DebugCheckVidConsistency() const = 0;
};

} // namespace sto

#endif //STO_ITREENODE_H
