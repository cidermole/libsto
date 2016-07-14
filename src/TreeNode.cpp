/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <algorithm>
#include <cassert>
#include <cstring>

#include "TreeNode.h"
//#include "TokenIndex.h"

namespace sto {

template<class Token, class SuffixArray>
TreeNode<Token, SuffixArray>::TreeNode(size_t maxArraySize) : is_leaf_(true), array_(new SuffixArray), kMaxArraySize(maxArraySize)
{}

template<class Token, class SuffixArray>
TreeNode<Token, SuffixArray>::~TreeNode() {
  // ~RBTree() should do the work. But pointers are opaque to it (ValueType), so it does not, currently.
  children_.Walk([](Vid vid, TreeNode<Token, SuffixArray> *e) {
    delete e;
  });
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::AddPosition(const Sentence<Token> &sent, Offset start, size_t depth) {
  assert(is_leaf()); // Exclusively for adding to a SA (leaf node).

  Position<Token> corpus_pos{sent.sid(), start};
  const Corpus<Token> &corpus = sent.corpus();
  std::shared_ptr<SuffixArray> array = array_;

  // find insert position in sorted suffix array
  // thread safety: single writer guarantees that the insert_pos will still be valid later below
  auto insert_pos = std::upper_bound(
      array->begin(), array->end(),
      corpus_pos,
      [&corpus](const Position<Token> &new_pos, const Position<Token> &arr_pos) {
        return arr_pos.compare(new_pos, corpus);
      }
  );

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

    array_ = resized; // atomic replace

    array = array_;
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
  bool allow_split = sent.size() + 1 > start + depth; // +1 for implicit </s>

  if(array->size() > kMaxArraySize && allow_split) {
    SplitNode(corpus, static_cast<Offset>(depth)); // suffix array grown too large, split into TreeNode
  }
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::AddLeaf(Vid vid) {
  children_[vid] = new TreeNode<Token, SuffixArray>();
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::AddSize(Vid vid, size_t add_size) {
  children_.AddSize(vid, add_size);
}

/** Split this leaf node (suffix array) into a proper TreeNode with children. */
template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  typedef typename SuffixArray::iterator iter;

  assert(is_leaf()); // this method works only on suffix arrays

  auto comp = [&corpus, depth](const Position<Token> &a, const Position<Token> &b) {
    // the suffix array at this depth should only contain positions that continue long enough without the sentence ending
    return a.add(depth, corpus).vid(corpus) < b.add(depth, corpus).vid(corpus);
  };

  assert(size() > 0);
  std::pair<iter, iter> vid_range;
  std::shared_ptr<SuffixArray> array = array_;
  Position<Token> pos = (*array)[0]; // first position with first vid

  // thread safety: we build the TreeNode while is_leaf_ == true, so children_ is not accessed while being modified

  // for each top-level word, find the suffix array range and populate individual split arrays
  while(true) {
    vid_range = std::equal_range(array->begin(), array->end(), pos, comp);

    // copy each range into its own suffix array
    TreeNode<Token, SuffixArray> *new_child = new TreeNode<Token, SuffixArray>(kMaxArraySize);
    std::shared_ptr<SuffixArray> new_array = new_child->array_;
    new_array->insert(new_array->begin(), vid_range.first, vid_range.second);
    //children_[pos.add(depth, corpus).vid(corpus)] = new_child;
    children_.FindOrInsert(pos.add(depth, corpus).vid(corpus), /* add_size = */ new_array->size()) = new_child;

    TreeNode<Token, SuffixArray> *n;
    assert(children_.Find(pos.add(depth, corpus).vid(corpus), &n));
    assert(n != nullptr);
    assert(children_.ChildSize(pos.add(depth, corpus).vid(corpus)) == new_array->size());

    if(vid_range.second != array->end())
      pos = *vid_range.second; // position with next vid
    else
      break;
  }
  assert(children_.Size() == array->size());

  // release: ensure prior writes to children_ get flushed before the atomic operation
  is_leaf_.store(false, std::memory_order_release);

  // destroy the suffix array (last reader will clean up)
  array_.reset();
  // note: array_ null check could replace is_leaf_
}

template<class Token, class SuffixArray>
size_t TreeNode<Token, SuffixArray>::size() const {
  // thread safety: obtain reference first, check later, so we are sure to have a valid array -- avoids race with SplitNode()
  std::shared_ptr<SuffixArray> array = array_;
  if(is_leaf())
    return array->size();
  else
    return children_.Size();
}

template<class Token, class SuffixArray>
Position<Token> TreeNode<Token, SuffixArray>::At(size_t sa_offset, size_t rel_offset) {
  // thread safety: obtain reference first, check later, so we are sure to have a valid array -- avoids race with SplitNode()
  std::shared_ptr<SuffixArray> array = array_;
  if(is_leaf()) {
    return (*array)[sa_offset + rel_offset];
  } else {
    TreeNode<Token, SuffixArray> *child = children_.At(&rel_offset); // note: changes rel_offset
    assert(child != nullptr);
    return child->At(sa_offset, rel_offset);
  }
}

std::string nspaces(size_t n) {
  char buf[n+1];
  memset(buf, ' ', n); buf[n] = '\0';
  return std::string(buf);
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth) {
  std::string spaces = nspaces(depth * 2);
  os << spaces << "TreeNode size=" << size() << " is_leaf=" << (is_leaf() ? "true" : "false") << std::endl;

  // for internal TreeNodes (is_leaf=false), these have children_ entries
  children_.Walk([&corpus, &os, &spaces, depth](Vid vid, TreeNode<Token, SuffixArray> *e) {
    std::string surface = corpus.vocab()[Token{vid}];
    os << spaces << "* '" << surface << "' vid=" << static_cast<int>(vid) << std::endl;
    e->DebugPrint(os, corpus, depth + 1);
  });

  // for suffix arrays (is_leaf=true)
  std::shared_ptr<SuffixArray> array = array_;
  if(array != nullptr) {
    for(Position<Token> p : *array) {
      os << spaces << "* [sid=" << static_cast<int>(p.sid) << " offset=" << static_cast<int>(p.offset) << "]" << std::endl;
    }
  }
}

// explicit template instantiation
template class TreeNode<SrcToken, std::vector<AtomicPosition<SrcToken>>>;
template class TreeNode<TrgToken, std::vector<AtomicPosition<TrgToken>>>;

template class TreeNode<SrcToken, SuffixArrayDisk<SrcToken>>;
template class TreeNode<TrgToken, SuffixArrayDisk<TrgToken>>;

} // namespace sto
