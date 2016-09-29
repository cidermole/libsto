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
TreeNode<Token, SuffixArray>::TreeNode(ITokenIndex<Token> &index, size_t maxArraySize, ITreeNode<Token> *parent, Vid vid):
    index_(index),
    is_leaf_(true),
    array_(nullptr),
    parent_(parent),
    depth_(parent ? parent->depth() + 1 : 0),
    vid_(vid),
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
void TreeNode<Token, SuffixArray>::Merge(IndexSpan<Token> &spanSource, IndexSpan<Token> &spanTarget, LeafMerger<Token, SuffixArray> &merger) {
  // iterate through children, recursively calling Merge() until we reach the TreeNodeDisk leaf level.
  // spanSource may not hit leaves at the same depth, but we can still iterate over the entire span to merge it.

  assert(spanTarget.node() == this);

  if(spanTarget.node()->is_leaf()) {

    // bug counter: 2
    //
    // game: next time you find a bug related to a SpanSize()==0 crash in VidIterator::operator++() ITokenIndex.h, increment this.
    // somehow, the bug only materializes on large data (benchmark-1.1). or maybe short-enough sentences? europarl with 700k lines is not enough.
    // examples/data/train is also not enough to reproduce.
    //
    // Reason for the bug is that we try to split the </s> node (kEosVid).
    
    if(spanSource.size() + spanTarget.size() > this->kMaxArraySize && this->vid_ != Token::kEosVid) { // and not </s> which we cannot split
      // we already know it's not going to fit, create another internal level
      //std::cerr << "TreeNodeDisk::Merge() -> SplitNode() targeting size=" << (spanSource.size() + spanTarget.size()) << " so splitting early at depth=" << spanTarget.depth() << std::endl;
      SplitNode(*spanTarget.corpus());
      //std::cerr << "SplitNode() done." << std::endl;
    } else {
      // TokenIndexSpan::begin() iterates over Positions, which is what we want here.  (note that IndexSpan::begin() would iterate over vid)
      MergeLeaf(*spanSource.get(), merger);
      return;
    }
  }

  for(auto vid : spanSource) {
    IndexSpan<Token> spans = spanSource;
    size_t num_new = spans.narrow(Token{vid});
    if(num_new == 0)
      continue; // tolerate (skip) empty leaf nodes

    IndexSpan<Token> spant = spanTarget;
    size_t spanSize = spant.narrow(Token{vid});
    if(spanSize == 0) {
      // (1) create tree entry (leaf)
      assert(spant.node() == this);
      this->AddLeaf(vid);
      spant.narrow(Token{vid}); // step IndexSpan into the node just created (which contains an empty SA)
      assert(spant.in_array());
    }
    dynamic_cast<TreeNode<Token, SuffixArray> *>(spant.node())->Merge(spans, spant, merger); // TODO: reinterpret_cast? (faster? correct?)
    this->AddSize(vid, num_new);
  }
  this->WriteChildren(); // flush internal node's children -- from AddLeaf()
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
IndexSpan<Token> TreeNode<Token, SuffixArray>::span() {
  return index_.span(*this);
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
void TreeNode<Token, SuffixArray>::DebugPrint(std::ostream &os, const std::unordered_map<Vid, std::string> &id2surface, size_t depth) {
  std::string spaces = nspaces(depth * 2);
  os << spaces << "TreeNode size=" << size() << " is_leaf=" << (is_leaf() ? "true" : "false") << std::endl;

  // for internal TreeNodes (is_leaf=false), these have children_ entries
  children_.Walk([&](Vid vid, TreeNode<Token, SuffixArray> *e) {
    std::string surface = id2surface.at(vid);
    os << spaces << "* '" << surface << "' vid=" << static_cast<int>(vid) << std::endl;
    e->DebugPrint(os, id2surface, depth + 1);
  });

  // for suffix arrays (is_leaf=true)
  std::shared_ptr<SuffixArray> array = array_;
  if(array != nullptr) {
    for(Position<Token> p : *array) {
      os << spaces << "* [sid=" << static_cast<int>(p.sid) << " offset=" << static_cast<int>(p.offset) << "]" << std::endl;
    }
  }
}

template<class Token, class SuffixArray>
void TreeNode<Token, SuffixArray>::DebugCheckVidConsistency() const {
  auto node_span = const_cast<TreeNode<Token, SuffixArray> *>(this)->span();
  size_t span_size = node_span.size();
  Position<Token> prev_pos;
  Corpus<Token> &corpus = *this->index().corpus();

  for(size_t i = 0; i < span_size; i++) {
    Position<Token> pos = node_span[i];
    Sentence<Token> sent = corpus.sentence(pos.sid);
    if(pos.offset + this->depth_ > sent.size())
      throw std::runtime_error("DebugCheckVidConsistency(): offset+depth > sent.size()");
    if(this->depth_ > 0 && pos.add(this->depth_ - 1, corpus).vid(corpus) != this->vid_)
      throw std::runtime_error("DebugCheckVidConsistency(): vid " + std::to_string(pos.add(this->depth_ - 1, corpus).vid(corpus)) + " at depth=" + std::to_string(this->depth_) + " != node vid " + std::to_string(this->vid_));
    if(i > 0) {
      if(prev_pos.add(this->depth_, corpus).vid(corpus) > pos.add(this->depth_, corpus).vid(corpus))
        throw std::runtime_error("DebugCheckVidConsistency(): sort order violation at i=" + std::to_string(i));
    }
    prev_pos = pos;
  }

  if(!this->is_leaf()) {
    for(auto vid : (*this)) {
      TreeNode<Token, SuffixArray> *n = nullptr;
      bool ok = this->children_.Find(vid, &n);
      if(!ok)
        throw std::runtime_error("DebugCheckVidConsistency(): failed to find own child vid");
      n->DebugCheckVidConsistency();
    }
  }
}

// explicit template instantiation
template class TreeNode<SrcToken, SuffixArrayMemory<SrcToken>>;
template class TreeNode<TrgToken, SuffixArrayMemory<TrgToken>>;

template class TreeNode<SrcToken, SuffixArrayDisk<SrcToken>>;
template class TreeNode<TrgToken, SuffixArrayDisk<TrgToken>>;

} // namespace sto
