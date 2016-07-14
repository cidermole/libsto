/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TreeNodeMemory.h"
#include "Corpus.h"
#include "MappedFile.h"

#include <algorithm>

namespace sto {


template<class Token>
TreeNodeMemory<Token>::TreeNodeMemory(std::string filename, size_t maxArraySize) : TreeNode<Token, SuffixArrayMemory<Token>>(maxArraySize) {
  this->array_.reset(new SuffixArray);
  if(filename != "")
    LoadArray(filename);
}

template<class Token>
void TreeNodeMemory<Token>::AddPosition(const Sentence<Token> &sent, Offset start, size_t depth) {
  assert(this->is_leaf()); // Exclusively for adding to a SA (leaf node).

  Position<Token> corpus_pos{sent.sid(), start};
  const Corpus<Token> &corpus = sent.corpus();
  std::shared_ptr<SuffixArray> array = this->array_;

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
  bool allow_split = sent.size() + 1 > start + depth; // +1 for implicit </s>

  if(array->size() > this->kMaxArraySize && allow_split) {
    SplitNode(corpus, static_cast<Offset>(depth)); // suffix array grown too large, split into TreeNode
  }
}

template<class Token>
void TreeNodeMemory<Token>::AddLeaf(Vid vid) {
  this->children_[vid] = new TreeNodeMemory<Token>("", this->kMaxArraySize);
}

template<class Token>
bool TreeNodeMemory<Token>::find_child_(Vid vid, TreeNodeMemory<Token> **child) {
  return TreeNode<Token, SuffixArray>::find_child_(vid, reinterpret_cast<TreeNode<Token, SuffixArray> **>(child));
}

/** Split this leaf node (suffix array) into a proper TreeNode with children. */
template<class Token>
void TreeNodeMemory<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  typedef typename SuffixArray::iterator iter;

  assert(this->is_leaf()); // this method works only on suffix arrays

  auto comp = [&corpus, depth](const Position<Token> &a, const Position<Token> &b) {
    // the suffix array at this depth should only contain positions that continue long enough without the sentence ending
    return a.add(depth, corpus).vid(corpus) < b.add(depth, corpus).vid(corpus);
  };

  assert(this->size() > 0);
  std::pair<iter, iter> vid_range;
  std::shared_ptr<SuffixArray> array = this->array_;
  Position<Token> pos = (*array)[0]; // first position with first vid

  // thread safety: we build the TreeNode while is_leaf_ == true, so children_ is not accessed while being modified

  // for each top-level word, find the suffix array range and populate individual split arrays
  while(true) {
    vid_range = std::equal_range(array->begin(), array->end(), pos, comp);

    // copy each range into its own suffix array
    TreeNodeMemory<Token> *new_child = new TreeNodeMemory<Token>("", this->kMaxArraySize);
    std::shared_ptr<SuffixArray> new_array = new_child->array_;
    new_array->insert(new_array->begin(), vid_range.first, vid_range.second);
    //children_[pos.add(depth, corpus).vid(corpus)] = new_child;
    this->children_.FindOrInsert(pos.add(depth, corpus).vid(corpus), /* add_size = */ new_array->size()) = new_child;

    TreeNodeMemory<Token> *n = nullptr;
    assert(this->find_child_(pos.add(depth, corpus).vid(corpus), &n));
    assert(n != nullptr);
    assert(this->children_.ChildSize(pos.add(depth, corpus).vid(corpus)) == new_array->size());

    if(vid_range.second != array->end())
      pos = *vid_range.second; // position with next vid
    else
      break;
  }
  assert(this->children_.Size() == array->size());

  // release: ensure prior writes to children_ get flushed before the atomic operation
  this->is_leaf_.store(false, std::memory_order_release);

  // destroy the suffix array (last reader will clean up)
  this->array_.reset();
  // note: array_ null check could replace is_leaf_
}

template<class Token>
void TreeNodeMemory<Token>::LoadArray(const std::string &filename) {
  typedef tpt::id_type Sid_t;
  typedef tpt::offset_type Offset_t;
  typedef tpt::TsaHeader TokenIndexHeader;

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

} // namespace sto
