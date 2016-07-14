/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DISKSUFFIXARRAY_H
#define STO_DISKSUFFIXARRAY_H

#include <iterator>
#include <cstddef>
#include <memory>

#include "Corpus.h"
#include "MappedFile.h"

namespace sto {

template<class Token>
struct __attribute__((packed)) SuffixArrayPosition {
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Offset Offset;

  Sid sid;
  Offset offset;

  operator Position<Token>() { return Position<Token>(sid, offset); }
};


template<class Token>
class SuffixArrayDisk {
public:
  // TODO: remove. just for testing integration
  //SuffixArrayDisk() : array_(nullptr), length_(0) { assert(0); }

  SuffixArrayDisk(const std::string &filename);

  class Iterator {
  public:
    typedef std::random_access_iterator_tag iterator_category;
    typedef sto::SuffixArrayPosition<Token> value_type;
    typedef ptrdiff_t                       difference_type;
    typedef const value_type*               pointer;
    typedef const value_type&               reference;

    Iterator(SuffixArrayPosition<Token> *pos = nullptr) : pos_(pos) {}

    Iterator &operator++() { ++pos_; return *this; }
    Position<Token> operator*() { return *pos_; }
    bool operator!=(const Iterator &other) { return pos_ != other.pos_; }

    // compat, testing
    Iterator operator+(size_t add) { return Iterator(pos_ + add); }
    Iterator &operator+=(size_t add) { pos_ += add; return *this; }
    size_t operator-(const Iterator &other) { return pos_ - other.pos_; } // note: should be ptrdiff_t instead!?
  private:
    SuffixArrayPosition<Token> *pos_;
  };
  typedef Iterator iterator; // for compatibility with std::vector

  // const these?
  Iterator begin() { return Iterator(array_); }
  Iterator end() { return Iterator(array_ + length_); }
  size_t size() const { return length_; }

  // const this?
  Position<Token> operator[](size_t pos) { return array_[pos]; }

private:
  SuffixArrayPosition<Token> *array_; /** pointer to mmapped suffix array on disk */
  size_t length_; /** length of array in number of entries (SuffixArrayPositions) */
  std::unique_ptr<MappedFile> mapping_;
};

} // namespace sto

#endif //STO_DISKSUFFIXARRAY_H
