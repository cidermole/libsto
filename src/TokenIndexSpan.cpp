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
#include "TreeNode.h"

namespace sto {

template<class Token>
TokenIndex<Token>::Span::Span(const TokenIndex<Token> &index) : index_(&index) {
  // starting sentinel
  tree_path_.push_back(index_->root_);

  // this sentinel should be handled in narrow(),
  // but for a leaf-only tree (rooted in a suffix array) we cannot do better:
  if (index_->root_->is_leaf())
    array_path_.push_back(Range{0, index_->root_->size()});
}

template<class Token>
size_t TokenIndex<Token>::Span::narrow(Token t) {
  size_t new_span;

  if (in_array())
    new_span = narrow_array_(t);
  else
    new_span = narrow_tree_(t);

  if (new_span == STO_NOT_FOUND)
    return 0;
  // only modify the IndexSpan if no failure

  sequence_.push_back(t);

  // if we just descended into the suffix array, add sentinel: spanning full array range
  // array_path_: entries always index relative to the specific suffix array
  if (in_array() && array_path_.size() == 0)
    array_path_.push_back(Range{0, tree_path_.back()->size()});

  return new_span;
}

template<class Token>
Range TokenIndex<Token>::Span::find_bounds_array_(Token t) {
  return tree_path_.back()->find_bounds_array_(*index_->corpus_, array_path_.back(), t, sequence_.size());
}

template<class Token>
size_t TokenIndex<Token>::Span::narrow_array_(Token t) {
  Range new_range = find_bounds_array_(t);

  if (new_range.size() == 0)
    return STO_NOT_FOUND; // do not modify the IndexSpan and signal failure

  array_path_.push_back(new_range);
  return new_range.size();
}

template<class Token>
size_t TokenIndex<Token>::Span::narrow_tree_(Token t) {
  TreeNodeT *node;
  if (!tree_path_.back()->find_child_(t.vid, &node))
    return STO_NOT_FOUND; // do not modify the IndexSpan and signal failure

  // note: we also end up here if stepping into an empty, existing SuffixArray leaf
  assert(node != nullptr);
  tree_path_.push_back(node);
  return tree_path_.back()->size();
}

template<class Token>
Position<Token> TokenIndex<Token>::Span::operator[](size_t rel) const {
  assert(rel < size());

  // traverses the tree down using binary search on the cumulative counts at each internal TreeNode
  // until we hit a SuffixArray leaf and can do random access there.
  // upper_bound()-1 of rel inside the list of our children
  return tree_path_.back()->At(array_path_.size() ? array_path_.back().begin : 0, rel);
}

template<class Token>
Position<Token> TokenIndex<Token>::Span::at_unchecked(size_t rel) const {
  return tree_path_.back()->At(array_path_.size() ? array_path_.back().begin : 0, rel);
}

template<class Token>
size_t TokenIndex<Token>::Span::size() const {
  if (in_array()) {
    assert(array_path_.size() > 0);
    return array_path_.back().size();
  } else {
    assert(tree_path_.size() > 0);
    return tree_path_.back()->size();
  }
}

template<class Token>
typename TokenIndex<Token>::Span::TreeNodeT *TokenIndex<Token>::Span::node() {
  return tree_path_.back();
}

template<class Token>
size_t TokenIndex<Token>::Span::depth() const {
  return sequence_.size();
}

template<class Token>
size_t TokenIndex<Token>::Span::tree_depth() const {
  return tree_path_.size() - 1; // exclude sentinel entry for root (for root, tree_depth() == 0)
}

template<class Token>
bool TokenIndex<Token>::Span::in_array() const {
  return tree_path_.back()->is_leaf();
}

template<class Token>
Corpus<Token> *TokenIndex<Token>::Span::corpus() const {
  return index_->corpus();
}

// explicit template instantiation
template class TokenIndex<SrcToken>::Span;
template class TokenIndex<TrgToken>::Span;

} // namespace sto
