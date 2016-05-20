/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "Vocab.h"
#include "Types.h"

namespace sto {

template<class Token>
Vocab<Token>::Vocab(): size_(0)
{}

template<class Token>
std::string Vocab<Token>::operator[](const Token token) const {
  return id2surface_.at(token.vid);
}

template<class Token>
Token Vocab<Token>::operator[](const std::string &surface) {
  auto result = surface2id_.find(surface);
  if(result != surface2id_.end()) {
    // retrieve result
    return Token{result->second};
  } else {
    // insert
    Vid id = size_++;
    surface2id_[surface] = id;
    id2surface_[id] = surface;
    return Token{id};
  }
}

template<class Token>
std::string Vocab<Token>::at(const Token token) const {
  return id2surface_.at(token.vid);
}

template<class Token>
Token Vocab<Token>::at(const std::string &surface) const {
  return Token{surface2id_.at(surface)};
}

// explicit template instantiation
template class Vocab<SrcToken>;
template class Vocab<TrgToken>;

} // namespace sto
