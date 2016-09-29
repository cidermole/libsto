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
#include <set>

#include "Types.h"
#include "StreamVersions.h"
#include "SuffixArrayMemory.h"

namespace rocksdb {
class DB;
}

namespace sto {

/** indicates TreeNodeDisk type, returned by DB::IsNodeLeaf() */
enum NodeType {
  NT_INTERNAL = 0,    /** internal node */
  NT_LEAF_EXISTS = 1, /** existing leaf node */
  NT_LEAF_MISSING = 2 /** should be a leaf node */
};

template<class Token> class DB;

struct DBKeyInfo {
  std::string lang;
  domid_t domain;
};

struct PerformanceCounters {
  double leafTime_ = 0.0;
  double internalTime_ = 0.0;
  size_t leafCount_ = 0;
  size_t internalCount_ = 0;
  size_t leafBytes_ = 0;
  size_t internalBytes_ = 0;

  void ResetCounters() {
    leafTime_ = 0.0;
    internalTime_ = 0.0;
    leafCount_ = 0;
    internalCount_ = 0;
    leafBytes_ = 0;
    internalBytes_ = 0;
  }

  std::string DebugPerformanceSummary() const;
};

/**
 * Base class for DB, without any particular template argument.
 */
class BaseDB {
public:
  BaseDB(const std::string &basePath, bool bulkLoad=false);

  BaseDB(const BaseDB &other, const std::string &key_prefix);

  virtual ~BaseDB();

  /**
   * Make a shallow copy referencing a different area in the same underlying rocksdb::DB object.
   *
   * @param key_prefix  prefix prepended to all keys, essentially like a namespace/schema in the database
   */
  template<typename Token>
  std::shared_ptr<DB<Token>> PrefixedDB(std::string key_prefix) {
    return std::shared_ptr<DB<Token>>(new DB<Token>(*this, key_prefix_ + key_prefix));
  }

  /** This is not actually a prefix, to do: change to better name */
  template<typename Token>
  std::shared_ptr<DB<Token>> PrefixedDB(std::string lang, domid_t domain) {
    return std::shared_ptr<DB<Token>>(new DB<Token>(*this, DBKeyInfo{lang, domain}));
  }

  PerformanceCounters &GetPerformanceCounters() { return *counters_; }

  /** Flush the writes buffered so far, atomically applying them all together. */
  void Flush() {}

protected:
  std::shared_ptr<PerformanceCounters> counters_;
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

  DB(const BaseDB &other, DBKeyInfo info);

  DB(const DB &other) = delete;
  virtual ~DB();

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
   * @param array pointer to array data
   */
  void PutNodeLeaf(const std::string &path, const SuffixArrayMemory<Token> &array);

  /**
   * Read the suffix array of a leaf node.
   * @param path   path from the root to this TreeNode
   * @param array  data returned here
   * @returns true if the leaf was found
   */
  bool GetNodeLeaf(const std::string &path, SuffixArrayMemory<Token> &array);

  /** Delete leaf at the given path. */
  void DeleteNodeLeaf(const std::string &path);

  /**
   * @returns nonzero if this path does not contain an internal node
   * (= is a leaf, or should be if it does not exist yet)
   */
  NodeType IsNodeLeaf(const std::string &path);

  /** Gets the domains indexed. */
  std::set<domid_t> GetIndexedDomains(const std::string &lang);

  /** Get persistence sequence number */
  StreamVersions GetStreamVersions();
  /** Put persistence sequence number */
  void PutStreamVersions(StreamVersions streamVersions);

private:
  DBKeyInfo info_;

  /** @returns key_prefix_ + key */
  std::string key_(const std::string &k);

  bool has_key_(const std::string &k);

  std::string vid_key_(Vid vid);
  std::string surface_key_(const std::string &surface);
  std::string leaf_key_(const std::string &path);
  std::string stream_key_prefix_();
  std::string stream_key_(stream_t stream);
  std::string internal_key_(const std::string &path);
  std::string domain_prefix_(const std::string &lang);
};

} // namespace sto

#endif //STO_DB_H
