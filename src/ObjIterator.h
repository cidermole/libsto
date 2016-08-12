/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_OBJITERATOR_H
#define STO_OBJITERATOR_H

#include <iterator>
#include <cstddef>

namespace sto {

/**
 * Generic iterator over an object that has ::value_type, operator[] and size()
 */
template<class Obj>
class ObjIterator {
public:
  typedef std::random_access_iterator_tag iterator_category;
  typedef typename Obj::value_type        value_type;
  typedef ptrdiff_t                       difference_type;
  typedef const value_type*               pointer;
  typedef const value_type&               reference;


  ObjIterator(const ObjIterator &other) = default;

  /** use Obj::begin() / end() instead. */
  ObjIterator(const Obj &obj, bool begin = true) : obj_(obj), index_(begin ? static_cast<size_t>(0) : obj.size()) {}

  value_type operator*() {
    return obj_[index_];
  }
  ObjIterator &operator++() {
    ++index_;
    return *this;
  }
  bool operator!=(const ObjIterator &other) {
    return index_ != other.index_;
  }

  ObjIterator operator+(size_t add) {
    ObjIterator ret(*this);
    ret.index_ += add;
    return ret;
  }

  ObjIterator &operator+=(size_t add) {
    index_ += add;
    return *this;
  }

  difference_type operator-(const ObjIterator &other) {
    assert(&obj_ == &other.obj_);
    return index_ - other.index_;
  }

private:
  const Obj &obj_;
  size_t index_;
};

} // namespace sto

#endif //STO_OBJITERATOR_H
