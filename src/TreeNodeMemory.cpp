/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TreeNodeMemory.h"
#include "Corpus.h"
#include "MappedFile.h"
#include "TokenIndex.h"
#include "ITokenIndex.h"

#include "util/Time.h"

#include <algorithm>
#include <functional>

namespace sto {


template<class Token>
TreeNodeMemory<Token>::TreeNodeMemory(ITokenIndex<Token> &index, size_t maxArraySize, std::string filename, std::shared_ptr<void>, ITreeNode<Token> *parent, Vid vid):
    TreeNode<Token, SuffixArrayMemory<Token>>(index, maxArraySize, parent, vid)
{
  this->array_.reset(new SuffixArray);
  if(filename != "")
    LoadArray(filename);
}

template<class Token>
void TreeNodeMemory<Token>::AddPosition(const Sentence<Token> &sent, Offset start) {
  assert(this->is_leaf()); // Exclusively for adding to a SA (leaf node).

  Position<Token> corpus_pos{sent.sid(), start};
  const Corpus<Token> &corpus = sent.corpus();
  std::shared_ptr<SuffixArray> array = this->array_;

  // find insert position in sorted suffix array
  // thread safety: single writer guarantees that the insert_pos will still be valid later below

  auto insert_pos = std::upper_bound(
      array->begin(), array->end(),
      corpus_pos,
      [&corpus](const Position<Token> &a, const Position<Token> &b) {
        return a.compare(b, corpus);
      }
  );

  // thread-safe insertion

  if(array->capacity() >= array->size() + 1) {
    // safe to insert, no reallocation: shifting elements backwards. readers may observe either old or shifted elements.
    array->insert(insert_pos, corpus_pos);
  } else {
    // reallocation must happen. we do this manually (instead of using vector::insert()) to avoid locking.

    // in a new copy, prepare the new state of the vector
    std::shared_ptr<SuffixArray> resized = std::make_shared<SuffixArray>();
    resized->reserve(std::max<size_t>(1, array->capacity() * 2)); // reserve at least 1, because capacity starts at 0
    resized->insert(resized->begin(), array->begin(), insert_pos); // copy first part
    resized->push_back(corpus_pos); // new item
    resized->insert(resized->end(), insert_pos, array->end()); // copy second part

    this->array_ = resized; // atomic replace

    array = this->array_;
  }

  /*
   * disallow splits of </s>
   *
   * We currently don't have a principled way of splitting </s>, as there are no subsequent tokens to compare.
   * Therefore, the SA leaf of </s> (especially below ".", i.e. the sequence ". </s>") may grow above kMaxArraySize.
   * It will grow approx. to the number of Corpus sentences indexed.
   *
   * If really necessary, we could think about splitting the same token into a tree. Note that we already have a tree
   * structure (RBTree TreeNode::children_) for partial sums. Maybe this modification could be attached there.
   *
   * It may seem from the suffix trie that </s> doesn't really convey any information, and it could be collapsed
   * into a single number. However, the leaves contain the Corpus Positions of the entire path to the suffix,
   * which we want to sample.
   */
  bool allow_split = sent.size() + 1 > start + this->depth_; // +1 for implicit </s>

  if(array->size() > this->kMaxArraySize && allow_split) {
    SplitNode(corpus); // suffix array grown too large, split into TreeNode
  }

  // update sizes for the TreeNodes above us

  // add to cumulative count for internal TreeNodes (excludes SA leaves which increment in AddPosition()), including the root (if it's not a SA)
  // add sizes from deepest level towards root, so that readers will see a valid state (children being at least as big as they should be)
  // avoid leaf, potentially avoid most recent internal TreeNode created by a split above.
  TreeNodeMemory<Token> *child = this;
  TreeNodeMemory<Token> *parent = dynamic_cast<TreeNodeMemory<Token> *>(this->parent());

  while(parent) {
    parent->AddSize(child->vid(), 1);
    child = parent;
    parent = dynamic_cast<TreeNodeMemory<Token> *>(parent->parent());
  }
}

template<class Token>
void TreeNodeMemBuf<Token>::AddPosition(const Sentence<Token> &sent, Offset start) {
  assert(this->is_leaf()); // Exclusively for adding to a SA (leaf node).

  Position<Token> corpus_pos{sent.sid(), start};

  // unsorted insert. this means that our state is invalid until we are actually sorted, but this should be much quicker.
  this->array_->push_back(corpus_pos);

  assert(this->parent() == nullptr); // must be leaf-only for now
}

template<class Token>
void TreeNodeMemBuf<Token>::EnsureSorted(const Corpus<Token> &corpus) {
  auto array = this->array_;
  if(array->size() > lastSortSize_.load()) {
    PosComp<Token> comp(corpus);
    std::cerr << "EnsureSorted() sorting TreeNodeMemBuf..." << std::endl;

    double t = benchmark_time([&](){
      std::sort(array->begin(), array->end(), comp);
    });

    std::cerr << "EnsureSorted() sorting took " << format_time(t) << " s" << std::endl;
    lastSortSize_.store(array->size());
  }
}

template<class Token>
void TreeNodeMemory<Token>::AddLeaf(Vid vid) {
  this->children_[vid] = new TreeNodeMemory<Token>(this->index_, this->kMaxArraySize, "", nullptr, this, vid);
}

template<class Token>
void TreeNodeMemory<Token>::MergeLeaf(const ITokenIndexSpan<Token> &addSpan, LeafMerger<Token, SuffixArray> &merger) {
  assert(this->is_leaf());

  // Merge two sorted Position ranges: one from memory (addSpan) and one from disk (this node).
  // Since this is where we grow, it may be necessary to split this node afterwards.

  // note: for persistence to be crash-safe, we must tolerate it if some Positions have already
  // been persisted (from a previously crashed run) --> we have to omit duplicate Positions

  size_t depth = this->depth_;

  size_t addSize = addSpan.size();
  size_t curSize = this->array_->size();
  //size_t newSize = curSize + addSize;

  // the only time we may get a zero-size leaf is if we are merging in an empty addSpan (and even then, only with a leaf root on disk)
  assert(addSize > 0 || depth == 0);

  SuffixArray &curSpan = *this->array_;
  //IndexSpan<Token> curSpan = this->span(); // IndexSpan, why r u so expensive to query using operator[]?

  // disallow splits of </s> - as argued in TreeNodeMemory::AddPosition()
  // bool allow_split = sent.size() + 1 > start + depth; // +1 for implicit </s>
  Corpus<Token> &corpus = *addSpan.corpus();
  bool allow_split = (curSize > 0 && corpus.sentence(Position<Token>(curSpan[0]).sid).size() + 1 > Position<Token>(curSpan[0]).offset + depth) ||
                     (addSize > 0 && corpus.sentence(Position<Token>(addSpan[0]).sid).size() + 1 > Position<Token>(addSpan[0]).offset + depth);
  // because shorter sequences come first in lexicographic order, we can compare the length of the first entry
  // (of either available index -- either cur or add may be empty, unfortunately)

  // build new array and replace atomically
  this->array_ = merger.MergeLeafArray(this->array_, addSpan);

  /*
   * note: should it become necessary to split </s> array, a simple sharding concept
   * would involve fixed-size blocks. For that, we need to change Merge() to deal with shards
   * and SuffixArrayDisk to transparently map access to several blocks as one sequence.
   *
   * We implement a much easier workaround: for allow_split=false arrays (like ". </s>"), appending the new
   * Positions will always be legal. Therefore, don't build in RAM, and just append to the file on disk.
   * See above at if(!allow_split) "optimized case".
   */

  //assert(allow_split);
  if(allow_split && this->array_->size() > this->kMaxArraySize) {
    SplitNode(corpus);
  }
}

template<class Token>
std::shared_ptr<typename TreeNodeMemory<Token>::SuffixArray> TreeNodeMemory<Token>::MergeLeafArray(std::shared_ptr<typename TreeNodeMemory<Token>::SuffixArray> curSpan, const ITokenIndexSpan<Token> &addSpan) {
  Corpus<Token> &corpus = *addSpan.corpus();
  size_t newSize = curSpan->size() + addSpan.size();
  std::shared_ptr<SuffixArray> newArray = std::make_shared<SuffixArray>(newSize);
  auto *pnew = newArray->data();

  // merge the two spans' Positions into newArray
  //PosComp<Token> comp(corpus, depth);
  PosComp<Token> comp(corpus, 0);
  pnew = std::set_union(curSpan->begin(), curSpan->end(), addSpan.begin(), addSpan.end(), pnew, comp);
  //pnew = std::merge(curSpan->begin(), curSpan->end(), addSpan.begin(), addSpan.end(), pnew, comp); // no support for skipping dupes

  newSize = pnew - newArray->data(); // if we skipped duplicate entries, this may now be smaller than newSize before

#ifndef NDEBUG
  // postconditions
  //assert(pnew - newArray.get() == (ptrdiff_t) newSize); // filled the entire array
  // <<< except if we are skipping duplicate entries

#if 0
  // debug prints
  std::cerr << "curSpan len=" << curSize << ":" << std::endl;
  for(size_t i = 0; i < curSize; i++)
    std::cerr << i << " " << curSpan[i].DebugStr(corpus) << std::endl;

  std::cerr << "addSpan len=" << addSize << ":" << std::endl;
  for(size_t i = 0; i < addSize; i++)
    std::cerr << i << " " << addSpan[i].DebugStr(corpus) << std::endl;

  std::cerr << "newArray len=" << newSize << ":" << std::endl;
  for(size_t i = 0; i < newSize; i++)
    std::cerr << i << " " << Position<Token>(newArray[i]).DebugStr(corpus) << std::endl;
#endif

  // array is sorted in ascending order
  for(size_t i = 0; i + 1 < newSize; i++) {
    Position<Token> p = (*newArray)[i], q = (*newArray)[i+1];
    //assert(p <= q) == assert(!(p > q)); // equivalent formula if we had > operator
    assert(!q.compare(p, corpus, /* pos_order_dupes = */ false)); // ascending order (tolerates old v2 mtt-build style order)
    assert(!(p == q)); // ensure no duplicates
  }
#endif

  newArray->resize(newSize);
  return newArray;
}

template<class Token>
bool TreeNodeMemory<Token>::find_child_(Vid vid, TreeNodeMemory<Token> **child) {
  return TreeNode<Token, SuffixArray>::find_child_(vid, reinterpret_cast<TreeNode<Token, SuffixArray> **>(child));
}

template<class Token>
bool TreeNodeMemBuf<Token>::find_child_(Vid vid, TreeNodeMemBuf<Token> **child) {
  return TreeNode<Token, SuffixArray>::find_child_(vid, reinterpret_cast<TreeNode<Token, SuffixArray> **>(child));
}

template<class Token>
TreeNodeMemory<Token> *TreeNodeMemory<Token>::make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus) {
  TreeNodeMemory<Token> *new_child = new TreeNodeMemory<Token>(this->index_, this->kMaxArraySize, "", nullptr, this, vid);
  std::shared_ptr<SuffixArray> new_array = new_child->array_;
  new_array->insert(new_array->begin(), first, last);
  return new_child;
}

template<class Token>
TreeNodeMemBuf<Token> *TreeNodeMemBuf<Token>::make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus) {
  TreeNodeMemBuf<Token> *new_child = new TreeNodeMemBuf<Token>(this->index_, this->kMaxArraySize, "", nullptr, this, vid);
  std::shared_ptr<SuffixArray> new_array = new_child->array_;
  new_array->insert(new_array->begin(), first, last);
  return new_child;
}

template<class Token>
void TreeNodeMemory<Token>::SplitNode(const Corpus<Token> &corpus) {
  TreeNode<Token, SuffixArrayMemory<Token>>::SplitNode(corpus, std::bind(&TreeNodeMemory<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

template<class Token>
void TreeNodeMemBuf<Token>::SplitNode(const Corpus<Token> &corpus) {
  TreeNode<Token, SuffixArrayMemory<Token>>::SplitNode(corpus, std::bind(&TreeNodeMemBuf<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

template<class Token>
void TreeNodeMemory<Token>::LoadArray(const std::string &filename) {
  typedef tpt::id_type Sid_t;
  typedef tpt::offset_type Offset_t;
  typedef tpt::TsaHeader TokenIndexHeader;

  seqNum_ = 1; // for legacy data, to make tests happy

  MappedFile file(filename);
  TokenIndexHeader &header = *reinterpret_cast<TokenIndexHeader *>(file.ptr);

  if(header.versionMagic != tpt::INDEX_V2_MAGIC) {
    throw std::runtime_error(std::string("unknown version magic in ") + filename);
  }

  size_t num_positions = (header.idxStart - sizeof(TokenIndexHeader)) / (sizeof(Sid_t) + sizeof(Offset_t));
  // we could also sanity-check index size against the vocabulary size.

  //std::cerr << "sizeof(TokenIndexHeader) = " << sizeof(TokenIndexHeader) << std::endl;
  //std::cerr << "num_positions = " << num_positions << std::endl;

  std::shared_ptr<SuffixArray> array = std::make_shared<SuffixArray>();
  tpt::TsaPosition *p = reinterpret_cast<tpt::TsaPosition *>(file.ptr + sizeof(TokenIndexHeader));
  for(size_t i = 0; i < num_positions; i++, p++) {
    array->push_back(Position<Token>{p->sid, p->offset});
  }
  this->array_ = array;
}

// explicit template instantiation
template class TreeNodeMemory<SrcToken>;
template class TreeNodeMemory<TrgToken>;

template class TreeNodeMemBuf<SrcToken>;
template class TreeNodeMemBuf<TrgToken>;

} // namespace sto
