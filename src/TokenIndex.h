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

#include "Range.h"
#include "SuffixArrayMemory.h"
#include "TreeNodeMemory.h"
#include "SuffixArrayDisk.h"
#include "TreeNodeDisk.h"
#include "Corpus.h"
#include "util/rbtree.hpp"
#include "ITokenIndex.h"

namespace sto {

template<class Token, class SuffixArray> class TreeNode;

template<class Token> class DB;

struct IndexTypeMemory {};
struct IndexTypeDisk {};

/** types for TokenIndex with different backing (Disk or Memory) */
template<typename Token, typename IndexTypeTag>
struct IndexTypes {
  // specialized below
  typedef void SuffixArray;
  typedef void TreeNode;
};

/** partial specialization: types for IndexTypeDisk */
template<>
template<typename Token>
struct IndexTypes<Token, IndexTypeDisk> {
  typedef SuffixArrayDisk<Token> SuffixArray;
  typedef TreeNodeDisk<Token> TreeNode;
};

/** partial specialization: types for IndexTypeMemory */
template<>
template<typename Token>
struct IndexTypes<Token, IndexTypeMemory> {
  typedef SuffixArrayMemory<Token> SuffixArray;
  typedef TreeNodeMemory<Token> TreeNode;
};

/**
 * Implementation of TokenIndex::AddSentence() that can be specialized
 * for testing IndexTypeDisk.
 */
template<class Token, typename TypeTag>
struct AddSentenceImpl {
  void operator()(const Sentence<Token> &sent);
  AddSentenceImpl(TokenIndex<Token, TypeTag> &index);
private:
  TokenIndex<Token, TypeTag> &index_;
};


/**
 * Partial specialization of AddSentenceImpl for IndexTypeDisk.
 * for testing IndexTypeDisk using AddSentence().
 */
template<class Token>
struct AddSentenceImpl<Token, IndexTypeDisk> {
  static constexpr size_t kMaxLeafSizeMem = 10000;

  void operator()(const Sentence<Token> &sent);

  AddSentenceImpl(TokenIndex<Token, IndexTypeDisk> &index);

private:
  std::unique_ptr<TokenIndex<Token, IndexTypeMemory>> memBuffer;
  size_t nsents = 0;
  //size_t kBatchSize = 10000; /** batch size in number of sents */  // (used this for individually running BenchmarkTests.index_100k_disk) -- breaks other tests since we don't have a flush at the end
  size_t kBatchSize = 1; /** batch size in number of sents */

  TokenIndex<Token, IndexTypeDisk> &index_;
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

    // note: use TokenIndex::span() for constructing an IndexSpan

    Span(const Span &other) = default;
    Span(Span &&other) = default;

    Span& operator=(const Span &other) = default;
    Span& operator=(Span &&other) = default;

    /**
     * Narrow the span by adding a token to the end of the lookup sequence.
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
     * Returns a size cached when narrow() was called.
     */
    virtual size_t size() const;

    /** Length of lookup sequence, or the number of times narrow() has been called. */
    virtual size_t depth() const;

    /** Distance from the root in number of TreeNodes. */
    virtual size_t tree_depth() const;

    /** TreeNode at current depth. */
    virtual ITreeNode<Token> *node() const;

    /** first part of path from root through the tree, excluding suffix array range choices */
    virtual const std::vector<ITreeNode<Token> *>& tree_path() const { return tree_path_; }

    /** true if span reaches into a suffix array leaf. */
    virtual bool in_array() const;

    virtual Range array_range() const { assert(array_path_.size() > 0); return array_path_.back(); }

    /** partial lookup sequence so far, as appended by narrow() */
    virtual const std::vector<Token>& sequence() const { return sequence_; }

    virtual Corpus<Token> *corpus() const;

    /** iterate over unique vocabulary IDs at this depth. */
    virtual VidIterator begin() const { return VidIterator(*this, /* begin = */ true); }
    virtual VidIterator end() const { return VidIterator(*this, /* begin = */ false); }

    virtual Span *copy() const { return new Span(*this); }

    /**
     * @returns the number of Positions comparing equal from the current index,
     * aka the number of Positions to skip to get to the next vid at the current depth
     *
     * Currently only available on leaf nodes, but just lazy.
     */
    virtual size_t StepSize(size_t index) const {
      assert(in_array());

      // much akin to code in TreeNode::SplitNode()... unite?
      typedef typename TreeNodeT::SuffixArrayT::iterator iter;

      // note: the real type of the SuffixArrayDisk range are AtomicPosition. But Position<->AtomicPosition are convertible
      //
      // #include <type_traits>
      //bool same = std::is_same<typename TreeNodeT::SuffixArrayT::value_type, AtomicPosition<Token>>::value;
      //assert(same);

      Position<Token> pos = (*this)[index];

      Corpus<Token> &corpus = *(*this).corpus();
      size_t depth = (*this).depth();
      auto comp = [&corpus, depth](const Position<Token> a, const Position<Token> b) {
        // the suffix array at this depth should only contain positions that continue long enough without the sentence ending
        return a.add(depth, corpus).vid(corpus) < b.add(depth, corpus).vid(corpus);
      };

      // TODO: move to TreeNode
      TreeNodeT *node = dynamic_cast<TreeNodeT *>(this->node());
      std::pair<iter, iter> vid_range = std::equal_range(node->array()->begin() + (*this).array_range().begin + index, node->array()->begin() + (*this).array_range().end, pos, comp);
      size_t step = vid_range.second - vid_range.first;
      assert(step > 0);
      return step;
    }

  protected:
    /** use TokenIndex::span() for constructing an IndexSpan */
    Span(const TokenIndex &index);

  private:
    static constexpr size_t STO_NOT_FOUND = static_cast<size_t>(-1);

    const TokenIndex *index_;

    std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
    std::vector<ITreeNode<Token> *> tree_path_; /** first part of path from root through the tree */
    std::vector<Range> array_path_; /** second part of path from leaf through the suffix array. These Ranges always index relative to the specific suffix array. */

    /** narrow() in suffix array.
     * returns > 0 on success, STO_NOT_FOUND on failure: for consistency with narrow_tree_() */
    size_t narrow_array_(Token t);

    /** find the bounds of an existing Token or insertion point of a new one */
    Range find_bounds_array_(Token t);

    /** narrow() in tree.
     * returns >= 0 on success, STO_NOT_FOUND on failure. (=0 stepping into empty SuffixArray leaf)
     */
    size_t narrow_tree_(Token t);
  };

  /** Returns the whole span of the entire index (empty lookup sequence). */
  virtual IndexSpan<Token> span() const;

  virtual Corpus<Token> *corpus() const { return corpus_; }

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
  virtual void AddSentence(const Sentence<Token> &sent);

  virtual void Merge(const TokenIndex<Token, IndexTypeMemory> &add);

  virtual void Merge(const ITokenIndex<Token> &add) { assert(0); } // TODO: assert convertible to TokenIndex<Token, IndexTypeMemory>, use above Merge()

  // not implemented  TODO remove this
  virtual void Merge(const TokenIndex<Token, IndexTypeDisk> &add);

  /** Write to (empty) DB. */
  virtual void Write(std::shared_ptr<DB<Token>> db) const;


  virtual void DebugPrint(std::ostream &os);

  /** Insert the subsequence from start into this index. Potentially splits. */
  void AddSubsequence_(const Sentence<Token> &sent, Offset start);

private:
  friend class Span;

  Corpus<Token> *corpus_;
  TreeNodeT *root_; /** root of the index tree */
  AddSentenceImpl<Token, TypeTag> add_buffer_; /** buffer caching AddSentence() calls */
};

} // namespace sto

#endif //STO_TOKENINDEX_H
