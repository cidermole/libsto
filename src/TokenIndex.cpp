/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <algorithm>

#include "TokenIndex.h"

namespace sto {

template<class Token>
IndexSpan<Token>::IndexSpan(TokenIndex<Token> &index) : index_(&index)
{
  tree_path_.push_back(index_->root_);
}

template<class Token>
size_t IndexSpan<Token>::narrow(Token t) {
  size_t new_span;

  if(tree_path_.back()->is_leaf())
    new_span = narrow_array_(t);
  else
    new_span = narrow_tree_(t);

  if(new_span != 0)
    sequence_.push_back(t); // only modify the IndexSpan if no failure

  return new_span;
}

template<class Token>
size_t IndexSpan<Token>::narrow_array_(Token t) {
  // TODO
  // for each token position, we need to check if it's long enough to extend as far as we do
  // (note: lexicographic sort order means shorter stuff is always at the beginning - so if Pos is too short, then Pos < Tok.)
  // then, we only need to compare at the depth of new_sequence_size, since all tokens before should be equal
  //size_t new_sequence_size = sequence_.size() + 1;
  //std::vector<Token> new_sequence = sequence_;
  //new_sequence.push_back(t);
  size_t old_sequence_size = sequence_.size();

  // Compare(Position, vector<Token>)

  auto& array = tree_path_.back()->array_;
  Range new_range;

  Corpus<Token>& corpus = *index_->corpus_;

  //auto compare = [&corpus, old_sequence_size](const Position<Token> &pos, const std::vector<Token> &seq) {
  auto compare = [&corpus, old_sequence_size](const Position<Token> &pos, const Token &t) {
    // lexicographic sort order means shorter stuff is always at the beginning
    if(pos.remaining_size() < old_sequence_size + 1)
      return true;
    // we only need to compare at the depth of new_sequence_size, since all tokens before should be equal

    // note: Token::operator<(Token&) compares by vid (not surface form)
    //return pos.sentence()[old_sequence_size] < seq.back();
    return pos.sentence()[pos.offset() + old_sequence_size] < t;
    // Sentence access should maybe be more efficient?
  };

  // binary search for the range containing Token t
  new_range.begin = std::lower_bound(
      array.begin(), array.end(),
      //new_sequence,
      t,
      compare
  ) - array.begin();

  new_range.end = std::upper_bound(
      array.begin(), array.end(),
      //new_sequence,
      t,
      compare
  ) - array.begin();

  if(new_range.size() == 0)
    return 0; // do not modify the IndexSpan and signal failure

  array_path_.push_back(new_range);
  return new_range.size();
}

template<class Token>
size_t IndexSpan<Token>::narrow_tree_(Token t) {
  auto& children = tree_path_.back()->children_;
  auto child_iter = children.find(t.vid);

  if(child_iter == children.end())
    return 0; // do not modify the IndexSpan and signal failure

  tree_path_.push_back(child_iter->second);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) {
  return Position<Token>(*index_->corpus(), 0, 0); // TODO
}

template<class Token>
size_t IndexSpan<Token>::size() const {
  if(tree_path_.back()->is_leaf())
    return array_path_.back().size();
  else
    return tree_path_.back()->size();
}

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus) : corpus_(&corpus), root_(nullptr) // TODO root_
{}

template<class Token>
IndexSpan<Token> TokenIndex<Token>::span() {
  return IndexSpan<Token>(*this);
}

// --------------------------------------------------------

template<class Token>
TreeNode<Token>::TreeNode() : size_(0)
{}

} // namespace sto
