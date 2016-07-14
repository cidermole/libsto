/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "DiskTreeNode.h"

#include <sstream>
#include <iomanip>

#include <boost/filesystem.hpp>

namespace sto {

template<class Token>
DiskTreeNode<Token>::DiskTreeNode(const std::string &path) : path_(path) {
  using namespace boost::filesystem;

  /*
   * if path exists: recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   */
  if(exists(path.c_str())) {
    this->is_leaf_ = exists(array_path().c_str());
  } else {
    create_directories(path.c_str());
    ofstream(array_path().c_str()); // create empty suffix array
    this->is_leaf_ = true;
  }
}

template<class Token>
void DiskTreeNode<Token>::Merge(typename TokenIndex<Token>::IndexSpan &curSpan, typename TokenIndex<Token>::IndexSpan &addSpan) {
  size_t addSize = addSpan.size();
  size_t curSize = this->array_->size();
  size_t icur = 0, iadd = 0;

  assert(this->array_->size() == curSpan.size()); // curSpan should be a full span
  // TODO: assert: (span over the same vid) -> each Position (at depth) should have the same vid.

  std::shared_ptr<SuffixArray> newArray = std::make_shared<SuffixArray>();
  Corpus<Token> &corpus = *curSpan.corpus();

  Position<Token> cur = (curSize > 0) ? curSpan[icur++] : Position<Token>();
  Position<Token> add = addSpan[iadd++];
  while(icur < curSize && iadd < addSize) {
    // assuming that curSpan is much larger, this loop will be most active
    while(cur.compare(add, corpus) && icur < curSize) {
      newArray->push_back(cur);
      cur = curSpan[icur++];
    }
    newArray->push_back(add);
    add = addSpan[iadd++];
  }
  // fill from the side that still has remaining positions
  while(icur < curSize) {
    newArray->push_back(cur);
    cur = curSpan[icur++];
  }
  while(iadd < addSize) {
    newArray->push_back(add);
    add = addSpan[iadd++];
  }
  // TODO: check if split is necessary, and perform split.
}

template<class Token>
std::string DiskTreeNode<Token>::child_sub_path(Vid vid) {
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

// explicit template instantiation
template class DiskTreeNode<SrcToken>;
template class DiskTreeNode<TrgToken>;

} // namespace sto
