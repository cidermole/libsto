/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <array>

#include <boost/filesystem.hpp>

#include "TreeNodeDisk.h"
#include "SuffixArrayMemory.h"
#include "TokenIndex.h"
#include "DB.h"

#include "rocksdb/db.h"
#include "ITokenIndex.h"

#define static_assert(x) static_cast<void>(0)

namespace sto {

template<class Token>
TreeNodeDisk<Token>::TreeNodeDisk(ITokenIndex<Token> &index, size_t maxArraySize, std::string path, std::shared_ptr<DB<Token>> db, ITreeNode<Token> *parent, Vid vid) :
    TreeNode<Token, SuffixArrayDisk<Token>>(index, maxArraySize, parent, vid), path_(path), db_(db), sync_(true) {
  using namespace boost::filesystem;

  assert(db != nullptr);

  if(is_root())
    seqNum_ = db_->GetSeqNum();

  // TODO: no need for SuffixArrayDisk. We keep everything in memory anyway.
  // (we could unify SuffixArrayDisk and SuffixArrayMemory)

  this->is_leaf_ = db_->IsNodeLeaf(path_) ? true : false;

  if(this->is_leaf_) {
    this->array_.reset(new SuffixArrayDisk<Token>());
    db_->GetNodeLeaf(array_path(), *this->array_);
  } else {
    // recursively load the subtree rooted at this path
    //LoadSubtree(reinterpret_cast<const Vid *>(children.data()), children.size() / sizeof(Vid));
    std::vector<Vid> children;
    db_->GetNodeInternal(path_, children);
    LoadSubtree(children.data(), children.size());
  }
}


template<class Token>
void TreeNodeDisk<Token>::LoadSubtree(const Vid *children, size_t num_children) {
  using namespace boost::filesystem;

  const Vid *end = children + num_children;
  for(const Vid *c = children; c != end; c++) {
    Vid vid = *c;

    TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(this->index_, this->kMaxArraySize, child_path(vid), this->db_, this, vid);
    size_t new_size = new_child->size();
    this->children_.FindOrInsert(vid, /* add_size = */ new_size) = new_child;

#ifndef NDEBUG
    TreeNode<Token, SuffixArray> *n = nullptr;
    assert(this->children_.Find(vid, &n));
    assert(n == new_child);
    assert(this->children_.ChildSize(vid) == new_child->size());
#endif
  }
}

template<class Token>
bool TreeNodeDisk<Token>::find_child_(Vid vid, TreeNodeDisk<Token> **child) {
  return TreeNode<Token, SuffixArray>::find_child_(vid, reinterpret_cast<TreeNode<Token, SuffixArray> **>(child));
}

template<class Token>
void TreeNodeDisk<Token>::Ack(seq_t seqNum) {
  //assert(is_root()); // only valid conceptually at the root node.
  assert(seqNum > seqNum_);
  if(seqNum <= seqNum_)
    return;

  seqNum_ = seqNum;
  db_->PutSeqNum(seqNum_);
  db_->Flush();
}

template<class Token>
seq_t TreeNodeDisk<Token>::seqNum() const {
  //assert(is_root()); // only valid at the root node.
  return seqNum_;
}

template<class Token>
template<class PositionSpan>
void TreeNodeDisk<Token>::MergeLeaf(const PositionSpan &addSpan, const Corpus<Token> &corpus) {
  assert(this->is_leaf());

  // Merge two sorted Position ranges: one from memory (addSpan) and one from disk (this node).
  // Since this is where we grow, it may be necessary to split this node afterwards.

  // note: for persistence to be crash-safe, we must tolerate it if some Positions have already
  // been persisted (from a previously crashed run) --> we have to omit duplicate Positions

  size_t depth = this->depth_;

  size_t addSize = addSpan.size();
  size_t curSize = this->array_->size();
  size_t newSize = curSize + addSize;

  SuffixArray &curSpan = *this->array_;
  //IndexSpan<Token> curSpan = this->span(); // IndexSpan, why r u so expensive to query using operator[]?

  // disallow splits of </s> - as argued in TreeNodeMemory::AddPosition()
  // bool allow_split = sent.size() + 1 > start + depth; // +1 for implicit </s>
  bool allow_split = (curSize > 0 && corpus.sentence(curSpan[0].sid).size() + 1 > curSpan[0].offset + depth) ||
                     (addSize > 0 && corpus.sentence(addSpan[0].sid).size() + 1 > addSpan[0].offset + depth);
  // because shorter sequences come first in lexicographic order, we can compare the length of the first entry
  // (of either available index -- either cur or add may be empty, unfortunately)

  std::unique_ptr<SuffixArrayPosition<Token>[]> newArray(new SuffixArrayPosition<Token>[newSize]);
  SuffixArrayPosition<Token> *pnew = newArray.get();

  // the only time we may get a zero-size leaf is if we are merging in an empty addSpan (and even then, only with a leaf root on disk)
  assert(addSize > 0 || depth == 0);

  // merge the two spans' Positions into newArray
  //PosComp<Token> comp(corpus, depth);
  PosComp<Token> comp(corpus, 0);
  pnew = std::set_union(curSpan.begin(), curSpan.end(), addSpan.begin(), addSpan.end(), pnew, comp);

  newSize = pnew - newArray.get(); // if we skipped duplicate entries, this may now be smaller than newSize before

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
    Position<Token> p = newArray[i], q = newArray[i+1];
    //assert(p <= q) == assert(!(p > q)); // equivalent formula if we had > operator
    assert(!q.compare(p, corpus)); // ascending order
    assert(!(p == q)); // ensure no duplicates
  }
#endif

  if(sync_) {
    // overwrite the DB key now; the existing array_ continues to hold the old data afterwards
    db_->PutNodeLeaf(array_path(), newArray.get(), newSize);
  }
  // atomically replace the old array_
  this->array_.reset(new SuffixArrayDisk<Token>(newArray.get(), newSize));

  newArray.reset();

  assert(this->array_->size() == newSize);

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
void TreeNodeDisk<Token>::Merge(IndexSpan<Token> &spanMemory, IndexSpan<Token> &spanDisk) {
  // iterate through children, recursively calling Merge() until we reach the TreeNodeDisk leaf level.
  // spanMemory may not hit leaves at the same depth, but we can still iterate over the entire span to merge it.

  assert(spanDisk.node() == this);

  if(spanDisk.node()->is_leaf()) {
    // trick: IndexSpan::begin() iterates over vid,
    // but ITokenIndexSpan::begin() iterates over Positions, which is what we want here.
    MergeLeaf(*spanMemory.get(), *spanDisk.corpus());
    return;
  }

  for(auto vid : spanMemory) {
    IndexSpan<Token> spanm = spanMemory;
    size_t num_new = spanm.narrow(Token{vid});

    assert(num_new > 0); // since we iterate precisely spanMemory

    IndexSpan<Token> spand = spanDisk;
    size_t spanSize = spand.narrow(Token{vid});
    if(spanSize == 0) {
      // (1) create tree entry (leaf)
      spand.node()->AddLeaf(vid);
      spand.narrow(Token{vid}); // step IndexSpan into the node just created (which contains an empty SA)
      assert(spand.in_array());
    }
    spand.node()->Merge(spanm, spand);
    this->AddSize(vid, num_new);
  }

  if(spanDisk.depth() == 0) {
    //db_->CompactRange(); // compact the entire database
  }
}

template<class Token>
void TreeNodeDisk<Token>::AddLeaf(Vid vid) {
  //assert(!this->children_.Find(vid)); // appending to children assumes that this vid is new
  this->children_[vid] = new TreeNodeDisk<Token>(this->index_, this->kMaxArraySize, child_path(vid), this->db_, this, vid);

  if(sync_) {
    // (in reality, we could just append to the existing blob of vids [in RAM]... child order does not matter)

    // note: for persistence to be crash-safe, we must tolerate it if some child vids have already
    // been persisted (from a previously crashed run)
    WriteChildren();
  }
}

template<class Token>
void TreeNodeDisk<Token>::SplitNode(const Corpus<Token> &corpus) {
  TreeNode<Token, SuffixArray>::SplitNode(corpus, std::bind(&TreeNodeDisk<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

  if(sync_) {
    // update the children
    WriteChildren();
    db_->DeleteNodeLeaf(array_path());
  }
}

template<class Token>
std::string TreeNodeDisk<Token>::child_sub_path(Vid vid) {
  constexpr size_t kVidDigits = sizeof(Vid)*2;
  constexpr size_t kSignificantDigitsDir2 = 3;
  //static_assert(kVidDigits > kSignificantDigitsDir2); // for dir1 size to work

  // e.g. vid=0x0007a120 -> "000007a/0007a120" (dir1/dir2)
  std::stringstream hexVid;
  hexVid << std::setfill('0') << std::setw((int)kVidDigits) << std::hex << vid;
  std::string dir1 = hexVid.str().substr(0, kVidDigits - kSignificantDigitsDir2);
  std::string dir2 = hexVid.str(); // dir2 has the full number again, for completeness

  std::stringstream path;
  path << dir1 << "/" << dir2;
  return path.str();
}

template<class Token>
TreeNodeDisk<Token> *TreeNodeDisk<Token>::make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus) {
  TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(this->index_, this->kMaxArraySize, child_path(vid), this->db_, this, vid);
  new_child->MergeLeaf(SuffixArrayPositionSpan<Token>(first.ptr(), last.ptr()), corpus);
  //new_array->insert(new_array->begin(), first, last); // this is the TreeNodeMemory interface. Maybe we could have implemented insert() here on SuffixArrayDisk, and use a common call?
  return new_child;
}

template<class Token>
void TreeNodeDisk<Token>::WriteChildren() {
  std::vector<Vid> children;
  for(Vid vid : this->children_)
    children.push_back(vid);
  db_->PutNodeInternal(path_, children);
}

// explicit template instantiation
template class TreeNodeDisk<SrcToken>;
template class TreeNodeDisk<TrgToken>;

} // namespace sto
