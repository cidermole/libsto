/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DB_H
#define STO_DB_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "Types.h"

namespace rocksdb {
class DB;
}

namespace sto {

template<class Token> struct SuffixArrayPosition;
template<class Token> struct SuffixArrayDisk;

/** indicates TreeNodeDisk type, returned by DB::IsNodeLeaf() */
enum NodeType {
  NT_INTERNAL = 0,    /** internal node */
  NT_LEAF_EXISTS = 1, /** existing leaf node */
  NT_LEAF_MISSING = 2 /** should be a leaf node */
};

template<class Token> class DB;

/**
 * Base class for DB, without any particular template argument.
 */
class BaseDB {
public:
  BaseDB(const std::string &basePath, bool bulkLoad=false);

  BaseDB(const BaseDB &other, const std::string &key_prefix);

  ~BaseDB();

  /**
   * Make a shallow copy referencing a different area in the same underlying rocksdb::DB object.
   *
   * @param key_prefix  prefix prepended to all keys, essentially like a namespace/schema in the database
   */
  template<typename Token>
  std::shared_ptr<DB<Token>> PrefixedDB(std::string key_prefix) {
    return std::shared_ptr<DB<Token>>(new DB<Token>(*this, key_prefix_ + key_prefix));
  }

  /** Make a shallow copy with (key,id) as prefix, like PrefixedDB() above. */
  template<typename Token>
  std::shared_ptr<DB<Token>> PrefixedDB(std::string key, size_t id) {
    std::string id_prefix(reinterpret_cast<const char *>(&id), sizeof(id));
    return std::shared_ptr<DB<Token>>(new DB<Token>(*this, key_prefix_ + key + id_prefix));
  }

  /** Flush the writes buffered so far, atomically applying them all together. */
  void Flush() {}

protected:
  std::shared_ptr<rocksdb::DB> db_;
  std::string key_prefix_; /** prefix prepended to all keys, essentially like a namespace/schema in the database */
  bool bulk_;

  /** compact the entire database */
  void CompactRange();
};

/**
 * Persistence methods for TokenIndex and Vocab.
 *
 * Note that writes are buffered, and you must call Flush() to apply them.
 * TODO: use WriteBatch and flush it in Flush()
 *
 * Currently backed by RocksDB.
 */
template<class Token>
class DB : public BaseDB {
public:
  typedef typename Token::Vid Vid;

  static constexpr size_t KEY_PREFIX_LEN = 4; /** key type prefix length in bytes */

  DB(const std::string &basePath); // should we deprecate this?

  /** Conversion from BaseDB, without adding a key prefix. */
  DB(const BaseDB &base);

  /** Convert from BaseDB and add a key prefix. */
  DB(const BaseDB &other, std::string key_prefix);

  DB(const DB &other) = delete;
  ~DB();

  /**
   * Load all vocabulary IDs and their surface forms.
   * @returns vocab size = (maxVid + 1)
   */
  size_t LoadVocab(std::unordered_map<Vid, std::string> &id2surface);

  /** Add a pair of vocabulary ID and surface form */
  void PutVocabPair(Vid vid, const std::string &surface);

  /**
   * Write the children of an internal TreeNode.
   * @param path      path from the root to this TreeNode
   * @param children  vids of children
   */
  void PutNodeInternal(const std::string &path, const std::vector<Vid> &children);

  /**
   * Read the children of an internal TreeNode.
   * @param path      path from the root to this TreeNode
   * @param children  vids of children returned here
   */
  void GetNodeInternal(const std::string &path, std::vector<Vid> &children);

  /**
   * Write the suffix array of a leaf node.
   * @param path  path from the root to this TreeNode
   * @param data  pointer to array data
   * @param len   number of entries (positions) in array
   */
  void PutNodeLeaf(const std::string &path, const SuffixArrayPosition<Token> *data, size_t len);

  /**
   * Read the suffix array of a leaf node.
   * @param path   path from the root to this TreeNode
   * @param array  data returned here
   * @returns true if the leaf was found
   */
  bool GetNodeLeaf(const std::string &path, SuffixArrayDisk<Token> &array);

  /** Delete leaf at the given path. */
  void DeleteNodeLeaf(const std::string &path);

  /**
   * @returns nonzero if this path does not contain an internal node
   * (= is a leaf, or should be if it does not exist yet)
   */
  NodeType IsNodeLeaf(const std::string &path);

  /** Get persistence sequence number */
  seq_t GetSeqNum();
  /** Put persistence sequence number */
  void PutSeqNum(seq_t seqNum);

private:
  //friend class BaseDB; // was for private: DB(const BaseDB &other, std::string key_prefix);

  /** @returns key_prefix_ + key */
  std::string key_(const std::string &k);

  std::string vid_key_(Vid vid);
  std::string surface_key_(const std::string &surface);
  std::string leaf_key_(const std::string &k);
  std::string seqnum_key_();
};

} // namespace sto

#endif //STO_DB_H
