/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_INDEXBUFFER_H
#define STO_INDEXBUFFER_H

#include "ITokenIndex.h"

namespace sto {

template<class Token, typename TypeTag> class TokenIndex;
struct IndexTypeMemory;

/**
 * Interface for buffering writes to a TokenIndex.
 *
 * Currently only used by BatchIndexBuffer.
 */
template<class Token>
struct IndexBuffer {
  /** Create a buffer in front of 'index'. */
  IndexBuffer(ITokenIndex<Token> &index) : index_(index) {}

  /**
   * Add a Sentence to the buffer.
   * The implementation can either decide to buffer, or add immediately to the underlying index.
   */
  virtual void AddSentence(const Sentence<Token> &sent, seq_t seqNum) = 0;

  /** Flush out all buffered writes to the index. */
  virtual void Flush() = 0;

protected:
  ITokenIndex<Token> &index_;
};

/**
 * Batching IndexBuffer implementation, intended for IndexTypeDisk.
 *
 * Strategy: add to IndexTypeMemory, then merge in when batch size has been reached.
 */
template<class Token>
struct BatchIndexBuffer : public IndexBuffer<Token> {
  static constexpr size_t kMaxLeafSizeMem = 10000;

  /**
   * Create BatchIndexBuffer in front of 'index'.
   *
   * @param index      underlying TokenIndex
   * @param batchSize  number of sentences to batch in memory before merging them
   */
  BatchIndexBuffer(ITokenIndex<Token> &index, size_t batchSize = 1);

  /**
   * Add a Sentence to the buffer.
   * The buffer may hold sentences indefinitely until you call Flush().
   */
  virtual void AddSentence(const Sentence<Token> &sent, seq_t seqNum) override;

  virtual void Flush() override;

private:
  std::unique_ptr<TokenIndex<Token, IndexTypeMemory>> buffer_;
  size_t nsents_ = 0; /** number of sentences in buffer_ */
  size_t batchSize_;  /** batch size in number of sents */
};

} // namespace sto

#endif //STO_INDEXBUFFER_H
