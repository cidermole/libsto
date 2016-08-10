/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_ITREENODE_H
#define STO_ITREENODE_H

#include <memory>

#include "Corpus.h"

namespace sto {

template<typename Token> class IndexSpan;

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

  /**
   * Access to a position within the selected span
   * in O(log(n/k)) with k = TreeNode<Token>::kMaxArraySize.
   */
  virtual Position<Token> At(size_t abs_offset, size_t rel_offset) = 0;

  /** Merge the entire IndexSpan spanMemory into this leaf. spanDisk must be a path to this TreeNode. */
  virtual void Merge(IndexSpan<Token> &spanMemory, IndexSpan<Token> &spanDisk) = 0;

  /** Add an empty leaf node (SuffixArray) as a child. */
  virtual void AddLeaf(Vid vid) = 0;

  /** Finalize an update with seqNum. Flush writes to DB and apply a new persistence sequence number. */
  virtual void Ack(seq_t seqNum) = 0;
  /** current persistence sequence number */
  virtual seq_t seqNum() const = 0;

  /** iterator over vids of children of internal nodes */
  virtual IVidIterator<Token> begin() const = 0;
  virtual IVidIterator<Token> end() const = 0;
};

} // namespace sto

#endif //STO_ITREENODE_H
