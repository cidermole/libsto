/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstring>

#include "TokenIndex.h"
#include "Types.h"

namespace sto {

template<class Token>
IndexSpan<Token>::IndexSpan(const TokenIndex<Token> &index) : index_(&index)
{
  // starting sentinel
  tree_path_.push_back(index_->root_);

  // this sentinel should be handled in narrow(),
  // but for a leaf-only tree (rooted in a suffix array) we cannot do better:
  if(index_->root_->is_leaf())
    array_path_.push_back(Range{0, index_->root_->size()});
}

template<class Token>
size_t IndexSpan<Token>::narrow(Token t) {
  size_t new_span;

  if(in_array())
    new_span = narrow_array_(t);
  else
    new_span = narrow_tree_(t);

  if(new_span == STO_NOT_FOUND)
    return 0;
  // only modify the IndexSpan if no failure

  sequence_.push_back(t);

  // if we just descended into the suffix array, add sentinel: spanning full array range
  // array_path_: entries always index relative to the specific suffix array
  if(in_array() && array_path_.size() == 0)
    array_path_.push_back(Range{0, tree_path_.back()->size()});

  return new_span;
}

template<class Token>
Range IndexSpan<Token>::find_bounds_array_(Token t) {
  // for each token position, we need to check if it's long enough to extend as far as we do
  // (note: lexicographic sort order means shorter stuff is always at the beginning - so if Pos is too short, then Pos < Tok.)
  // then, we only need to compare at the depth of new_sequence_size, since all tokens before should be equal
  size_t old_sequence_size = sequence_.size();

  auto array = tree_path_.back()->array_;
  Range prev_bounds = array_path_.back();
  Range bounds;

  Corpus<Token> &corpus = *index_->corpus_;

  // binary search for the range containing Token t
  bounds.begin = std::lower_bound(
      array->begin() + prev_bounds.begin, array->begin() + prev_bounds.end,
      //new_sequence,
      t,
      [&corpus, old_sequence_size](const Position<Token> &pos, const Token &t) {
        Sentence<Token> sent = corpus.sentence(pos.sid);
        // lexicographic sort order means shorter sequences always come first in array
        // sent.size() + 1: add implicit </s>
        if (sent.size() + 1 - pos.offset < old_sequence_size + 1)
          return true;
        // we only need to compare at the depth of new_sequence_size, since all tokens before are equal

        // note: Token::operator<(Token&) compares by vid (not surface form)
        return sent[pos.offset + old_sequence_size] < t;
      }
  ) - array->begin();

  bounds.end = std::upper_bound(
      array->begin() + prev_bounds.begin, array->begin() + prev_bounds.end,
      //new_sequence,
      t,
      [&corpus, old_sequence_size](const Token &t, const Position<Token> &pos) {
        Sentence<Token> sent = corpus.sentence(pos.sid);
        // lexicographic sort order means shorter sequences always come first in array
        // sent.size() + 1: add implicit </s>
        if (sent.size() + 1 - pos.offset < old_sequence_size + 1)
          return false;
        // we only need to compare at the depth of new_sequence_size, since all tokens before are equal

        // note: Token::operator<(Token&) compares by vid (not surface form)
        return t < sent[pos.offset + old_sequence_size];
      }
  ) - array->begin();

  return bounds;
}

template<class Token>
size_t IndexSpan<Token>::narrow_array_(Token t) {
  Range new_range = find_bounds_array_(t);

  if(new_range.size() == 0)
    return STO_NOT_FOUND; // do not modify the IndexSpan and signal failure

  array_path_.push_back(new_range);
  return new_range.size();
}

template<class Token>
size_t IndexSpan<Token>::narrow_tree_(Token t) {
  TreeNode<Token> *node;
  if(!tree_path_.back()->children_.Find(t.vid, &node))
    return STO_NOT_FOUND; // do not modify the IndexSpan and signal failure

  // note: we also end up here if stepping into an empty, existing SuffixArray leaf
  assert(node != nullptr);
  tree_path_.push_back(node);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) const {
  assert(rel < size());

  // traverses the tree down using binary search on the cumulative counts at each internal TreeNode
  // until we hit a SuffixArray leaf and can do random access there.
  // upper_bound()-1 of rel inside the list of our children
  return tree_path_.back()->At(array_path_.back().begin, rel);
}

template<class Token>
size_t IndexSpan<Token>::size() const {
  if(in_array()) {
    assert(array_path_.size() > 0);
    return array_path_.back().size();
  } else {
    assert(tree_path_.size() > 0);
    return tree_path_.back()->size();
  }
}

template<class Token>
TreeNode<Token> *IndexSpan<Token>::node() {
  return tree_path_.back();
}

template<class Token>
size_t IndexSpan<Token>::depth() const {
  return sequence_.size();
}

template<class Token>
size_t IndexSpan<Token>::tree_depth() const {
  return tree_path_.size() - 1; // exclude sentinel entry for root (for root, tree_depth() == 0)
}

template<class Token>
bool IndexSpan<Token>::in_array() const {
  return tree_path_.back()->is_leaf();
}

// explicit template instantiation
template class IndexSpan<SrcToken>;
template class IndexSpan<TrgToken>;

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNode<Token>(maxLeafSize))
{}

template<class Token>
TokenIndex<Token>::~TokenIndex() {
  delete root_;
}

template<class Token>
IndexSpan<Token> TokenIndex<Token>::span() const {
  return IndexSpan<Token>(*this);
}

template<class Token>
void TokenIndex<Token>::AddSentence(const Sentence<Token> &sent) {
  // start a subsequence at each sentence position
  // each subsequence only goes as deep as necessary to hit a SA
  for(Offset i = 0; i <= sent.size(); i++)
    AddSubsequence_(sent, i);
}

template<class Token>
void TokenIndex<Token>::DebugPrint(std::ostream &os) {
  root_->DebugPrint(os, *corpus_);
}

template<class Token>
void TokenIndex<Token>::AddSubsequence_(const Sentence<Token> &sent, Offset start) {
  /*
   * A hybrid suffix trie / suffix array implementation.
   *
   * The suffix array is split into several parts, each with up to kMaxArraySize entries.
   *
   * The suffix array parts are arranged as leaves in a tree that starts out as a suffix trie at the root.
   * Branches that are small enough end in a suffix array leaf. Therefore, each suffix array leaf holds
   * the entire used vocabulary ID range at a specific depth (= distance from root).
   *
   * depth 1:
   *
   * root
   * |
   * |   internal TreeNode
   * v   v
   * *--[the]--{  < suffix array leaf
   * the cat ...
   * the dog ...
   * ...
   * the zebra ...
   * }
   *
   * depth 2:
   *
   * *--[a]--[small]--{
   *   a small cat ...
   *   a small dog ...
   *   ...
   *   a small zebra ...
   * }
   */


  // track the position to insert at
  IndexSpan<Token> cur_span = span();
  size_t span_size;
  Offset i;
  bool finished = false;

  for(i = start; !finished && i <= sent.size(); i++) {
    span_size = cur_span.narrow(sent[i]);

    if(span_size == 0 || cur_span.in_array()) {
      // create an entry (whether in tree or SA)
      if(!cur_span.in_array()) {
        // (1) create tree entry (leaf)
        cur_span.node()->AddLeaf(sent[i].vid);
        cur_span.narrow(sent[i]); // step IndexSpan into the node just created (which contains an empty SA)
        assert(cur_span.in_array());
      }
      // stop after adding to a SA (entry there represents all the remaining depth)
      finished = true;
      // create SA entry
      cur_span.node()->AddPosition(sent, start, cur_span.tree_depth());
      // After a split, cur_span is at the new internal TreeNode, not at the SA.
      // This is by design: since the SA insertion added a count there, the split created a TreeNode with already incremented size.

      // note: it might make sense to move the split here.
    }
  }
  assert(finished);
  //assert(cur_span.in_array()); // after a split, cur_span is at the new internal TreeNode, not at the SA.

  // add to cumulative count for internal TreeNodes (excludes SA leaves which increment in AddPosition()), including the root (if it's not a SA)
  auto &path = cur_span.tree_path();
  i = start + cur_span.depth();
  auto it = path.rbegin();
  ++it; i--; // avoid leaf, potentially avoid most recent internal TreeNode created by a split above.
  for(; it != path.rend(); ++it) {
    // add sizes from deepest level towards root, so that readers will see a valid state (children being at least as big as they should be)
    assert(!(*it)->is_leaf());
    (*it)->AddSize(sent[i].vid, /* add_size = */ 1);
    i--;
  }
}

// explicit template instantiation
template class TokenIndex<SrcToken>;
template class TokenIndex<TrgToken>;

// --------------------------------------------------------

template<class Token>
TreeNode<Token>::TreeNode(size_t maxArraySize) : is_leaf_(true), array_(new SuffixArray), kMaxArraySize(maxArraySize)
{}

template<class Token>
TreeNode<Token>::~TreeNode() {
  // ~RBTree() should do the work. But pointers are opaque to it (ValueType), so it does not, currently.
  children_.Walk([](Vid vid, TreeNode<Token> *e) {
    delete e;
  });
}

template<class Token>
void TreeNode<Token>::AddPosition(const Sentence<Token> &sent, Offset start, size_t depth) {
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

template<class Token>
void TreeNode<Token>::AddLeaf(Vid vid) {
  children_[vid] = new TreeNode<Token>();
}

template<class Token>
void TreeNode<Token>::AddSize(Vid vid, size_t add_size) {
  children_.AddSize(vid, add_size);
}

/** Split this leaf node (suffix array) into a proper TreeNode with children. */
template<class Token>
void TreeNode<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
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
    TreeNode<Token> *new_child = new TreeNode<Token>(kMaxArraySize);
    std::shared_ptr<SuffixArray> new_array = new_child->array_;
    new_array->insert(new_array->begin(), vid_range.first, vid_range.second);
    //children_[pos.add(depth, corpus).vid(corpus)] = new_child;
    children_.FindOrInsert(pos.add(depth, corpus).vid(corpus), /* add_size = */ new_array->size()) = new_child;

    TreeNode<Token> *n;
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

template<class Token>
size_t TreeNode<Token>::size() const {
  // thread safety: obtain reference first, check later, so we are sure to have a valid array -- avoids race with SplitNode()
  std::shared_ptr<SuffixArray> array = array_;
  if(is_leaf())
    return array->size();
  else
    return children_.Size();
}

template<class Token>
Position<Token> TreeNode<Token>::At(size_t sa_offset, size_t rel_offset) {
  // thread safety: obtain reference first, check later, so we are sure to have a valid array -- avoids race with SplitNode()
  std::shared_ptr<SuffixArray> array = array_;
  if(is_leaf()) {
    return (*array)[sa_offset + rel_offset];
  } else {
    TreeNode<Token> *child = children_.At(&rel_offset); // note: changes rel_offset
    assert(child != nullptr);
    return child->At(sa_offset, rel_offset);
  }
}

std::string nspaces(size_t n) {
  char buf[n+1];
  memset(buf, ' ', n); buf[n] = '\0';
  return std::string(buf);
}

template<class Token>
void TreeNode<Token>::DebugPrint(std::ostream &os, const Corpus<Token> &corpus, size_t depth) {
  std::string spaces = nspaces(depth * 2);
  os << spaces << "TreeNode size=" << size() << " is_leaf=" << (is_leaf() ? "true" : "false") << std::endl;

  // for internal TreeNodes (is_leaf=false), these have children_ entries
  children_.Walk([&corpus, &os, &spaces, depth](Vid vid, TreeNode<Token> *e) {
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

} // namespace sto
