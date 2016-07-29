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

namespace rocksdb {
class DB;
}

namespace sto {

/** Position that is byte-packed for efficient disk storage. */
template<class Token>
struct __attribute__((packed)) SuffixArrayPosition {
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Offset Offset;

  Sid sid;
  Offset offset;

  /** for performance/laziness, a non-initializing constructor */
  SuffixArrayPosition() {}

  SuffixArrayPosition(const Position<Token> &other) : sid(other.sid), offset(other.offset) {}
  operator Position<Token>() { return Position<Token>(sid, offset); }
};

/** Adapter for TreeNodeDisk::Merge(), has an identical interface to TokenIndex::Span */
template<class Token>
class SuffixArrayPositionSpan {
public:
  SuffixArrayPositionSpan(SuffixArrayPosition<Token> *first, SuffixArrayPosition<Token> *last) : first_(first), last_(last) {}
  Position<Token> operator[](size_t pos) const { return first_[pos]; }
  size_t size() const { return last_ - first_; }

private:
  SuffixArrayPosition<Token> *first_;
  SuffixArrayPosition<Token> *last_;
};

/**
 * suffix array from disk, cached in RAM, used in TreeNodeDisk leaves.
 * Supports random_access iteration compatible with std::vector for reading.
 */
template<class Token>
class SuffixArrayDisk {
public:
  typedef sto::SuffixArrayPosition<Token> value_type;

  SuffixArrayDisk();
  SuffixArrayDisk(const std::string &bytes);
  SuffixArrayDisk(const SuffixArrayPosition<Token> *data, size_t len);

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

    SuffixArrayPosition<Token> *ptr() const { return pos_; }
  private:
    SuffixArrayPosition<Token> *pos_;
  };
  typedef Iterator iterator; // for compatibility with std::vector

  Iterator begin() const { return Iterator(array_); }
  Iterator end() const { return Iterator(array_ + length_); }
  size_t size() const { return length_; }

  Position<Token> operator[](size_t pos) const { return array_[pos]; }

  SuffixArrayDisk &operator=(const std::string &bytes);

private:
  std::string value_;
  SuffixArrayPosition<Token> *array_; /** pointer to mmapped suffix array on disk */
  size_t length_; /** length of array in number of entries (SuffixArrayPositions) */
};

} // namespace sto

#endif //STO_DISKSUFFIXARRAY_H
