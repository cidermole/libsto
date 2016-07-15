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

namespace sto {

template<class Token>
TreeNodeDisk<Token>::TreeNodeDisk(std::string path, size_t maxArraySize) :
    TreeNode<Token, SuffixArrayDisk<Token>>(maxArraySize), path_(path)
{
  using namespace boost::filesystem;

  /*
   * if path exists: load the leaf / recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   */
  if(exists(path.c_str())) {
    this->is_leaf_ = exists(array_path().c_str());
  } else {
    create_directories(path.c_str());
    ofstream(array_path().c_str()); // create empty suffix array
    this->is_leaf_ = true;
  }

  if(this->is_leaf_) {
    this->array_.reset(new SuffixArrayDisk<Token>(array_path()));
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

      TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(child_path(vid), this->kMaxArraySize);
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
void TreeNodeDisk<Token>::Merge(SuffixArrayPosition<Token> *first, SuffixArrayPosition<Token> *last, const Corpus<Token> &corpus, Offset depth) {
  // typename TokenIndex<Token>::Span &addSpan
  SuffixArrayPosition<Token> *addSpan = first;
  size_t addSize = last - first;
  size_t curSize = this->array_->size();
  size_t newSize = curSize + addSize;
  size_t icur = 0, iadd = 0;

  // typename TokenIndex<Token>::Span &curSpan
  SuffixArray &curSpan = *this->array_;

  SuffixArrayPosition<Token> *newArray = new SuffixArrayPosition<Token>[newSize];
  SuffixArrayPosition<Token> *pnew = newArray;
  //Corpus<Token> &corpus = *addSpan.corpus();

  assert(addSize > 0);
  Vid vid = Position<Token>(addSpan[0]).add(depth, corpus).vid(corpus);

  Position<Token> cur = (curSize > 0) ? curSpan[icur++] : Position<Token>();
  Position<Token> add = addSpan[iadd++];
  while(icur < curSize && iadd < addSize) {
    // assuming that curSpan is much larger, this loop will be most active
    while(cur.compare(add, corpus) && icur < curSize) {
      assert(cur.add(depth, corpus).vid(corpus) == vid); // since span over the same vid -> each Position (at depth) should have the same vid.
      *pnew++ = cur;
      cur = curSpan[icur++];
    }
    assert(add.add(depth, corpus).vid(corpus) == vid); // since span over the same vid -> each Position (at depth) should have the same vid.
    *pnew++ = add;
    add = addSpan[iadd++];
  }
  // fill from the side that still has remaining positions
  while(icur < curSize) {
    *pnew++ = cur;
    cur = curSpan[icur++];
  }
  while(iadd < addSize) {
    *pnew++ = add;
    add = addSpan[iadd++];
  }

  // the existing mmap continues to point to the old file data afterwards (since the file remains open)
  WriteArray(&newArray, newArray + newSize);
  // map the newly written array, and atomically replace the old array_
  this->array_.reset(new SuffixArrayDisk<Token>(array_path()));

  // disallow splits of </s> - as argued in TreeNodeMemory::AddPosition()
  // TODO: check if the depth is correctly assumed here
  bool allow_split = (vid != Token::Vocabulary::kEOS);

  /*
   * note: should it become necessary to split </s> array, a simple sharding concept
   * would involve fixed-size blocks. For that, we need to change Merge() to deal with shards
   * and SuffixArrayDisk to transparently map access to several blocks as one sequence.
   *
   * TODO:
   * A much easier workaround: for allow_split=false arrays (like ". </s>"), appending the new
   * Positions will always be legal. Therefore, don't build in RAM, and just append to the file on disk.
   */

  if(this->array_->size() > this->kMaxArraySize && allow_split) {
    SplitNode(corpus, depth);
  }
}

template<class Token>
void TreeNodeDisk<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  TreeNode<Token, SuffixArray>::SplitNode(corpus, depth, std::bind(&TreeNodeDisk<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
}

template<class Token>
std::string TreeNodeDisk<Token>::child_sub_path(Vid vid) {
  constexpr size_t kVidDigits = sizeof(Vid)*2;
  constexpr size_t kSignificantDigitsDir2 = 3;
  static_assert(kVidDigits > kSignificantDigitsDir2); // for dir1 size to work

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
  TreeNodeDisk<Token> *new_child = new TreeNodeDisk<Token>(child_path(vid), this->kMaxArraySize);
  new_child->Merge(first.ptr(), last.ptr(), corpus, depth);
  //new_array->insert(new_array->begin(), first, last); // this is the TreeNodeMemory interface. Maybe we could have implemented insert() here on SuffixArrayDisk, and use a common call?
  return new_child;
}

template<class Token>
void TreeNodeDisk<Token>::WriteArray(SuffixArrayPosition<Token> **first, SuffixArrayPosition<Token> *last) {
  // write newArray to a temp file first
  std::string array_tmp = array_path() + ".tmp";
  FILE *tmp = fopen(array_tmp.c_str(), "wb");
  if(!tmp) {
    delete[] *first;
    first = nullptr;
    throw std::runtime_error(std::string("failed to open array.tmp file for write at ") + array_tmp);
  }
  fwrite(*first, sizeof(SuffixArrayPosition<Token>), last - *first, tmp);
  fclose(tmp);
  delete[] *first;
  first = nullptr;

  // move temp file to array
  boost::filesystem::rename(array_tmp.c_str(), array_path().c_str());
}

// explicit template instantiation
template class TreeNodeDisk<SrcToken>;
template class TreeNodeDisk<TrgToken>;

} // namespace sto
