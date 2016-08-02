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

// --------------------------------------------------------

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::TokenIndex(const std::string &filename, Corpus<Token> &corpus, std::shared_ptr<DB<Token>> db, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNodeT(filename, db, maxLeafSize)), add_buffer_(*this)
{}

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNodeT("", nullptr, maxLeafSize)), add_buffer_(*this)
{}

template<class Token, typename TypeTag>
TokenIndex<Token, TypeTag>::~TokenIndex() {
  delete root_;
}

template<class Token, typename TypeTag>
typename TokenIndex<Token, TypeTag>::Span TokenIndex<Token, TypeTag>::span() const {
  return Span(*this);
}

template<class Token, typename TypeTag>
void AddSentenceImpl<Token,TypeTag>::operator()(const Sentence<Token> &sent) {
  typedef typename Corpus<Token>::Offset Offset;
  // start a subsequence at each sentence position
  // each subsequence only goes as deep as necessary to hit a SA
  for(Offset i = 0; i < sent.size(); i++)
    index_.AddSubsequence_(sent, i);
}

template<class Token, typename TypeTag>
AddSentenceImpl<Token,TypeTag>::AddSentenceImpl(TokenIndex<Token, TypeTag> &index) : index_(index)
{}

template<class Token>
void AddSentenceImpl<Token, IndexTypeDisk>::operator()(const Sentence<Token> &sent) {
  // add to memory index, then merge in
  // (workaround for testing IndexTypeDisk using AddSentence())

  Corpus<Token> &corpus = *index_.corpus();

#if 0
  // merge every sentence
  TokenIndex<Token, IndexTypeMemory> add(*index.corpus());
  add.AddSentence(sent);
  index.Merge(add);
#else
  // merge in batches of kBatchSize
  // TODO: flush remaining entries at the end
  memBuffer->AddSentence(sent);
  if(++nsents == kBatchSize) {
    index_.Merge(*memBuffer);
    nsents = 0;
    memBuffer.reset(new TokenIndex<Token, IndexTypeMemory>(corpus, kMaxLeafSizeMem));
  }
#endif
}

template<class Token>
AddSentenceImpl<Token, IndexTypeDisk>::AddSentenceImpl(TokenIndex<Token, IndexTypeDisk> &index) :
    memBuffer(new TokenIndex<Token, IndexTypeMemory>(*index.corpus(), kMaxLeafSizeMem)), index_(index)
{}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::AddSentence(const Sentence<Token> &sent) {
  // work around C++ lacking partial specialization of member functions
  add_buffer_(sent);
}


/*
  template<class IndexSpanMemory, class IndexSpanDisk>
  void Merge(const IndexSpanMemory &spanMemory, IndexSpanDisk &spanDisk);
  */

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::Merge(const TokenIndex<Token, IndexTypeMemory> &add) {
  auto us = this->span();
  auto adds = add.span();
  root_->Merge(adds, us);
}

template<class Token, typename TypeTag>
void TokenIndex<Token, TypeTag>::Merge(const TokenIndex<Token, IndexTypeDisk> &add) {
  throw new std::runtime_error("disk to disk merge not implemented yet");
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
  Span cur_span = span();
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
template class TokenIndex<SrcToken, IndexTypeMemory>;
template class TokenIndex<TrgToken, IndexTypeMemory>;

template class TokenIndex<SrcToken, IndexTypeDisk>;
template class TokenIndex<TrgToken, IndexTypeDisk>;

} // namespace sto
