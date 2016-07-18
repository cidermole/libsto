/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TOKENINDEX_H
#define STO_TOKENINDEX_H

#include <iostream>
#include <vector>

#include "Range.h"
#include "SuffixArrayMemory.h"
#include "TreeNodeMemory.h"
#include "SuffixArrayDisk.h"
#include "TreeNodeDisk.h"
#include "Corpus.h"
#include "util/rbtree.hpp"

namespace sto {

template<class Token, class SuffixArray> class TreeNode;

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
 * Indexes a Corpus. The index is implemented as a hybrid suffix tree/array.
 *
 * Vocab note: vid of explicit sentence end delimiting symbol </s> must be at the very beginning of all vocab symbols
 * (sort order matters, shorter sequences must come first)!
 */
template<class Token, typename TypeTag = IndexTypeMemory>
class TokenIndex {
public:
  typedef typename Corpus<Token>::Offset Offset;

  typedef typename IndexTypes<Token, TypeTag>::SuffixArray SuffixArray;
  typedef typename IndexTypes<Token, TypeTag>::TreeNode TreeNodeT;


  /**
   * Load TokenIndex from mtt-build *.sfa file for the associated corpus.
   *
   * Currently, this creates one big suffix array (not memory mapped) regardless of maxLeafSize, but subsequent calls to
   * AddSentence() will honor maxLeafSize and split where necessary (so the first additions will be expensive).
   */
  TokenIndex(const std::string &filename, Corpus<Token> &corpus, size_t maxLeafSize = 10000);

  /** Construct an empty TokenIndex, i.e. this does not index the Corpus by itself. */
  TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize = 10000);
  ~TokenIndex();


  /**
   * IndexSpan represents the matched locations of a partial lookup sequence
   * within TokenIndex.
   *
   * You start with the empty lookup sequence from TokenIndex::span() and
   * keep adding tokens to the lookup via narrow().
   */
  class Span {
  public:
    friend class TokenIndex<Token, TypeTag>;
    typedef typename TokenIndex<Token, TypeTag>::TreeNodeT TreeNodeT;

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
    size_t narrow(Token t);

    /**
     * Random access to a position within the selected span.
     * O(log(n/k)) with with k = TreeNode<Token>::kMaxArraySize.
     *
     * When reading a SuffixArray leaf that is being written to,
     * returned Position values will always be valid, but
     * may be to the left of 'rel'.
     */
    Position<Token> operator[](size_t rel) const;

    // for testing!
    Position<Token> at_unchecked(size_t rel) const;

    /**
     * Number of token positions spanned in the index.
     *
     * Returns a size cached when narrow() was called.
     */
    size_t size() const;

    /** Length of lookup sequence, or the number of times narrow() has been called. */
    size_t depth() const;

    /** Distance from the root in number of TreeNodes. */
    size_t tree_depth() const;

    /** TreeNode at current depth. */
    TreeNodeT *node();

    /** first part of path from root through the tree, excluding suffix array range choices */
    const std::vector<TreeNodeT *>& tree_path() const { return tree_path_; }

    /** true if span reaches into a suffix array leaf. */
    bool in_array() const;

    /** partial lookup sequence so far, as appended by narrow() */
    const std::vector<Token>& sequence() const { return sequence_; }

    Corpus<Token> *corpus() const;

  protected:
    /** use TokenIndex::span() for constructing an IndexSpan */
    Span(const TokenIndex<Token, TypeTag> &index);

  private:
    static constexpr size_t STO_NOT_FOUND = static_cast<size_t>(-1);

    const TokenIndex<Token, TypeTag> *index_;

    std::vector<Token> sequence_; /** partial lookup sequence so far, as appended by narrow() */
    std::vector<TreeNodeT *> tree_path_; /** first part of path from root through the tree */
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
  Span span() const;

  Corpus<Token> *corpus() const { return corpus_; }

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
  void AddSentence(const Sentence<Token> &sent);

/*
  template<class IndexSpanMemory, class IndexSpanDisk>
  void Merge(const IndexSpanMemory &spanMemory, IndexSpanDisk &spanDisk);
  */

  void Merge(const TokenIndex<Token, IndexTypeMemory> &add);

  void DebugPrint(std::ostream &os);

private:
  friend class Span;

  Corpus<Token> *corpus_;
  TreeNodeT *root_; /** root of the index tree */

  /** Insert the subsequence from start into this index. Potentially splits. */
  void AddSubsequence_(const Sentence<Token> &sent, Offset start);
};

} // namespace sto

#endif //STO_TOKENINDEX_H
