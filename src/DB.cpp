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

#include "util/Time.h"

namespace sto {

static std::string now() {
  return std::string("[") + format_time(current_time()) + "] ";
}

BaseDB::BaseDB(const std::string &basePath, bool bulkLoad): bulk_(bulkLoad) {
  rocksdb::DB *db = nullptr;
  rocksdb::Options options;

  if(bulkLoad)
    options.PrepareForBulkLoad();
  options.create_if_missing = true;
  //options.use_fsync = true;

  //options.prefix_extractor.reset(rocksdb::NewFixedPrefixTransform(DB::KEY_PREFIX_LEN)); // cannot use this with variable-length key_prefix_

  //std::cerr << "opening DB " << basePath << " ..." << std::endl;
  rocksdb::Status status = rocksdb::DB::Open(options, basePath, &db);

  assert(status.ok());
  db_.reset(db);
}

BaseDB::BaseDB(const BaseDB &other, const std::string &key_prefix) : db_(other.db_), key_prefix_(key_prefix), bulk_(other.bulk_)
{}

BaseDB::~BaseDB() {
  // bulk load and we are the last user?
  if(bulk_ && this->db_.use_count() == 1) {
    std::cerr << now() << "~BaseDB() running CompactRange()..." << std::endl;
    CompactRange();
    std::cerr << now() << "~BaseDB() CompactRange() finished." << std::endl;
  }
}

void BaseDB::CompactRange() {
  this->db_->CompactRange(rocksdb::CompactRangeOptions(), nullptr, nullptr);
}

// ----------------------------------------------------------------------------

template<class Token>
DB<Token>::DB(const std::string &basePath) : BaseDB(basePath)
{}

template<class Token>
DB<Token>::DB(const BaseDB &base) : BaseDB(base, "")
{}

template<class Token>
DB<Token>::~DB() {
  //std::cerr << "closing DB." << std::endl;
  if(this->nleaves_)
    std::cerr << "DB: written " << this->nleaves_ << " in " << format_time(this->putLeafTime_) << " s" << std::endl;
}

/*
 * Vocabulary storage: we directly store the maps id2surface and surface2id.
 */
template<class Token>
size_t DB<Token>::LoadVocab(std::unordered_map<Vid, std::string> &id2surface) {
  using namespace rocksdb;

  id2surface.clear();

  // prefix scan over id2surface to get all vids

  size_t size = 0;
  auto iter = this->db_->NewIterator(ReadOptions());
  std::string prefix = key_("vid_");
  //std::cerr << "LoadVocab() prefix = " << prefix << std::endl;
  for(iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix); iter->Next()) {
    std::string surface;
    this->db_->Get(ReadOptions(), iter->key(), &surface);
    Vid id;
    memcpy(&id, iter->key().data() + prefix.size(), sizeof(id));

    id2surface[id] = surface;
    size++;
  }

  //std::cerr << "DB::LoadVocab(): prefix scan of key=" << prefix << " found " << size << " keys" << std::endl;
  return size;
}

template<class Token>
void DB<Token>::PutVocabPair(Vid vid, const std::string &surface) {
  // TODO: transaction (either both or neither - though only vid assumes surface is there)

  std::string sk = surface_key_(surface); // ensure that underlying byte storage lives long enough
  std::string vk = vid_key_(vid);

  //std::cerr << "PutVocabPair() sk = " << sk << std::endl;

  // in presence of vid, LoadVocab() assumes surface will be there as well
  rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), sk, rocksdb::Slice(reinterpret_cast<const char *>(&vid), sizeof(vid)));
  this->db_->Put(rocksdb::WriteOptions(), vk, surface);
  assert(status.ok());
}

/** write the children of an internal TreeNode */
template<class Token>
void DB<Token>::PutNodeInternal(const std::string &path, const std::vector<Vid> &children) {
  rocksdb::Slice val(reinterpret_cast<const char *>(children.data()), children.size() * sizeof(Vid));
  std::string key = key_(path);
  //std::cerr << "DB::PutNodeInternal(key=" << key << ", children.size()=" << children.size() << ")" << std::endl;
  rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), key, val);
  assert(status.ok());
}

template<class Token>
void DB<Token>::GetNodeInternal(const std::string &path, std::vector<Vid> &children) {
  std::string value;
  std::string key = key_(path);
  rocksdb::Status status = this->db_->Get(rocksdb::ReadOptions(), key, &value);
  assert(status.ok());

  // copy std::string value -> std::vector<Vid> children
  size_t nchildren = value.size() / sizeof(Vid);
  const Vid *vid = reinterpret_cast<const Vid *>(value.data());
  const Vid *end = vid + nchildren;
  children.clear();
  for(; vid != end; vid++)
    children.push_back(*vid);
}

template<class Token>
std::string DB<Token>::vid_key_(Vid vid) {
  char key_str[4 + sizeof(Vid)] = "vid_";
  memcpy(&key_str[4], &vid, sizeof(Vid));
  std::string key(key_str, 4 + sizeof(Vid));
  return key_(key);
}

template<class Token>
std::string DB<Token>::surface_key_(const std::string &surface) {
  return key_("srf_" + surface);
}

template<class Token>
std::string DB<Token>::leaf_key_(const std::string &k) {
  return key_("arr_" + k);
}

template<class Token>
std::string DB<Token>::seqnum_key_() {
  return key_("seqn");
}

template<class Token>
void DB<Token>::PutNodeLeaf(const std::string &path, const SuffixArrayPosition<Token> *data, size_t len) {
  std::string key = leaf_key_(path);
  //std::cerr << "DB::PutNodeLeaf(key=" << key << ", len=" << len << ")" << std::endl;
  rocksdb::Slice val((const char *) data, len * sizeof(SuffixArrayPosition<Token>));
  this->putLeafTime_ += benchmark_time([&](){
    rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), key, val);
    assert(status.ok());
  });

  this->nleaves_++;
}

template<class Token>
bool DB<Token>::GetNodeLeaf(const std::string &path, SuffixArrayDisk<Token> &array) {
  std::string key = leaf_key_(path);
  std::string value;

  rocksdb::Status status = this->db_->Get(rocksdb::ReadOptions(), key, &value);
  //std::cerr << "DB::GetNodeLeaf(key=" << key << ") = " << status.ok() << std::endl;
  array = value;

  assert(status.ok() || status.IsNotFound());
  return status.ok();
}

template<class Token>
void DB<Token>::DeleteNodeLeaf(const std::string &path) {
  std::string key = leaf_key_(path);
  this->db_->Delete(rocksdb::WriteOptions(), key);
}

template<class Token>
NodeType DB<Token>::IsNodeLeaf(const std::string &path) {
  // TODO: KEYT_NODE_TYPE  // metadata key with short value. (saves us from reading the whole data blob just for testing if internal/leaf)

  // true if this path does not contain an internal node

  std::string key = key_(path);
  std::string array_key_str = key_(path + "/array");
  std::string value_discarded;

  //std::cerr << "DB::IsNodeLeaf(key=" << key << ", array_key=" << array_key_str << ")" << std::endl;

  if(this->db_->Get(rocksdb::ReadOptions(), array_key_str, &value_discarded).ok()) {
    // has array -> leaf
    return NT_LEAF_EXISTS;
  } else if(this->db_->Get(rocksdb::ReadOptions(), key, &value_discarded).ok()) {
    // has children -> internal node
    return NT_INTERNAL;
  } else {
    // no array, no children -> need empty leaf node
    return NT_LEAF_MISSING;
  }
}

template<class Token>
seq_t DB<Token>::GetSeqNum() {
  std::string key = seqnum_key_();
  std::string value;
  rocksdb::Status status = this->db_->Get(rocksdb::ReadOptions(), key, &value);
  assert(status.ok() || status.IsNotFound());
  if(!status.ok())
    return 0;
  return *reinterpret_cast<const seq_t *>(value.data());
}

template<class Token>
void DB<Token>::PutSeqNum(seq_t seqNum) {
  std::string key = seqnum_key_();
  rocksdb::Slice val(reinterpret_cast<const char *>(&seqNum), sizeof(seqNum));
  rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), key, val);
  assert(status.ok());
}

template<class Token>
DB<Token>::DB(const BaseDB &other, std::string key_prefix) : BaseDB(other, key_prefix)
{}

template<class Token>
std::string DB<Token>::key_(const std::string &k) {
  return this->key_prefix_ + k;
}

// explicit template instantiation
template class DB<SrcToken>;
template class DB<TrgToken>;
template class DB<Domain>;

} // namespace sto
