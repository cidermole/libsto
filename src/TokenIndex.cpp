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
IndexSpan<Token>::IndexSpan(TokenIndex<Token> &index) : index_(&index)
{
  // starting sentinel
  tree_path_.push_back(index_->root_);

  // this sentinel should be handled in narrow(),
  // but for a leaf-only tree (rooted in a suffix array) we cannot do better:
  if(index_->root_->is_leaf())
    array_path_.push_back(Range{0, index_->root_->size()});

  // who invalidates IndexSpan when TokenIndex is updated?
  // ideally, we should track that, and provide errors to the user.
}

template<class Token>
size_t IndexSpan<Token>::narrow(Token t) {
  size_t new_span;

  if(in_array_())
    new_span = narrow_array_(t);
  else
    new_span = narrow_tree_(t);

  if(new_span != 0) {
    // only modify the IndexSpan if no failure
    sequence_.push_back(t);

    // if we just descended into the suffix array, add sentinel: spanning full array range
    // array_path_: entries always index relative to the specific suffix array
    if(in_array_() && array_path_.size() == 0)
      array_path_.push_back(Range{0, tree_path_.back()->size()});
  }

  return new_span;
}

template<class Token>
Range IndexSpan<Token>::find_bounds_array_(Token t) {
  // for each token position, we need to check if it's long enough to extend as far as we do
  // (note: lexicographic sort order means shorter stuff is always at the beginning - so if Pos is too short, then Pos < Tok.)
  // then, we only need to compare at the depth of new_sequence_size, since all tokens before should be equal
  size_t old_sequence_size = sequence_.size();

  auto &array = tree_path_.back()->array_;
  Range prev_bounds = array_path_.back();
  Range bounds;

  Corpus<Token> &corpus = *index_->corpus_;

  // binary search for the range containing Token t
  bounds.begin = std::lower_bound(
      array.begin() + prev_bounds.begin, array.begin() + prev_bounds.end,
      //new_sequence,
      t,
      [&corpus, old_sequence_size](const Position<Token> &pos, const Token &t) {
        Sentence<Token> sent = corpus.sentence(pos.sid);
        // lexicographic sort order means shorter sequences always come first in array
        if (sent.size() - pos.offset < old_sequence_size + 1)
          return true;
        // we only need to compare at the depth of new_sequence_size, since all tokens before are equal

        // note: Token::operator<(Token&) compares by vid (not surface form)
        return sent[pos.offset + old_sequence_size] < t;
      }
  ) - array.begin();

  bounds.end = std::upper_bound(
      array.begin() + prev_bounds.begin, array.begin() + prev_bounds.end,
      //new_sequence,
      t,
      [&corpus, old_sequence_size](const Token &t, const Position<Token> &pos) {
        Sentence<Token> sent = corpus.sentence(pos.sid);
        // lexicographic sort order means shorter sequences always come first in array
        if (sent.size() - pos.offset < old_sequence_size + 1)
          return false;
        // we only need to compare at the depth of new_sequence_size, since all tokens before are equal

        // note: Token::operator<(Token&) compares by vid (not surface form)
        return t < sent[pos.offset + old_sequence_size];
      }
  ) - array.begin();

  return bounds;
}

template<class Token>
size_t IndexSpan<Token>::narrow_array_(Token t) {
  Range new_range = find_bounds_array_(t);

  if(new_range.size() == 0)
    return 0; // do not modify the IndexSpan and signal failure

  array_path_.push_back(new_range);
  return new_range.size();
}

template<class Token>
size_t IndexSpan<Token>::narrow_tree_(Token t) {
  TreeNode<Token> *node;
  if(!tree_path_.back()->children_.Find(t.vid, &node))
    return 0; // do not modify the IndexSpan and signal failure

  tree_path_.push_back(node);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) {
  assert(rel < size());

  // may need to traverse the tree down, using bin search on the cumulative counts
  // upper_bound()-1 of rel inside the list of our children
  return tree_path_.back()->AtUnordered(rel);
}

template<class Token>
Position<Token> IndexSpan<Token>::At(size_t rel) {
  assert(rel < size());

  // may need to traverse the tree down, using bin search on the cumulative counts
  // upper_bound()-1 of rel inside the list of our children
  return tree_path_.back()->At(rel, index_->corpus()->vocab());
}

template<class Token>
size_t IndexSpan<Token>::size() const {
  if(in_array_()) {
    assert(array_path_.size() > 0);
    return array_path_.back().size();
  } else {
    assert(tree_path_.size() > 0);
    return tree_path_.back()->size();
  }
}

template<class Token>
size_t IndexSpan<Token>::depth() const {
  return sequence_.size();
}

template<class Token>
bool IndexSpan<Token>::in_array_() const {
  return tree_path_.back()->is_leaf();
}

// explicit template instantiation
template class IndexSpan<SrcToken>;
template class IndexSpan<TrgToken>;

// --------------------------------------------------------

template<class Token>
TreeChildMap<Token>::TreeChildMap() {}

/*
template<class Token>
typename TreeChildMap<Token>::Iterator TreeChildMap<Token>::find(Vid vid) {
  return children_.find(vid);
}
*/

template<class Token>
Position<Token> TreeChildMap<Token>::AtUnordered(size_t offset) {
  TreeNode<Token> *child = children_.At(&offset); // note: changes offset
  return child->AtUnordered(offset);
}

template<class Token>
Position<Token> TreeChildMap<Token>::At(size_t offset, const Vocab<Token> &vocab) {
  TreeNode<Token> *child = children_.At(&offset); // note: changes offset
  return child->AtUnordered(offset);
}

// explicit template instantiation
template class TreeChildMap<SrcToken>;
template class TreeChildMap<TrgToken>;

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNode<Token>(maxLeafSize))
{}

template<class Token>
TokenIndex<Token>::~TokenIndex() {
  delete root_;
}

template<class Token>
IndexSpan<Token> TokenIndex<Token>::span() {
  return IndexSpan<Token>(*this);
}

template<class Token>
void TokenIndex<Token>::AddSentence(const Sentence<Token> &sent) {
  // start a subsequence at each sentence position
  // each subsequence only goes as deep as necessary to hit a SA
  for(Offset i = 0; i < sent.size(); i++)
    AddSubsequence_(sent, i);
}

template<class Token>
void TokenIndex<Token>::DebugPrint(std::ostream &os) {
  root_->DebugPrint(os, *corpus_);
}

template<class Token>
void TokenIndex<Token>::AddSubsequence_(const Sentence<Token> &sent, Offset start) {
  // track the position to insert at
  IndexSpan<Token> cur_span = span();
  size_t span_size;
  bool finished = false;

  for(Offset i = start; !finished && i < sent.size(); i++) {
    // add to cumulative count for internal TreeNodes (excludes SA leaves which increment in AddPosition_()), including the root (if it's not a SA)
    if(!cur_span.tree_path_.back()->is_leaf()) {
      cur_span.tree_path_.back()->size_++; // we add this here, and not in the partial sum update loop below, because we may have split a SA, which increments on its own already (a bit ugly)
      if(cur_span.tree_path_.back()->children_.Find(sent[i].vid)) // fails if we need to create a new tree entry, see (1) below
        cur_span.tree_path_.back()->children_.AddSize(sent[i].vid, /* add_size = */ 1);
      // note: avoids the TreeNode potentially created by splitting a SA. However, SA adds its own count, so the TreeNode ends up being the correct size.
    }

    span_size = cur_span.narrow(sent[i]);

    if(span_size == 0 || cur_span.in_array_()) {
      // create an entry (whether in tree or SA)
      if(!cur_span.in_array_()) {
        // (1) create tree entry (leaf)
        cur_span.tree_path_.back()->children_[sent[i].vid] = new TreeNode<Token>(); // to do: should be implemented as a method on TreeNode
        cur_span.tree_path_.back()->children_.AddSize(sent[i].vid, /* add_size = */ 1);
        cur_span.narrow(sent[i]); // step IndexSpan into the node just created (which contains an empty SA)
        assert(cur_span.in_array_());
      }
      // stop after adding to a SA (entry there represents all the remaining depth)
      finished = true;
      // create SA entry
      cur_span.tree_path_.back()->AddPosition_(sent, start, cur_span.tree_path_.size() - 1 /* why not equivalent? */ /* BAD: cur_span.depth() - (span_size ? 1 : 0)*/); // depth()-1: e.g. for the root level SA split, we've got depth()==1 (except if span_size==0) but need to look at first SA entry position (at offset 0)
      // note: cur_span is not entirely in a valid state after this, because a leaf node has been split, but array_path_ is lacking sentinel: spanning full array range
    }
  }

  /*
  // update partial sums of cumulative counts
  Offset i = start;
  auto tp = cur_span.tree_path_.begin();
  for(; i < sent.size() && tp != cur_span.tree_path_.end(); ++i, ++tp) {
    if(!(*tp)->is_leaf()) {
      (*tp)->children_.AddSize(sent[i].vid, 1);
    }
  }
   */
}

// explicit template instantiation
template class TokenIndex<SrcToken>;
template class TokenIndex<TrgToken>;

// --------------------------------------------------------

template<class Token>
TreeNode<Token>::TreeNode(size_t maxArraySize) : size_(0), partial_size_sum_(0), kMaxArraySize(maxArraySize)
{}

template<class Token>
TreeNode<Token>::~TreeNode() {
  /*
  for(auto entry : children_)
    delete entry->second;
    */
  // ~RBTree() should do the work. But pointers are opaque to it (ValueType), so it does not, currently.
  children_.Walk([](Vid vid, TreeNode<Token> *e) {
    delete e;
  });
}

template<class Token>
void TreeNode<Token>::AddPosition_(const Sentence<Token> &sent, Offset start, size_t depth) {
  assert(is_leaf()); // Exclusively for adding to a SA (leaf node).

  Position<Token> corpus_pos{sent.sid(), start};
  const Corpus<Token> &corpus = sent.corpus();

  // find insert position in sorted suffix array
  auto insert_pos = std::upper_bound(
      array_.begin(), array_.end(),
      corpus_pos,
      [&corpus](const Position<Token> &new_pos, const Position<Token> &arr_pos) {
        return arr_pos.compare(new_pos, corpus);
      }
  );

  //std::cerr << "TreeNode::AddPosition_(sent, start=" << ((int) start) << ") token=" << corpus.vocab()[sent[start]] << " (vid=" << sent[start].vid << ") insert_pos=" << (insert_pos - array_.begin()) << std::endl;

  array_.insert(insert_pos, corpus_pos);
  size_++;
  assert(size_ == array_.size());

  if(array_.size() > kMaxArraySize)
    SplitNode(corpus, static_cast<Offset>(depth)); // suffix array grown too large, split into TreeNode
}

/** Split this leaf node (suffix array) into a proper TreeNode with children. */
template<class Token>
void TreeNode<Token>::SplitNode(const Corpus<Token> &corpus, Offset depth) {
  typedef typename std::vector<Position<Token>>::iterator iter;

  assert(is_leaf()); // this method works only on suffix arrays

  auto comp = [&corpus, depth](const Position<Token> &a, const Position<Token> &b) {
    // the suffix array at this depth should only contain positions that continue long enough without the sentence ending
    return a.add(depth, corpus).vid(corpus) < b.add(depth, corpus).vid(corpus);
  };

  assert(size() > 0);
  std::pair<iter, iter> vid_range;
  Position<Token> pos = array_[0]; // first position with first vid

  // for each top-level word, find the suffix array range and populate individual split arrays
  while(true) {
    vid_range = std::equal_range(array_.begin(), array_.end(), pos, comp);

    // copy each range into its own suffix array
    TreeNode<Token> *new_child = new TreeNode<Token>(kMaxArraySize);
    new_child->array_.insert(new_child->array_.begin(), vid_range.first, vid_range.second);
    new_child->size_ = new_child->array_.size();
    //children_[pos.add(depth, corpus).vid(corpus)] = new_child;
    children_.FindOrInsert(pos.add(depth, corpus).vid(corpus), /* add_size = */ new_child->size_) = new_child;

    TreeNode<Token> *n;
    assert(children_.Find(pos.add(depth, corpus).vid(corpus), &n));
    assert(n->size_ == new_child->size_); // NOT: n->children_.size(). that's one level deeper and is empty!
    assert(children_.ChildSize(pos.add(depth, corpus).vid(corpus)) == new_child->size_);

    if(vid_range.second != array_.end())
      pos = *vid_range.second; // position with next vid
    else
      break;
  }
  assert(children_.size() == array_.size());
  array_.clear(); // destroy the suffix array
}

template<class Token>
Position<Token> TreeNode<Token>::AtUnordered(size_t offset) {
  if(is_leaf())
    return array_[offset];
  else
    return children_.AtUnordered(offset);
}

template<class Token>
Position<Token> TreeNode<Token>::At(size_t offset, const Vocab<Token> &vocab) {
  if(is_leaf())
    return array_[offset];
  else
    return children_.At(offset, vocab);
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
  for(auto p : array_) {
    os << spaces << "* [sid=" << static_cast<int>(p.sid) << " offset=" << static_cast<int>(p.offset) << "]" << std::endl;
  }
}

} // namespace sto
