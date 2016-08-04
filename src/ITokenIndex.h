/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_ITOKENINDEX_H
#define STO_ITOKENINDEX_H

#include <iostream>
#include <vector>
#include <memory>

#include "Range.h"
#include "Corpus.h"
#include "SuffixArrayMemory.h"
//#include "TreeNodeMemory.h"
//#include "TreeNodeDisk.h"
#include "ITreeNode.h"

namespace sto {

template<class Token> class DB;

/**
 * IndexSpan represents the matched locations of a partial lookup sequence
 * within TokenIndex.
 *
 * You start with the empty lookup sequence from TokenIndex::span() and
 * keep adding tokens to the lookup via narrow().
 */
template<typename Token>
class ITokenIndexSpan {
public:
  virtual ~ITokenIndexSpan() {}

  /**
   * Iterator over vids (vocabulary IDs) associated with a TreeNode.
   * Obtain instances from TokenIndex::Span
   */
  class VidIterator {
  public:
    typedef typename Corpus<Token>::Vid Vid;
    typedef typename Corpus<Token>::Offset Offset;

    VidIterator(const VidIterator &other) = default;

    /** use TokenIndex::Span::begin() / end() instead. */
    VidIterator(const ITokenIndexSpan &span, bool begin = true) : span_(span), index_(begin ? 0 : span.size()), depth_(span.depth()), iter_(begin ? span.node()->begin() : span.node()->end()), is_leaf_(span.in_array()) {}

    virtual Vid operator*() {
      if(is_leaf_)
        return span_[index_].add((Offset) depth_, *span_.corpus()).vid(*span_.corpus());
      else
        return *iter_;
    }
    virtual VidIterator &operator++() {
      if(is_leaf_)
        index_ += StepSize();
      else
        ++iter_;
      return *this;
    }
    virtual bool operator!=(const VidIterator &other) {
      if(is_leaf_)
        return index_ != other.index_;
      else
        return iter_ != other.iter_;
    }

    /**
     * @returns the number of Positions comparing equal from the current index_,
     * aka the number of Positions to skip to get to the next vid at the current depth
     */
    virtual size_t StepSize() {
      return span_.StepSize(index_);
    }

  private:
    const ITokenIndexSpan &span_;
    size_t index_;
    size_t depth_;
    IVidIterator<Token> iter_;
    bool is_leaf_;
  };


  /**
   * Narrow the span by adding a token to the end of the lookup sequence.
   * Returns new span size.
   * If the token was not found at all, returns zero without modifying the
   * IndexSpan.
   * If an empty SuffixArray leaf was found, returns zero while
   * still modifying the IndexSpan.
   */
  virtual size_t narrow(Token t) = 0;

  /**
   * Random access to a position within the selected span.
   * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   *
   * When reading a SuffixArray leaf that is being written to,
   * returned Position values will always be valid, but
   * may be to the left of 'rel'.
   */
  virtual Position<Token> operator[](size_t rel) const = 0;

  // for testing
  virtual Position<Token> at_unchecked(size_t rel) const = 0;

  /**
   * Number of token positions spanned in the index.
   *
   * Returns a size cached when narrow() was called.
   */
  virtual size_t size() const = 0;

  /** Length of lookup sequence, or the number of times narrow() has been called. */
  virtual size_t depth() const = 0;

  /** Distance from the root in number of TreeNodes. */
  virtual size_t tree_depth() const = 0;

  /** TreeNode at current depth. */
  virtual ITreeNode<Token> *node() const = 0;

  /** first part of path from root through the tree, excluding suffix array range choices */
  virtual const std::vector<ITreeNode<Token> *>& tree_path() const = 0;

  /** true if span reaches into a suffix array leaf. */
  virtual bool in_array() const = 0;

  virtual Range array_range() const = 0;

  /** partial lookup sequence so far, as appended by narrow() */
  virtual const std::vector<Token>& sequence() const = 0;

  virtual Corpus<Token> *corpus() const = 0;

  /** iterate over unique vocabulary IDs at this depth. */
  virtual VidIterator begin() const = 0;
  virtual VidIterator end() const = 0;

  /** allocate a new instance that is a copy of this instance. */
  virtual ITokenIndexSpan *copy() const = 0;

  /**
   * @returns the number of Positions comparing equal from the current index,
   * aka the number of Positions to skip to get to the next vid at the current depth
   *
   * Currently only available on leaf nodes, but just lazy.
   */
  virtual size_t StepSize(size_t index) const = 0;
};

/**
 * Interface to a corpus index.
 *
 * This is the public interface, which is implemented by TokenIndex for memory or disk-backed storage.
 */
template<class Token>
class ITokenIndex {
public:
  virtual ~ITokenIndex() {}

  /** Returns the whole span of the entire index (empty lookup sequence). */
  virtual std::shared_ptr<ITokenIndexSpan<Token>> span() const = 0;

  virtual Corpus<Token> *corpus() const = 0;

  /**
   * Insert the existing Corpus Sentence into this index. Last token must be the EOS symbol </s>.
   *
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is roughly an
   *
   * O(l * (k + log(n)))
   *
   * operation, with l = sent.size(), k = TreeNode<Token>::kMaxArraySize
   * and n = span().size() aka the full index size.
   *
   * Thread safety: writes concurrent to multiple reading threads
   * do not result in invalid state being read.
   */
  virtual void AddSentence(const Sentence<Token> &sent) = 0;

  virtual void Merge(const ITokenIndex<Token> &add) = 0;

  /** Write to (empty) DB. */
  virtual void Write(std::shared_ptr<DB<Token>> db) const = 0;

  virtual void DebugPrint(std::ostream &os) = 0;

};

} // namespace sto


#endif //STO_ITOKENINDEX_H
