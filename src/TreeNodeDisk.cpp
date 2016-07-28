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

#define static_assert(x) static_cast<void>(0)

namespace sto {

template<class Token>
TreeNodeDisk<Token>::TreeNodeDisk(std::string path, std::shared_ptr<DB<Token>> db, size_t maxArraySize) :
    TreeNode<Token, SuffixArrayDisk<Token>>(maxArraySize), path_(path), db_(db), sync_(false) {
  using namespace boost::filesystem;

  assert(db != nullptr);

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

    TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(child_path(vid), this->db_, this->kMaxArraySize);
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
template<class PositionSpan>
void TreeNodeDisk<Token>::MergeLeaf(const PositionSpan &addSpan, const Corpus<Token> &corpus, Offset depth) {
  assert(this->is_leaf());

  size_t addSize = addSpan.size();
  size_t curSize = this->array_->size();
  size_t newSize = curSize + addSize;
  size_t icur = 0, iadd = 0;

  SuffixArray &curSpan = *this->array_;

  // disallow splits of </s> - as argued in TreeNodeMemory::AddPosition()
  // bool allow_split = sent.size() + 1 > start + depth; // +1 for implicit </s>
  bool allow_split = curSize > 0 && corpus.sentence(curSpan[0].sid).size() + 1 > curSpan[0].offset + depth;

  SuffixArrayPosition<Token> *newArray = new SuffixArrayPosition<Token>[newSize];
  SuffixArrayPosition<Token> *pnew = newArray;

  assert(addSize > 0);
  Vid vid = depth > 0 ? Position<Token>(addSpan[0]).add(depth-1, corpus).vid(corpus) : 0;

  // to do: if Span had an iterator, we could use std::merge() here.

  Position<Token> cur;
  Position<Token> add;
  while(icur < curSize && iadd < addSize) {
    // assuming that curSpan is much larger, this loop will be most active
    add = addSpan[iadd];
    while(icur < curSize && add.compare((cur = curSpan[icur]), corpus)) { // add > cur
      assert(depth == 0 || cur.add(depth-1, corpus).vid(corpus) == vid); // since span over the same vid -> each Position (at depth-1) should have the same vid.
      *pnew++ = cur;
      icur++;
    }
    assert(depth == 0 || add.add(depth-1, corpus).vid(corpus) == vid); // since span over the same vid -> each Position (at depth-1) should have the same vid.
    *pnew++ = add;
    iadd++;
  }
  // fill from the side that still has remaining positions
  while(icur < curSize) {
    cur = curSpan[icur++];
    *pnew++ = cur;
  }
  while(iadd < addSize) {
    add = addSpan[iadd++];
    *pnew++ = add;
  }

#ifndef NDEBUG
  // postconditions
  assert(pnew - newArray == (ptrdiff_t) newSize); // filled the entire array

  // array is sorted in ascending order
  for(size_t i = 0; i < newSize - 1; i++) {
    Position<Token> p = newArray[i], q = newArray[i+1];
    //assert(p <= q) == assert(!(p > q));
    assert(!p.compare(q, corpus));
  }
#endif

  if(sync_) {
    // overwrite the DB key now; the existing array_ continues to hold the old data afterwards
    WriteArray(&newArray, newArray + newSize);
    // map the newly written array, and atomically replace the old array_
    this->array_.reset(new SuffixArrayDisk<Token>());
    this->db_->GetNodeLeaf(array_path(), *this->array_);
    // to do: if WriteArray() didn't take ownership, we could use the raw data in newArray (instead of reading it back)
  } else {
    // this copies twice here, but nevermind -- just for testing, I/O will be much slower anyway
    std::string bytes(reinterpret_cast<const char *>(newArray), sizeof(SuffixArrayPosition<Token>) * newSize);
    this->array_.reset(new SuffixArrayDisk<Token>(bytes));
    delete[] newArray;
    newArray = nullptr;
  }

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
    SplitNode(corpus, depth);
  }
}

template<class Token>
void TreeNodeDisk<Token>::Merge(typename TokenIndex<Token, IndexTypeMemory>::Span &spanMemory, typename TokenIndex<Token, IndexTypeDisk>::Span &spanDisk) {
  // iterate through children, recursively calling Merge() until we reach the TreeNodeDisk leaf level.
  // the memorySpan may not hit leaves at the same depth, but we can still iterate over the entire span to merge it.

  //const typename TokenIndex<Token>::Span &spanMemory = spanMemory_x; // TODO temp
  //typename TokenIndex<Token>::Span &spanDisk = spanDisk_x; // TODO temp

  // to do: this is similar to TokenIndex::AddSubsequence_()

  assert(spanDisk.node() == this);

  if(spanDisk.node()->is_leaf()) {
    MergeLeaf(spanMemory, *spanDisk.corpus(), (Offset) spanDisk.depth());
    return;
  }

  for(auto vid : spanMemory) {
    typename TokenIndex<Token, IndexTypeMemory>::Span spanm = spanMemory;
    size_t num_new = spanm.narrow(Token{vid});

    assert(num_new > 0); // since we iterate precisely spanMemory

    typename TokenIndex<Token, IndexTypeDisk>::Span spand = spanDisk;
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
}

template<class Token>
void TreeNodeDisk<Token>::AddLeaf(Vid vid) {
  //assert(!this->children_.Find(vid)); // appending to children assumes that this vid is new
  this->children_[vid] = new TreeNodeDisk<Token>(child_path(vid), this->db_, this->kMaxArraySize);

  if(sync_) {
    // (in reality, we could just append to the existing blob of vids... child order does not matter)
    WriteChildren();
  }
}

template<class Token>
void TreeNodeDisk<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  TreeNode<Token, SuffixArray>::SplitNode(corpus, depth, std::bind(&TreeNodeDisk<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

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
TreeNodeDisk<Token> *TreeNodeDisk<Token>::make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus, Offset depth) {
  TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(child_path(vid), this->db_, this->kMaxArraySize);
  new_child->MergeLeaf(SuffixArrayPositionSpan<Token>(first.ptr(), last.ptr()), corpus, depth);
  //new_array->insert(new_array->begin(), first, last); // this is the TreeNodeMemory interface. Maybe we could have implemented insert() here on SuffixArrayDisk, and use a common call?
  return new_child;
}

template<class Token>
void TreeNodeDisk<Token>::WriteArray(SuffixArrayPosition<Token> **first, SuffixArrayPosition<Token> *last) {
  db_->PutNodeLeaf(array_path(), *first, (last - *first));
  delete[] *first;
  *first = nullptr;
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
