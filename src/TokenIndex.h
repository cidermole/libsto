/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include <iostream>
#include <vector>
#include <memory>
#include <unordered_map>

#include "Range.h"
#include "SuffixArrayMemory.h"
#include "TreeNodeMemory.h"
#include "TreeNodeDisk.h"
#include "Corpus.h"
#include "util/rbtree.hpp"
#include "ITokenIndex.h"

namespace sto {

template<class Token, class SuffixArray> class TreeNode;

template<class Token> class DB;

struct IndexTypeMemory {
  static constexpr bool HasAddSentence = true;
};
struct IndexTypeMemBuf {
  static constexpr bool HasAddSentence = true;
};
struct IndexTypeDisk {
  static constexpr bool HasAddSentence = false; /** IndexTypeDisk implementation has no AddSentence() support, so we need to use Merge() instead */
};

/** types for TokenIndex with different backing (Disk or Memory) */
template<typename Token, typename IndexTypeTag>
struct IndexTypes {
  // specialized below
  typedef void SuffixArray;
  typedef ITreeNode<Token> TreeNode;
};

/** partial specialization: types for IndexTypeDisk */
template<>
template<typename Token>
struct IndexTypes<Token, IndexTypeDisk> {
  typedef SuffixArrayMemory<Token> SuffixArray;
  typedef TreeNodeDisk<Token> TreeNode;
};

/** partial specialization: types for IndexTypeMemory */
template<>
template<typename Token>
struct IndexTypes<Token, IndexTypeMemory> {
  typedef SuffixArrayMemory<Token> SuffixArray;
  typedef TreeNodeMemory<Token> TreeNode;
};

/** partial specialization: types for IndexTypeMemBuf */
template<>
template<typename Token>
struct IndexTypes<Token, IndexTypeMemBuf> {
  typedef SuffixArrayMemory<Token> SuffixArray;
  typedef TreeNodeMemBuf<Token> TreeNode;
};

/**
 * Indexes a Corpus. The index is implemented as a hybrid suffix tree/array.
 *
 * Vocab note: vid of explicit sentence end delimiting symbol </s> must be at the very beginning of all vocab symbols
 * (sort order matters, shorter sequences must come first)!
 */
template<class Token, typename TypeTag = IndexTypeMemory>
class TokenIndex : public ITokenIndex<Token> {
public:
  typedef Token TokenT;
  typedef TypeTag TypeTagT;
  typedef typename Corpus<Token>::Offset Offset;
  typedef typename Corpus<Token>::Vid Vid;

  typedef typename IndexTypes<Token, TypeTag>::SuffixArray SuffixArray;
  typedef typename IndexTypes<Token, TypeTag>::TreeNode TreeNodeT;


  /**
   * Load TokenIndex from mtt-build *.sfa file for the associated corpus.
   *
   * Currently, this creates one big suffix array (not memory mapped) regardless of maxLeafSize, but subsequent calls to
   * AddSentence() will honor maxLeafSize and split where necessary (so the first additions will be expensive).
   */
  TokenIndex(const std::string &filename, Corpus<Token> &corpus, std::shared_ptr<DB<Token>> db = nullptr, size_t maxLeafSize = 10000);

  /** Construct an empty TokenIndex, i.e. this does not index the Corpus by itself. */
  TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize = 10000);
  virtual ~TokenIndex();


  /**
   * IndexSpan represents the matched locations of a partial lookup sequence
   * within TokenIndex.
   *
   * You start with the empty lookup sequence from TokenIndex::span() and
   * keep adding tokens to the lookup via narrow().
   */
  class Span : public ITokenIndexSpan<Token> {
  public:
    friend class TokenIndex;
    typedef typename TokenIndex::TreeNodeT TreeNodeT;
    typedef typename ITokenIndexSpan<Token>::VidIterator VidIterator;
    typedef typename ITokenIndexSpan<Token>::PosIterator PosIterator;

    // note: use TokenIndex::span() for constructing an IndexSpan

    Span(const Span &other) = default;
    Span(Span &&other) = default;

    Span& operator=(const Span &other) = default;
    Span& operator=(Span &&other) = default;

    /**
     * Narrow the span by adding a Token to the end of the lookup sequence.
     * Returns new span size.
     * If the token was not found at all, returns zero without modifying the
     * IndexSpan.
     * If an empty SuffixArray leaf was found, returns zero while
     * still modifying the IndexSpan.
     */
    virtual size_t narrow(Token t);

    /**
     * Random access to a position within the selected span.
     * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
     *
     * When reading a SuffixArray leaf that is being written to,
     * returned Position values will always be valid, but
     * may be to the left of 'rel'.
     */
    virtual Position<Token> operator[](size_t rel) const;

    // for testing
    virtual Position<Token> at_unchecked(size_t rel) const;

    /**
     * Number of token positions spanned in the index.
     *
     * Note that this always returns NON-ZERO size here (except on empty TokenIndex),
     * even if the lookup sequence was not found. Use the return value of narrow() instead.
     */
    virtual size_t size() const;

    /** Length of lookup sequence, or the number of times narrow() has been successfully called. */
    virtual size_t depth() const;

    /** TreeNode at current depth. */
    virtual ITreeNode<Token> *node() const;

    /** true if span reaches into a suffix array leaf. */
    virtual bool in_array() const;

    /** partial lookup sequence so far, as appended by narrow() */
    virtual const std::vector<Token>& sequence() const { return sequence_; }

    virtual Corpus<Token> *corpus() const;

    virtual ITokenIndex<Token> *index() const { return const_cast<TokenIndex *>(index_); }

    /** iterate over unique vocabulary IDs at this depth. */
    virtual VidIterator vid_begin() const { return VidIterator(*this, /* begin = */ true); }
    virtual VidIterator vid_end() const { return VidIterator(*this, /* begin = */ false); }

    /** iterate over Positions in this Span. */
    virtual PosIterator begin() const { return PosIterator(*this, /* begin = */ true); }
    virtual PosIterator end() const { return PosIterator(*this, /* begin = */ false); }

    virtual Span *copy() const { return new Span(*this); }

    /**
     * Number of Positions equal to 't' in this span.
     *
     * Currently only available on leaf nodes: just lazy.
     */
    virtual size_t StepSize(Token t) const {
      assert(in_array());
      size_t step = this->find_bounds_array_(t).size();
      assert(step > 0); // when this is 0, that may be because you are trying to iterate over a depth reaching into </s> entries
      return step;
    }

  protected:
    /** use TokenIndex::span() to construct an IndexSpan */
    Span(const TokenIndex &index);

    /** Span over the vid range of a TreeNode. use TreeNode::span() to construct an IndexSpan */
    Span(ITreeNode<Token> &node);

  private:
    static constexpr size_t STO_NOT_FOUND = static_cast<size_t>(-1);

    const TokenIndex *index_;

    std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
    std::vector<ITreeNode<Token> *> tree_path_; /** first part of path from root through the tree */
    std::vector<Range> array_path_; /** second part of path from leaf through the suffix array. These Ranges always index relative to the specific suffix array. */
    std::shared_ptr<SuffixArray> array_; /** hold on to the node's array as long as we exist - a concurrent writing thread may split it meanwhile */

    /** narrow() in suffix array.
     * returns > 0 on success, STO_NOT_FOUND on failure: for consistency with narrow_tree_() */
    size_t narrow_array_(Token t);

    /** find the bounds of an existing Token or insertion point of a new one */
    Range find_bounds_array_(Token t) const;

    /** narrow() in tree.
     * returns >= 0 on success, STO_NOT_FOUND on failure. (=0 stepping into empty SuffixArray leaf)
     */
    size_t narrow_tree_(Token t);
  };

  /** Returns the whole span of the entire index (empty lookup sequence). */
  virtual IndexSpan<Token> span() const;

  /** Returns the Span over all Positions. */
  virtual IndexSpan<Token> span(ITreeNode<Token> &node) const;

  virtual Corpus<Token> *corpus() const { return corpus_; }

  /**
   * Insert the existing Corpus Sentence into this index. Last token must be the EOS symbol </s>.
   *
   * AddSentence() must be called in increasing sequence of seqNum. Otherwise, calls will be ignored!
   *
   * This potentially splits existing suffix array leaves into individual TreeNodes,
   * and inserts Position entries into the suffix array. Hence, it is roughly an
   *
   * O(l * (k + log(n)))
   *
   * operation, with l = sent.size(), k = TreeNode<Token>::kMaxArraySize
   * and n = span().size() aka the full index size.
   *
   * Thread safety: one writer, multiple reader threads.
   * Readers always see a valid state, but no guarantees on when writes become visible.
   */
  virtual void AddSentence(const Sentence<Token> &sent, sto_updateid_t version = sto_updateid_t{static_cast<stream_t>(-1), static_cast<seqid_t>(-1)});

  /**
   * Merge all Positions from 'add' into this TokenIndex.
   *
   * 'add' must have a greater seqNum than us. Otherwise, calls will be ignored!
   */
  virtual void Merge(const ITokenIndex<Token> &add);
  virtual void Merge(const ITokenIndex<Token> &add, LeafMerger<Token, SuffixArray> &merger);

  /** Split the root node. */
  virtual void Split();

  /** Write to (empty) DB. */
  virtual void Write(std::shared_ptr<DB<Token>> db) const;

  /** current persistence sequence number */
  virtual StreamVersions streamVersions() const override { return streamVersions_; }

  /** call this after external low-level addition of Positions */
  virtual void Flush(StreamVersions streamVersions) override;

  virtual void DebugPrint(std::ostream &os, const std::unordered_map<Vid, std::string> &id2surface);

  /** for collecting profiling information */
  virtual std::shared_ptr<DB<Token>> GetDB() override;

  /** Insert the subsequence from start into this index. Potentially splits. */
  void AddSubsequence_(const Sentence<Token> &sent, Offset start);

private:
  friend class Span;

  Corpus<Token> *corpus_;
  TreeNodeT *root_; /** root of the index tree */
  StreamVersions streamVersions_; /** persistence sequence number */

  /**
   * Finalize an update with seqNum. Called internally after the update has been applied,
   * to flush writes to DB and apply a new persistence sequence number.
   */
  //void Ack(seq_t seqNum);
  void Flush();
};

} // namespace sto

#endif //STO_TOKENINDEX_H
