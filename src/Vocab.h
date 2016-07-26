/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_VOCAB_H
#define STO_VOCAB_H

#include <string>
#include <cassert>
#include <unordered_map>

namespace rocksdb {
class DB;
}

namespace sto {

/**
 * Vocabulary mapping between surface forms and Tokens (holding vocabulary IDs).
 * Choose between SrcToken and TrgToken from Types.h
 */
template<class Token>
class Vocab {
public:
  typedef typename Token::Vid Vid;
  static constexpr typename Token::Vid kEOS = 1; /** vocabulary ID for </s>, the end-of-sentence sentinel token */
  static constexpr char kEOSSurface[] = "</s>"; // to do: call kEOS kEosVid, this kEosSurface

  /** Create empty vocabulary */
  Vocab(rocksdb::DB *db = nullptr);

  /** Load vocabulary from mtt-build .tdx format */
  Vocab(const std::string &filename, rocksdb::DB *db = nullptr);

  /** Returns the surface form of `token`. */
  const std::string& operator[](const Token token) const;

  /** Returns the Token for the given `surface` form. May insert `surface`. */
  Token operator[](const std::string &surface);

  /** Returns the surface form of `token`. */
  std::string at(const Token token) const;

  // for debug
  std::string at_vid(const Vid vid) const;

  /** Returns the Token for the given `surface` form. */
  Token at(const std::string &surface) const;

  Token begin() const;
  Token end() const;

private:
  std::unordered_map<Vid, std::string> id2surface_;
  std::unordered_map<std::string, Vid> surface2id_;
  Vid size_;
  rocksdb::DB *db_;

  /** Load vocabulary from mtt-build .tdx format */
  void load_ugsapt_tdx(const std::string &filename);

  static std::string vid_key(Vid vid);
  static std::string surface_key(const std::string &surface);

  void db_put_pair(Vid vid, const std::string &surface);

  void db_load();
};

/** Empty vocabulary interface without implementations, to provide as a template argument. */
template<class Token>
class DummyVocab {
public:
  typedef typename Token::Vid Vid;
  static constexpr typename Token::Vid kEOS = {0, 0};

  std::string operator[](const Token token) const { assert(false); return std::string(); }
  std::string at(const Token token) const { assert(false); return std::string(); }
};

} // namespace sto

#endif //STO_VOCAB_H
