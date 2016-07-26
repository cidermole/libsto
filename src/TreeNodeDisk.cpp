/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TreeNodeDisk.h"
#include "SuffixArrayMemory.h"
#include "TokenIndex.h"

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <array>

#include <boost/filesystem.hpp>

#include "rocksdb/db.h"

#define static_assert(x) static_cast<void>(0)

namespace sto {

template<class Token>
TreeNodeDisk<Token>::TreeNodeDisk(std::string path, rocksdb::DB *db, size_t maxArraySize) :
    TreeNode<Token, SuffixArrayDisk<Token>>(maxArraySize), path_(path), db_(db)
{
  using namespace boost::filesystem;

  /*
   * if path exists: load the leaf / recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   */
  if(exists(path.c_str())) {
    //this->is_leaf_ = exists(array_path().c_str());
    this->is_leaf_ = true; // TODO
  } else {
    //create_directories(path.c_str());
    //ofstream(array_path().c_str()); // create empty suffix array
    this->is_leaf_ = true;
    // TODO
  }

  if(this->is_leaf_) {
    this->array_.reset(new SuffixArrayDisk<Token>(array_path(), db_));
  } else {
    LoadSubtree();
  }
}


template<class Token>
void TreeNodeDisk<Token>::LoadSubtree() {
  using namespace boost::filesystem;

  // first level:
  directory_iterator i = directory_iterator(path_.c_str()), iend = directory_iterator();
  for(; i != iend; ++i) {
    const path &p1 = i->path();
    if(!is_directory(p1))
      continue;
    // second level:
    directory_iterator j = directory_iterator(path_.c_str()), jend = directory_iterator();
    for(; j != jend; ++j) {
      const path &p2 = j->path();
      assert(is_directory(p2));

      Vid vid = static_cast<Vid>(strtoul(p2.filename().native().c_str(), NULL, 16));
      assert(absolute(child_path(vid).c_str()).native() == absolute(p2).native()); // using the vid we just read, we must arrive at the same path

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


  // the existing mmap continues to point to the old file data afterwards (since the file remains open)
  WriteArray(&newArray, newArray + newSize, db_);
  // map the newly written array, and atomically replace the old array_
  this->array_.reset(new SuffixArrayDisk<Token>(array_path(), db_));
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
  this->children_[vid] = new TreeNodeDisk<Token>(child_path(vid), this->db_, this->kMaxArraySize);
}

template<class Token>
void TreeNodeDisk<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  TreeNode<Token, SuffixArray>::SplitNode(corpus, depth, std::bind(&TreeNodeDisk<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
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
void TreeNodeDisk<Token>::WriteArray(SuffixArrayPosition<Token> **first, SuffixArrayPosition<Token> *last, rocksdb::DB *db) {
  void *data = *first;
  size_t size = sizeof(SuffixArrayPosition<Token>) * (last - *first);

  std::cerr << "in WriteArray(), my DB " << " = " << db << std::endl;

  std::string key_str = array_path();
  rocksdb::Slice key = key_str; // this is a reference, and as such, it MUST refer to something in this scope (must not be Slice key = array_path())
  rocksdb::Slice val((const char *) data, size);
  rocksdb::Status status = db->Put(rocksdb::WriteOptions(), key, val);
  std::cerr << "Put key=" << key_str << " len=" << size << " ok=" << status.ok() << std::endl;

  // DEBUG:

  //rocksdb::Slice key = filename;
  std::string value;

  rocksdb::Status status2 = db->Get(rocksdb::ReadOptions(), key, &value);
  std::cerr << "immediate-Get key=" << key_str << " status.ok()=" << status2.ok() << " IsNotFound()=" << status2.IsNotFound() << std::endl;
  assert(status2.ok());
}

// explicit template instantiation
template class TreeNodeDisk<SrcToken>;
template class TreeNodeDisk<TrgToken>;

} // namespace sto
