/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TokenIndex.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace sto {

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::TokenIndex(const std::string &filename, Corpus<Token> &corpus, std::shared_ptr<DB<Token>> db, size_t maxLeafSize) :
    corpus_(&corpus),
    root_(new TreeNodeT(*this, maxLeafSize, filename, db, nullptr, Token::kInvalidVid)),
    seqNum_(root_->seqNum())
{}

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize) :
    corpus_(&corpus),
    root_(new TreeNodeT(*this, maxLeafSize, "", nullptr, nullptr, Token::kInvalidVid))
{}

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::~TokenIndex() {
  delete root_;
}

template<class Token, typename TypeTag>
IndexSpan<Token> TokenIndex<Token, TypeTag>::span() const {
  return IndexSpan<Token>(std::static_pointer_cast<ITokenIndexSpan<Token>>(std::shared_ptr<Span>(new Span(*this))));
}

template<class Token, typename TypeTag>
IndexSpan<Token> TokenIndex<Token, TypeTag>::span(ITreeNode<Token> &node) const {
  return IndexSpan<Token>(std::static_pointer_cast<ITokenIndexSpan<Token>>(std::shared_ptr<Span>(new Span(node))));
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::AddSentence(const Sentence<Token> &sent, seq_t seqNum) {
  typedef typename Corpus<Token>::Offset Offset;

  // workaround for testing
  if(seqNum == static_cast<seq_t>(-1))
    seqNum = sent.sid() + 1;

  // no update necessary
  if(seqNum <= this->seqNum())
    return;

  if(TypeTag::HasAddSentence) {
    // start a subsequence at each sentence position
    // each subsequence only goes as deep as necessary to hit a SA
    for(Offset i = 0; i < sent.size(); i++)
      AddSubsequence_(sent, i);

    Ack(seqNum);
  } else {
    // Workaround for testing. Should not be called in production, because it's slow! Use an IndexBuffer instead.

    // merge every sentence
    TokenIndex<Token, IndexTypeMemory> add(*corpus());
    add.AddSentence(sent, seqNum);
    Merge(add);
  }
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::Merge(const ITokenIndex<Token> &add) {
  // no update necessary
  if(add.seqNum() <= seqNum())
    return;

  auto us = this->span();
  auto adds = add.span();
  root_->Merge(adds, us);

  Ack(add.seqNum());
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::Write(std::shared_ptr<DB<Token>> db) const {
  TokenIndex<Token, IndexTypeDisk> target(/* filename = */ "/", *this->corpus(), db); // note: filename is now only used as DB key prefix; we handle DB prefixes elsewhere
  target.Merge(*this);
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::DebugPrint(std::ostream &os) {
  root_->DebugPrint(os, *corpus_);
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::AddSubsequence_(const Sentence<Token> &sent, Offset start) {
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
  auto cur_span = span();
  size_t span_size;
  Offset i;
  bool finished = false;

  // <= sent.size(): includes implicit </s> at the end
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
      dynamic_cast<TreeNodeT *>(cur_span.node())->AddPosition(sent, start);
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
    TreeNodeT *n = dynamic_cast<TreeNodeT *>(*it);
    // add sizes from deepest level towards root, so that readers will see a valid state (children being at least as big as they should be)
    assert(!n->is_leaf());
    n->AddSize(sent[i].vid, /* add_size = */ 1);
    i--;
  }
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::Ack(seq_t seqNum) {
  assert(seqNum > seqNum_);
  if(seqNum <= seqNum_)
    return;

  seqNum_ = seqNum;
  root_->Ack(seqNum_);
}

// --------------------------------------------------------

// explicit template instantiation
template class TokenIndex<SrcToken, IndexTypeMemory>;
template class TokenIndex<TrgToken, IndexTypeMemory>;

template class TokenIndex<SrcToken, IndexTypeDisk>;
template class TokenIndex<TrgToken, IndexTypeDisk>;

} // namespace sto
