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
#include "ITreeNode.h"
#include "ObjIterator.h"

namespace sto {

template<class Token> class DB;
template<class Token> class ITokenIndex;

/**
 * Internal, abstract TokenIndexSpan interface, implemented e.g. by TokenIndex::Span
 *
 * For outside users, use IndexSpan<Token> from TokenIndex::span()
 */
template<typename Token>
class ITokenIndexSpan {
public:
  typedef ObjIterator<ITokenIndexSpan<Token>> PosIterator;
  typedef Position<Token> value_type;

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

    Vid operator*() {
      if(is_leaf_)
        return span_[index_].add((Offset) depth_, *span_.corpus()).vid(*span_.corpus());
      else
        return *iter_;
    }
    VidIterator &operator++() {
      if(is_leaf_) {
        size_t step_size = span_.StepSize(this->operator*()); // number of Positions to skip to get to the next vid at the current depth
        if(step_size == 0) {
          std::cerr << "StepSize(" << ((unsigned int) this->operator*()) << ")==0 at VidIterator::operator++() in seq len=" << span_.sequence().size() << " is" << std::endl;
          auto seq = span_.sequence();
          for(auto s : seq)
              std::cerr << " " << ((unsigned int) s.vid);
          std::cerr << std::endl;
          std::cerr << "span size=" << span_.size() << std::endl;

          auto test_span = span_.index()->span();
          std::cerr << "Stepping in a new span. Full index size=" << test_span.size() << std::endl;
          for(auto s : seq)
            std::cerr << "narrow(" << ((unsigned int) s.vid) << ")=" << test_span.narrow(s) << std::endl;
          std::cerr << "after narrow sequence, span size=" << test_span.size() << std::endl;

          std::cerr << "verifying node consistency..." << std::endl;
          span_.node()->DebugCheckVidConsistency();

          std::cerr << "verifying full index consistency..." << std::endl;
          span_.index()->span().node()->DebugCheckVidConsistency();

          throw std::runtime_error("StepSize()==0. This should never happen.");
        }
        index_ += step_size;
      } else {
        ++iter_;
      }
      return *this;
    }
    bool operator!=(const VidIterator &other) {
      if(is_leaf_)
        return index_ != other.index_;
      else
        return iter_ != other.iter_;
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

  /** TreeNode at current depth. */
  virtual ITreeNode<Token> *node() const = 0;

  /** true if span reaches into a suffix array leaf. */
  virtual bool in_array() const = 0;

  /** partial lookup sequence so far, as appended by narrow() */
  virtual const std::vector<Token>& sequence() const = 0;

  virtual Corpus<Token> *corpus() const = 0;

  /** iterate over unique vocabulary IDs at this depth. */
  virtual VidIterator vid_begin() const = 0;
  virtual VidIterator vid_end() const = 0;

  /** iterate over Positions in this Span. */
  virtual PosIterator begin() const = 0;
  virtual PosIterator end() const = 0;

  /** allocate a new instance that is a copy of this instance. */
  virtual ITokenIndexSpan *copy() const = 0;

  /**
   * Number of Positions equal to 't' in this span.
   *
   * Currently only available on leaf nodes, but just lazy.
   */
  virtual size_t StepSize(Token t) const = 0;

  virtual ITokenIndex<Token> *index() const = 0;
};


/**
 * IndexSpan represents the matched locations of a partial lookup sequence
 * within TokenIndex.
 *
 * You start with the empty lookup sequence from TokenIndex::span() and
 * keep adding tokens to the lookup via narrow().
 *
 * This is a proxy object taking a virtual ITokenIndexSpan.
 */
template<class Token>
class IndexSpan {
public:
  typedef typename ITokenIndexSpan<Token>::VidIterator VidIterator;

  // note: use TokenIndex::span() for constructing an IndexSpan

  /** Create an IndexSpan wrapper backed by 'span' */
  IndexSpan(std::shared_ptr<ITokenIndexSpan<Token>> span) : span_(span) {}

  /** Deep copy so modifications don't affect 'other' */
  IndexSpan(const IndexSpan &other) : span_(other.span_->copy()) {}
  IndexSpan(IndexSpan &&other) = default;

  IndexSpan &operator=(const IndexSpan &other) { span_ = other.span_->copy(); return *this; }
  IndexSpan &operator=(IndexSpan &&other) = default;

  /**
   * Narrow the span by adding a token to the end of the lookup sequence.
   * Returns new span size.
   * If the token was not found at all, returns zero without modifying the
   * IndexSpan.
   * If an empty SuffixArray leaf was found, returns zero while
   * still modifying the IndexSpan.
   */
  size_t narrow(Token t) { return span_->narrow(t); }

  /**
   * Random access to a position within the selected span.
   * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
   *
   * When reading a SuffixArray leaf that is being written to,
   * returned Position values will always be valid, but
   * may be to the left of 'rel'.
   */
  Position<Token> operator[](size_t rel) const { return span_->operator[](rel); }

  // for testing
  Position<Token> at_unchecked(size_t rel) const { return span_->at_unchecked(rel); }

  /**
   * Number of token positions spanned in the index.
   *
   * Returns a size cached when narrow() was called.
   */
  size_t size() const { return span_->size(); }

  /** Length of lookup sequence, or the number of times narrow() has been called. */
  size_t depth() const { return span_->depth(); }

  /** TreeNode at current depth. */
  ITreeNode<Token> *node() const { return span_->node(); }

  /** true if span reaches into a suffix array leaf. */
  bool in_array() const { return span_->in_array(); }

  /** partial lookup sequence so far, as appended by narrow() */
  const std::vector<Token> &sequence() const { return span_->sequence(); }

  Corpus<Token> *corpus() const { return span_->corpus(); }

  /** iterate over unique vocabulary IDs at this depth. */
  VidIterator begin() const { return span_->vid_begin(); }
  VidIterator end() const { return span_->vid_end(); }

  /**
   * @returns the number of Positions comparing equal from the current index,
   * aka the number of Positions to skip to get to the next vid at the current depth
   *
   * Currently only available on leaf nodes, but just lazy.
   */
  size_t StepSize(Token t) const { return span_->StepSize(t); }

  ITokenIndexSpan<Token> *get() const { return span_.get(); }

  virtual ITokenIndex<Token> *index() const { return span_->index(); }

private:
  std::shared_ptr<ITokenIndexSpan<Token>> span_;
};


/**
 * Interface to a Corpus index for fast lookup of phrases.
 *
 * This is the public interface, which is implemented by TokenIndex for memory or disk-backed storage.
 */
template<class Token>
class ITokenIndex {
public:
  virtual ~ITokenIndex() {}

  /** Returns the whole span of the entire index (empty lookup sequence). */
  virtual IndexSpan<Token> span() const = 0;

  /** Returns the Span over all Positions with this prefix (path from root to this node). */
  virtual IndexSpan<Token> span(ITreeNode<Token> &node) const = 0;

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
  virtual void AddSentence(const Sentence<Token> &sent, seq_t seqNum = static_cast<seq_t>(-1)) = 0;

  /** Merge all Positions from 'add' into this TokenIndex. */
  virtual void Merge(const ITokenIndex<Token> &add) = 0;

  /** Split the root node. */
  virtual void Split() = 0;

  /** Write to (empty) DB. */
  virtual void Write(std::shared_ptr<DB<Token>> db) const = 0;

  /** current persistence sequence number */
  virtual seq_t seqNum() const = 0;

  virtual void SetSeqNum(seq_t seqNum) = 0;

  virtual void DebugPrint(std::ostream &os) = 0;

};

} // namespace sto


#endif //STO_ITOKENINDEX_H
