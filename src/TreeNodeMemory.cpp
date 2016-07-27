/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TreeNodeMemory.h"
#include "Corpus.h"
#include "MappedFile.h"
#include "TokenIndex.h"

#include <algorithm>
#include <functional>

namespace sto {


template<class Token>
TreeNodeMemory<Token>::TreeNodeMemory(std::string filename, std::shared_ptr<void>, size_t maxArraySize) : TreeNode<Token, SuffixArrayMemory<Token>>(maxArraySize) {
  this->array_.reset(new SuffixArray);
  if(filename != "")
    LoadArray(filename);
}

template<class Token>
void TreeNodeMemory<Token>::Merge(const typename TokenIndex<Token, IndexTypeMemory>::Span &spanMemory, typename TokenIndex<Token, IndexTypeMemory>::Span &spanUs) {
  assert(0);
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
  this->children_[vid] = new TreeNodeMemory<Token>("", nullptr, this->kMaxArraySize);
}

template<class Token>
bool TreeNodeMemory<Token>::find_child_(Vid vid, TreeNodeMemory<Token> **child) {
  return TreeNode<Token, SuffixArray>::find_child_(vid, reinterpret_cast<TreeNode<Token, SuffixArray> **>(child));
}

template<class Token>
TreeNodeMemory<Token> *TreeNodeMemory<Token>::make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus, Offset depth) {
  TreeNodeMemory<Token> *new_child = new TreeNodeMemory<Token>("", nullptr, this->kMaxArraySize);
  std::shared_ptr<SuffixArray> new_array = new_child->array_;
  new_array->insert(new_array->begin(), first, last);
  return new_child;
}

template<class Token>
void TreeNodeMemory<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  TreeNode<Token, SuffixArrayMemory<Token>>::SplitNode(corpus, depth, std::bind(&TreeNodeMemory<Token>::make_child_, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
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
