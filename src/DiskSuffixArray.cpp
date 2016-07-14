/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "DiskSuffixArray.h"

namespace sto {

template<class Token>
DiskSuffixArray<Token>::DiskSuffixArray(const std::string &filename) : mapping_(new MappedFile(filename)) {
  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(mapping_->ptr);
  length_ = mapping_->size() / sizeof(SuffixArrayPosition<Token>);
}

} // namespace sto