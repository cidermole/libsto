/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_VOCAB_H
#define STO_VOCAB_H

#include <string>
#include <unordered_map>

namespace sto {

template<class Token>
class Vocab {
public:
  Vocab();

  /** Returns the surface form of `token`. */
  std::string operator[](const Token token) const;

  /** Returns the Token for the given `surface` form. May insert `surface`. */
  Token operator[](const std::string &surface);

  /** Returns the surface form of `token`. */
  std::string at(const Token token) const;

  /** Returns the Token for the given `surface` form. */
  Token at(const std::string &surface) const;

private:
  typedef typename Token::Vid Vid;

  std::unordered_map<Vid, std::string> id2surface_;
  std::unordered_map<std::string, Vid> surface2id_;
  Vid size_;
};

} // namespace sto

#endif //STO_VOCAB_H
