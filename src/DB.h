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

  /** compact the entire database */
  void CompactRange();

  /**
   * Make a shallow copy referencing a different area in the same underlying rocksdb::DB object.
   *
   * @param key_prefix  prefix prepended to all keys, essentially like a global namespace/schema in the database
   */
  std::shared_ptr<DB<Token>> PrefixedDB(std::string key_prefix);

private:
  std::shared_ptr<rocksdb::DB> db_;
  std::string key_prefix_; /** prefix prepended to all keys, essentially like a global namespace/schema in the database */

  /** ctor used by PrefixedDB() */
  DB(const DB &other, std::string key_prefix);

  /** @returns key_prefix_ + key */
  std::string key_(const std::string &k);

  std::string vid_key_(Vid vid);
  std::string surface_key_(const std::string &surface);
};

} // namespace sto

#endif //STO_DB_H
