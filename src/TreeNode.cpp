/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <algorithm>
#include <cassert>
#include <cstring>

#include "TreeNode.h"
#include "TokenIndex.h"

namespace sto {

template<class Token, class SuffixArray>
TreeNode<Token, SuffixArray>::TreeNode(ITreeNode<Token> *parent, size_t maxArraySize):
    is_leaf_(true),
    array_(nullptr),
    parent_(parent),
    depth_(parent ? parent->depth() + 1 : 0),
    kMaxArraySize(maxArraySize)
{}

template<class Token, class SuffixArray>
TreeNode<Token, SuffixArray>::~TreeNode() {
  // ~RBTree() should do the work. But pointers are opaque to it (ValueType), so it does not, currently.
  children_.Walk([](Vid vid, TreeNode<Token, SuffixArray> *e) {
    delete e;
  });
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::AddSize(Vid vid, size_t add_size) {
  children_.AddSize(vid, add_size);
}

template<class Token, class SuffixArray>
Range TreeNode<Token, SuffixArray>::find_bounds_array_(Corpus<Token> &corpus, Range prev_bounds, Token t, size_t depth) {
  // for each token position, we need to check if it's long enough to extend as far as we do
  // (note: lexicographic sort order means shorter stuff is always at the beginning - so if Pos is too short, then Pos < Tok.)
  // then, we only need to compare at the depth of new_sequence_size, since all tokens before should be equal
  size_t old_sequence_size = depth;

  auto array = array_;
  Range bounds;

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

template<class Token, class SuffixArray>
bool TreeNode<Token, SuffixArray>::find_child_(Vid vid, TreeNode<Token, SuffixArray> **child) {
  return children_.Find(vid, child);
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
template class TreeNode<SrcToken, SuffixArrayMemory<SrcToken>>;
template class TreeNode<TrgToken, SuffixArrayMemory<TrgToken>>;

template class TreeNode<SrcToken, SuffixArrayDisk<SrcToken>>;
template class TreeNode<TrgToken, SuffixArrayDisk<TrgToken>>;

} // namespace sto
