/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DB_H
#define STO_DB_H

#include <memory>
#include <string>
#include <unordered_map>

#include "Types.h"

namespace rocksdb {
class DB;
}

namespace sto {

/**
 * Persistence methods for TokenIndex and Vocab.
 * Currently backed by RocksDB.
 */
template<class Token>
class DB {
public:
  typedef typename Token::Vid Vid;

  static constexpr size_t KEY_PREFIX_LEN = 4; /** key type prefix length in bytes */

  DB(const std::string &basePath);
  DB(const DB &other) = delete;
  ~DB();

  /** load all vocabulary IDs and their surface forms. @returns vocab size = (maxVid + 1) */
  size_t LoadVocab(std::unordered_map<Vid, std::string> &id2surface);

  /** add a pair of vocabulary ID and surface form. */
  void AddVocabPair(Vid vid, const std::string &surface);

  // temporary
  rocksdb::DB *get() { return db_.get(); }

private:
  std::unique_ptr<rocksdb::DB> db_;

  static std::string vid_key_(Vid vid);
  static std::string surface_key_(const std::string &surface);
};

} // namespace sto

#endif //STO_DB_H
