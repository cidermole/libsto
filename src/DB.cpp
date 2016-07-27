/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "DB.h"

#include <iostream>

#include "rocksdb/db.h"
#include "rocksdb/slice_transform.h"

// for SuffixArrayPosition, SuffixArrayDisk
#include "SuffixArrayDisk.h"

namespace sto {

template<class Token>
DB<Token>::DB(const std::string &basePath) {
  rocksdb::DB *db = nullptr;
  rocksdb::Options options;

  options.create_if_missing = true;
  //options.use_fsync = true;
  options.prefix_extractor.reset(rocksdb::NewFixedPrefixTransform(DB::KEY_PREFIX_LEN));

  std::cerr << "opening DB " << basePath << " ..." << std::endl;
  rocksdb::Status status = rocksdb::DB::Open(options, basePath, &db);

  assert(status.ok());
  db_.reset(db);
}

template<class Token>
DB<Token>::~DB() {
  std::cerr << "closing DB." << std::endl;
}

/*
 * Vocabulary storage: we directly store the maps id2surface and surface2id.
 */
template<class Token>
size_t DB<Token>::LoadVocab(std::unordered_map<Vid, std::string> &id2surface) {
  using namespace rocksdb;

  id2surface.clear();

  // prefix scan over id2surface to get all vids

  Vid maxVid = 1; // note: affects size of empty Vocab, via Vocab::db_load()
  auto iter = db_->NewIterator(ReadOptions());
  std::string prefix = "vid_";
  for(iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix); iter->Next()) {
    std::string surface;
    db_->Get(ReadOptions(), iter->key(), &surface);
    Vid id;
    memcpy(&id, iter->key().data() + prefix.size(), sizeof(id));

    id2surface[id] = surface;
    maxVid = std::max(maxVid, id);
  }

  return maxVid + 1;
}

template<class Token>
void DB<Token>::PutVocabPair(Vid vid, const std::string &surface) {
  // TODO: transaction (either both or neither - though only vid assumes surface is there)

  std::string sk = surface_key_(surface); // ensure that underlying byte storage lives long enough
  std::string vk = vid_key_(vid);

  // in presence of vid, LoadVocab() assumes surface will be there as well
  rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), sk, rocksdb::Slice(reinterpret_cast<const char *>(&vid), sizeof(vid)));
  db_->Put(rocksdb::WriteOptions(), vk, surface);
  assert(status.ok());
}

/** write the children of an internal TreeNode */
template<class Token>
void DB<Token>::PutNodeInternal(const std::string &path, const std::vector<Vid> &children) {
  rocksdb::Slice val(reinterpret_cast<const char *>(children.data()), children.size() * sizeof(Vid));
  rocksdb::Slice key = path;
  rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), key, val);
  assert(status.ok());
}

template<class Token>
void DB<Token>::GetNodeInternal(const std::string &path, std::vector<Vid> &children) {
  std::string value;
  rocksdb::Slice key = path;
  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);
  assert(status.ok());

  // copy std::string value -> std::vector<Vid> children
  size_t nchildren = value.size() / sizeof(Vid);
  const Vid *vid = reinterpret_cast<const Vid *>(children.data());
  const Vid *end = reinterpret_cast<const Vid *>(children.data() + nchildren);
  children.clear();
  for(; vid != end; vid++)
    children.push_back(*vid);
}

template<class Token>
std::string DB<Token>::vid_key_(Vid vid) {
  char key_str[4 + sizeof(Vid)] = "vid_";
  memcpy(&key_str[4], &vid, sizeof(Vid));
  std::string key(key_str, 4 + sizeof(Vid));
  return key;
}

template<class Token>
std::string DB<Token>::surface_key_(const std::string &surface) {
  return "srf_" + surface;
}

template<class Token>
void DB<Token>::PutNodeLeaf(const std::string &path, const SuffixArrayPosition<Token> *data, size_t len) {
  rocksdb::Slice key = path;
  rocksdb::Slice val((const char *) data, len * sizeof(SuffixArrayPosition<Token>));
  rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), key, val);
  assert(status.ok());
}

template<class Token>
bool DB<Token>::GetNodeLeaf(const std::string &path, SuffixArrayDisk<Token> &array) {
  rocksdb::Slice key = path;
  std::string value;

  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);
  array = value;

  assert(status.ok() || status.IsNotFound());
  return status.ok();
}

template<class Token>
void DB<Token>::DeleteNodeLeaf(const std::string &path) {
  rocksdb::Slice key = path;
  db_->Delete(rocksdb::WriteOptions(), key);
}

template<class Token>
NodeType DB<Token>::IsNodeLeaf(const std::string &path) {
  // TODO: KEYT_NODE_TYPE  // metadata key with short value. (saves us from reading the whole data blob just for testing if internal/leaf)

  // true if this path does not contain an internal node

  rocksdb::Slice key = path;
  std::string array_key_str = path + "/array";
  std::string value_discarded;

  if(db_->Get(rocksdb::ReadOptions(), array_key_str , &value_discarded).ok()) {
    // has array -> leaf
    return NT_LEAF_EXISTS;
  } else if(db_->Get(rocksdb::ReadOptions(), key, &value_discarded).ok()) {
    // has children -> internal node
    return NT_INTERNAL;
  } else {
    // no array, no children -> need empty leaf node
    return NT_LEAF_MISSING;
  }
}

// explicit template instantiation
template class DB<SrcToken>;
template class DB<TrgToken>;

} // namespace sto
