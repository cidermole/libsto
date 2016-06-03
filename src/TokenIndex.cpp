/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <algorithm>

#include "TokenIndex.h"
#include "Types.h"

namespace sto {

template<class Token>
IndexSpan<Token>::IndexSpan(TokenIndex<Token> &index) : index_(&index)
{
  tree_path_.push_back(index_->root_);
}

template<class Token>
size_t IndexSpan<Token>::narrow(Token t) {
  size_t new_span;

  if(in_array_())
    new_span = narrow_array_(t);
  else
    new_span = narrow_tree_(t);

  if(new_span != 0)
    sequence_.push_back(t); // only modify the IndexSpan if no failure

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
  auto child_iter = children.find(t.vid);

  if(child_iter == children.end())
    return 0; // do not modify the IndexSpan and signal failure

  tree_path_.push_back(child_iter->second);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) {
  return Position<Token>{0, 0}; // TODO
}

template<class Token>
size_t IndexSpan<Token>::size() const {
  if(in_array_())
    return array_path_.back().size();
  else
    return tree_path_.back()->size();
}

template<class Token>
bool IndexSpan<Token>::in_array_() const {
  return tree_path_.back()->is_leaf();
}

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus) : corpus_(&corpus), root_(nullptr) // TODO root_
{}

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
  size_t size;

  for(Offset i = 0; i < sent.size(); i++) {
    size = cur_span.narrow(sent[i]);

    if(size == 0)
      /* need to create an entry (whether in tree or SA) */;
    else
      /* need to add a count (if in tree) or create an entry (if in SA) */;

    if(size != 0 && !cur_span.in_array_()) {
      // add to a count (in tree). to be precise:
      // add to cumulative counts all the way up to the tree root
      for(auto node : cur_span.tree_path_)
        node->size_++;
    } else {
      // create an entry (whether in tree or SA)
      if(cur_span.in_array_()) {
        // insert SA entry
      } else {
        // insert tree entry
      }
    }

    // note: inserting into SA requires position (since we need to compare with upcoming tokens)
    //root_->AddPosition(Position<Token>{sent.sid(), i}, *this);
  }
}

// --------------------------------------------------------

template<class Token>
TreeNode<Token>::TreeNode() : size_(0)
{}

template<class Token>
TreeNode<Token>::~TreeNode() {
  for(auto entry : children_)
    delete entry->second;
}

template<class Token>
void TreeNode<Token>::AddPosition(Position<Token> pos, TokenIndex<Token> &index) {
  // TODO
}

// explicit template instantiation
template class TokenIndex<SrcToken>;
template class TokenIndex<TrgToken>;

} // namespace sto
