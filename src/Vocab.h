/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_VOCAB_H
#define STO_VOCAB_H

#include <memory>
#include <string>
#include <cassert>
#include <unordered_map>

#include "Types.h"

namespace sto {

template<class Token> class DB;

/**
 * Vocabulary mapping between surface forms and Tokens (holding vocabulary IDs).
 * Choose between SrcToken and TrgToken from Types.h
 */
template<class Token>
class Vocab {
public:
  typedef typename Token::Vid Vid;
  static constexpr Vid kEosVid = Token::kEosVid; /** vocabulary ID for </s>, the end-of-sentence sentinel token */
  static constexpr Vid kUnkVid = Token::kUnkVid; /** vocabulary ID for <unk>, the unknown word sentinel token */
  static constexpr char kEosSurface[] = "1"; /** end of sentence sentinel marker */
  static constexpr char kUnkSurface[] = "2"; /** end of sentence sentinel marker */

  /** Load vocabulary from db, or create in-memory empty vocabulary */
  Vocab(std::shared_ptr<DB<Token>> db = nullptr);

  /** Load vocabulary from mtt-build .tdx format */
  Vocab(const std::string &filename);

  /** Returns the surface form of `token`. */
  std::string operator[](const Token token) const;

  /** Returns the Token for the given `surface` form. May insert `surface`. */
  Token operator[](const std::string &surface);

  /** Returns the surface form of `token`. */
  std::string at(const Token token) const;

  // for debug
  std::string at_vid(const Vid vid) const;

  /** Returns the Token for the given `surface` form. If not found, returns kUnkVid. */
  Token at(const std::string &surface) const;

  /**
   * Number of word types (including special sentinel symbols).
   * Because vids are not sequential in general, this is NOT THE FIRST FREE vid.
   */
  Vid size() const { return size_; }

  bool contains(const std::string &surface) const;

  /** write out into an empty DB */
  void Write(std::shared_ptr<DB<Token>> db) const;

  /** Finalize an update with seqNum. Flush writes to DB and apply a new persistence sequence number. */
  void Ack(seq_t seqNum);
  /** Current persistence sequence number. */
  seq_t seqNum() const { return seqNum_; }

  class VocabIterator {
  public:
    VocabIterator(const VocabIterator &other): it_(other.it_), end_(other.end_) {}
    VocabIterator(const typename std::unordered_map<Vid, std::string>::const_iterator &it,
                  const typename std::unordered_map<Vid, std::string>::const_iterator &end): it_(it), end_(end) {}

    VocabIterator &operator++() {
      ++it_;
      /*
       *    two users of Vocab::begin()/end()
       *
       *    Vocab.cpp:110  -- Write() - must include </s>
       *    DocumentMap.cpp:107
       *
       *    neither needs skipping.
       *
      // skip reserved IDs (never show those in iteration)
      while(it_->first < Token::kReservedVids && it_ != end_)
        ++it_;
      */
      return *this;
    }

    const Vid &operator*() const { return it_->first; }
    bool operator!=(const VocabIterator &other) const { return it_ != other.it_; }

  private:
    typename std::unordered_map<Vid, std::string>::const_iterator it_;
    typename std::unordered_map<Vid, std::string>::const_iterator end_;
  };

  // note: does NOT keep sorted vid order (iterates unordered_map)
  VocabIterator begin() const;
  VocabIterator end() const;

  /** calls stoul() with sanity checks */
  static Vid str2vid(const std::string &s);

private:
  std::unordered_map<Vid, std::string> id2surface_;
  std::unordered_map<std::string, Vid> surface2id_;
  Vid size_; /** size including special reserved symbols */
  std::shared_ptr<DB<Token>> db_;
  seq_t seqNum_ = 0; /** persistence sequence number */

  /** Load vocabulary from mtt-build .tdx format */
  void ugsapt_load(const std::string &filename);

  /** Load vocabulary from database. @returns true if there was a vocabulary. */
  bool db_load();

  /** put the </s> EOS sentinel at the correct vid. */
  void put_sentinels();
};

/** Empty vocabulary interface without implementations, to provide as a template argument. */
template<class Token>
class DummyVocab {
public:
  typedef typename Token::Vid Vid;
  static constexpr typename Token::Vid kEosVid = Token::kEosVid;

  std::string operator[](const Token token) const { assert(false); return std::string(); }
  std::string at(const Token token) const { assert(false); return std::string(); }
};

} // namespace sto

#endif //STO_VOCAB_H
