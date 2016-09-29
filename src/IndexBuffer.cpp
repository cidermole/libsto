/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "IndexBuffer.h"
#include "TokenIndex.h"

namespace sto {

template<class Token>
BatchIndexBuffer<Token>::BatchIndexBuffer(ITokenIndex<Token> &index, size_t batchSize) :
    IndexBuffer<Token>(index),
    buffer_(new TokenIndex<Token, IndexTypeMemory>(*this->index_.corpus(), kMaxLeafSizeMem)),
    batchSize_(batchSize)
{}

template<class Token>
void BatchIndexBuffer<Token>::AddSentence(const Sentence<Token> &sent, sto_updateid_t version) {
  // add to memory index, then merge in when batch size has been reached

#if 0
  // merge every sentence
  TokenIndex<Token, IndexTypeMemory> add(*this->index_.corpus());
  add.AddSentence(sent);
  this->index_.Merge(add);
#else
  // merge in batches of batchSize_
  buffer_->AddSentence(sent, version);
  if(++nsents_ == batchSize_)
    Flush();
#endif
}

template<class Token>
void BatchIndexBuffer<Token>::Flush() {
  this->index_.Merge(*buffer_);
  nsents_ = 0;
  buffer_.reset(new TokenIndex<Token, IndexTypeMemory>(*this->index_.corpus(), kMaxLeafSizeMem));
}

// --------------------------------------------------------

// explicit template instantiation
template class BatchIndexBuffer<SrcToken>;
template class BatchIndexBuffer<TrgToken>;


} // namespace sto
