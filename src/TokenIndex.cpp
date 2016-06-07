/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <algorithm>
#include <cassert>

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

  // TODO: who invalidates IndexSpan when TokenIndex is updated?
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
  // TODO
  // for each token position, we need to check if it's long enough to extend as far as we do
  // (note: lexicographic sort order means shorter stuff is always at the beginning - so if Pos is too short, then Pos < Tok.)
  // then, we only need to compare at the depth of new_sequence_size, since all tokens before should be equal
  //size_t new_sequence_size = sequence_.size() + 1;
  //std::vector<Token> new_sequence = sequence_;
  //new_sequence.push_back(t);
  size_t old_sequence_size = sequence_.size();

  // Compare(Position, vector<Token>)

  auto &array = tree_path_.back()->array_;
  Range bounds;

  Corpus<Token> &corpus = *index_->corpus_;

  //auto compare = [&corpus, old_sequence_size](const Position<Token> &pos, const std::vector<Token> &seq) {
  auto compare = [&corpus, old_sequence_size](const Position<Token> &pos, const Token &t) {
    Sentence<Token> sent = corpus.sentence(pos.sid);
    // lexicographic sort order means shorter sequences always come first
    if (sent.size() - pos.offset < old_sequence_size + 1)
      return true;
    // we only need to compare at the depth of new_sequence_size, since all tokens before should be equal

    // note: Token::operator<(Token&) compares by vid (not surface form)
    //return pos.sentence()[old_sequence_size] < seq.back();
    return sent[pos.offset + old_sequence_size] < t;
    // Sentence access should maybe be more efficient?
  };

  // binary search for the range containing Token t
  bounds.begin = std::lower_bound(
      array.begin(), array.end(),
      //new_sequence,
      t,
      compare
  ) - array.begin();

  bounds.end = std::upper_bound(
      array.begin(), array.end(),
      //new_sequence,
      t,
      [&compare](const Token &t, const Position<Token> &pos) { return compare(pos, t); } // whoever designed C++11, please tell me why arguments flip vs. lower_bound() -- in fact, why compare is not just a [](const Position<Token> &)
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
  auto& children = tree_path_.back()->children_;
  TreeNodeChildMapIter child_iter = children.find(t.vid);

  if(child_iter == children.end())
    return 0; // do not modify the IndexSpan and signal failure

  this->tree_node_visit_(*tree_path_.back(), child_iter);

  tree_path_.push_back(child_iter->second);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) {
  assert(rel < size());

  if(in_array_()) {
    return tree_path_.back()->array_[rel];
  } else {
    // may need to traverse the tree down, using bin search on the cumulative counts
    // upper_bound()-1 of rel inside the list of our children

  }

  return Position<Token>{0, 0}; // TODO
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
bool IndexSpan<Token>::in_array_() const {
  return tree_path_.back()->is_leaf();
}

// explicit template instantiation
template class IndexSpan<SrcToken>;
template class IndexSpan<TrgToken>;

// --------------------------------------------------------

template<class Token>
void PartialSumUpdater<Token>::tree_node_visit_(TreeNode<Token> &node, TreeNodeChildMapIter child) {
  // update partial sums to our right (assuming an insertion happened which changed sizes)
  size_t partial_sum = child->second->partial_size_sum_;
  for(; child != node.children_.end(); child++) {
    child->second->partial_size_sum_ = partial_sum;
    partial_sum += child->second->size();
  }
}

// explicit template instantiation
template class PartialSumUpdater<SrcToken>;
template class PartialSumUpdater<TrgToken>;

// --------------------------------------------------------

template<class Token>
TreeChildMap<Token>::TreeChildMap() {}

template<class Token>
TreeNode<Token> *TreeChildMap<Token>::operator[](Vid vid) {
  typename ChildMap::Entry *entry = children_.Lookup(vid, /* insert = */ true);
  return entry->second;
}

template<class Token>
typename TreeChildMap<Token>::Iterator TreeChildMap<Token>::find(Vid vid) {
  typename ChildMap::Entry *entry = children_.Lookup(vid, /* insert = */ false);
  if(entry != nullptr)
    //return Iterator(entry); // TODO
    return end();
  else
    return end();
}

// explicit template instantiation
template class TreeChildMap<SrcToken>;
template class TreeChildMap<TrgToken>;

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus) : corpus_(&corpus), root_(new TreeNode<Token>())
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
void TokenIndex<Token>::AddSubsequence_(const Sentence<Token> &sent, Offset start) {
  // track the position to insert at
  IndexSpan<Token> cur_span = span();
  size_t span_size;
  bool finished = false;

  for(Offset i = start; !finished && i < sent.size(); i++) {
    span_size = cur_span.narrow(sent[i]);

    if(span_size == 0 || cur_span.in_array_()) {
      // create an entry (whether in tree or SA)
      if(!cur_span.in_array_()) {
        // create tree entry (leaf)
        cur_span.tree_path_.back()->children_[sent[i].vid] = new TreeNode<Token>(); // to do: should be implemented as a method on TreeNode
        cur_span.narrow(sent[i]); // step IndexSpan into the node just created (which contains an empty SA)
        assert(cur_span.in_array_());
      }
      // stop after adding to a SA (entry there represents all the remaining depth)
      finished = true;
      // create SA entry
      cur_span.tree_path_.back()->AddPosition_(sent, start);
    }
  }

  // add to cumulative counts all the way up to the tree root
  for(auto node : cur_span.tree_path_)
    node->size_++;
  // note: if subsequence ends at an internal tree node -> tree node's count is larger than the sum of its children
  // TODO: internal tree node implicit symbol </s> must be at the very beginning of all vocab symbols (sort order matters)!!!

  // update partial sums of cumulative counts
  PartialSumUpdater<Token> updater(*this);
  for(Offset i = start; i < sent.size(); i++)
    if(updater.narrow(sent[i]) == 0)
      break;
}

// explicit template instantiation
template class TokenIndex<SrcToken>;
template class TokenIndex<TrgToken>;

// --------------------------------------------------------

template<class Token>
TreeNode<Token>::TreeNode() : size_(0), partial_size_sum_(0)
{}

template<class Token>
TreeNode<Token>::~TreeNode() {
  for(auto entry : children_)
    delete entry.second;
}

template<class Token>
void TreeNode<Token>::AddPosition_(const Sentence<Token> &sent, Offset start) {
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

  array_.insert(insert_pos, corpus_pos);
  //size_++; // done outside! see TokenIndex<Token>::AddSubsequence_()
}


} // namespace sto
