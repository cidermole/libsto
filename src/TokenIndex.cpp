/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstring>

#include "Corpus.h"
#include "TokenIndex.h"
#include "TreeNode.h"
#include "Types.h"
#include "MappedFile.h"

namespace sto {

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(const std::string &filename, Corpus<Token> &corpus, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNodeT(maxLeafSize))
{
  typedef tpt::id_type Sid_t;
  typedef tpt::offset_type Offset_t;
  typedef tpt::TsaHeader TokenIndexHeader;

  MappedFile file(filename);
  TokenIndexHeader &header = *reinterpret_cast<TokenIndexHeader *>(file.ptr);

  if(header.versionMagic != tpt::INDEX_V2_MAGIC) {
    throw std::runtime_error(std::string("unknown version magic in ") + filename);
  }

  size_t num_positions = (header.idxStart - sizeof(TokenIndexHeader)) / (sizeof(Sid_t) + sizeof(Offset_t));
  // we could also sanity-check index size against the vocabulary size.

  //std::cerr << "sizeof(TokenIndexHeader) = " << sizeof(TokenIndexHeader) << std::endl;
  //std::cerr << "num_positions = " << num_positions << std::endl;

  std::shared_ptr<SuffixArray> array = std::make_shared<SuffixArray>();
  tpt::TsaPosition *p = reinterpret_cast<tpt::TsaPosition *>(file.ptr + sizeof(TokenIndexHeader));
  for(size_t i = 0; i < num_positions; i++, p++) {
    array->push_back(Position<Token>{p->sid, p->offset});
  }
  root_->array_ = array;
}

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus, size_t maxLeafSize) : corpus_(&corpus), root_(new TreeNodeT(maxLeafSize))
{}

template<class Token>
TokenIndex<Token>::~TokenIndex() {
  delete root_;
}

template<class Token>
typename TokenIndex<Token>::Span TokenIndex<Token>::span() const {
  return Span(*this);
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
template class TokenIndex<SrcToken>;
template class TokenIndex<TrgToken>;

} // namespace sto
