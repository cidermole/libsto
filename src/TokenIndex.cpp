/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "TokenIndex.h"

namespace sto {

template<class Token>
IndexSpan<Token>::IndexSpan(TokenIndex<Token> &index) : index_(&index)
{}

template<class Token>
size_t IndexSpan<Token>::narrow(Token t) {
  return 0; // TODO
}

template<class Token>
Position<Token> IndexSpan<Token>::operator[](size_t rel) {
  return Position<Token>(*index_->corpus(), 0, 0); // TODO
}

template<class Token>
size_t IndexSpan<Token>::size() {
  return 0; // TODO
}

// --------------------------------------------------------

template<class Token>
TokenIndex<Token>::TokenIndex(Corpus<Token> &corpus) : corpus_(&corpus)
{}

template<class Token>
IndexSpan<Token> TokenIndex<Token>::span() {
  return IndexSpan<Token>(*this);
}

} // namespace sto
