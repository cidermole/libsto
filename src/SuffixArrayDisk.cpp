/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "SuffixArrayDisk.h"
#include "rocksdb/db.h"

#include <iostream>

namespace sto {

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk() {
  (*this) = "";
}

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk(size_t len) {
  value_ = std::string(sizeof(SuffixArrayPosition<Token>) * len, '\0');
  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(const_cast<char *>(value_.c_str()));
  length_ = len;
}

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk(const std::string &bytes) {
  // keep data block cached in RAM
  (*this) = bytes;
}

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk(const SuffixArrayPosition<Token> *data, size_t len) {
  // keep data block cached in RAM
  (*this) = std::string(reinterpret_cast<const char *>(data), sizeof(SuffixArrayPosition<Token>) * len);
}

template<class Token>
void SuffixArrayDisk<Token>::resize(size_t len) {
  value_.resize(sizeof(SuffixArrayPosition<Token>) * len);
  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(const_cast<char *>(value_.c_str()));
  length_ = len;
}

template<class Token>
SuffixArrayDisk<Token> &SuffixArrayDisk<Token>::operator=(const std::string &bytes) {
  value_ = bytes;
  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(const_cast<char *>(value_.c_str()));
  length_ = value_.size() / sizeof(SuffixArrayPosition<Token>);
  return *this;
}

// explicit template instantiation
template class SuffixArrayDisk<SrcToken>;
template class SuffixArrayDisk<TrgToken>;
template class SuffixArrayDisk<Domain>;

} // namespace sto