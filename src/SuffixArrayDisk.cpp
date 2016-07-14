/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "SuffixArrayDisk.h"

namespace sto {

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk(const std::string &filename) : mapping_(new MappedFile(filename)) {
  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(mapping_->ptr);
  length_ = mapping_->size() / sizeof(SuffixArrayPosition<Token>);
}

// explicit template instantiation
template class SuffixArrayDisk<SrcToken>;
template class SuffixArrayDisk<TrgToken>;

} // namespace sto