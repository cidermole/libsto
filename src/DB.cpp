/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "DB.h"

#include <iostream>
#include <sstream>

#include "rocksdb/db.h"
#include "rocksdb/slice_transform.h"

// for SuffixArrayPosition, SuffixArrayDisk
#include "SuffixArrayDisk.h"

#include "util/Time.h"

namespace sto {

static std::string now() {
  return std::string("[") + format_time(current_time()) + "] ";
}

BaseDB::BaseDB(const std::string &basePath, bool bulkLoad): counters_(new PerformanceCounters), bulk_(bulkLoad) {
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

BaseDB::BaseDB(const BaseDB &other, const std::string &key_prefix) : counters_(other.counters_), db_(other.db_), key_prefix_(key_prefix), bulk_(other.bulk_)
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
  if(this->counters_->leafCount_)
    std::cerr << "DB: written " << this->counters_->leafCount_ << " in " << format_time(this->counters_->leafTime_) << " s" << std::endl;
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
  std::string key = internal_key_(path);
  //std::cerr << "DB::PutNodeInternal(key=" << key << ", children.size()=" << children.size() << ")" << std::endl;
  this->counters_->internalTime_ += benchmark_time([&](){
    rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), key, val);
    assert(status.ok());
  });
  this->counters_->internalBytes_ += key.size() + val.size();
  this->counters_->internalCount_++;
}

template<class Token>
void DB<Token>::GetNodeInternal(const std::string &path, std::vector<Vid> &children) {
  std::string value;
  std::string key = internal_key_(path);
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


template<typename T>
static std::string to_bytes(T n) {
  return std::string(reinterpret_cast<const char *>(&n), sizeof(n));
}

template<class Token>
std::string DB<Token>::vid_key_(Vid vid) {
  throw std::runtime_error("vid_key_ unused");

  char is_root_dummy = 0;
  char key_str[4 + sizeof(Vid)] = "vid_";
  memcpy(&key_str[4], &vid, sizeof(Vid));
  std::string key(key_str, 4 + sizeof(Vid));
  return key_(info_.lang + is_root_dummy + to_bytes(info_.domain) + key);
}

template<class Token>
std::string DB<Token>::surface_key_(const std::string &surface) {
  throw std::runtime_error("surface_key_ unused");
  char is_root_dummy = 0;
  return key_(info_.lang + is_root_dummy + to_bytes(info_.domain) + "srf_" + surface);
}


template<class Token>
std::string DB<Token>::leaf_key_(const std::string &path) {
  char is_root = (path == "");
  return key_(info_.lang + is_root + to_bytes(info_.domain) + "arr_" + path);
}

template<class Token>
std::string DB<Token>::stream_key_prefix_() {
  char is_root_dummy = 0;
  return key_(info_.lang + is_root_dummy + to_bytes(info_.domain) + "seqn");
}

template<class Token>
std::string DB<Token>::stream_key_(stream_t stream) {
  char is_root_dummy = 0;
  char key_str[4 + sizeof(stream_t)] = "seqn";
  memcpy(&key_str[4], &stream, sizeof(stream_t));
  std::string key(key_str, 4 + sizeof(stream_t));
  return key_(info_.lang + is_root_dummy + to_bytes(info_.domain) + key);
}

template<class Token>
std::string DB<Token>::internal_key_(const std::string &path) {
  char is_root = (path == "");
  return key_(info_.lang + is_root + to_bytes(info_.domain) + "int_" + path);
}

template<class Token>
std::string DB<Token>::domain_prefix_(const std::string &lang) {
  char is_root = 1;
  return key_(lang + is_root);
}

template<class Token>
void DB<Token>::PutNodeLeaf(const std::string &path, const SuffixArrayPosition<Token> *data, size_t len) {
  std::string key = leaf_key_(path);
  //std::cerr << "DB::PutNodeLeaf(key=" << key << ", len=" << len << ")" << std::endl;
  rocksdb::Slice val((const char *) data, len * sizeof(SuffixArrayPosition<Token>));
  this->counters_->leafTime_ += benchmark_time([&](){
    rocksdb::Status status = this->db_->Put(rocksdb::WriteOptions(), key, val);
    assert(status.ok());
  });
  this->counters_->leafBytes_ += key.size() + val.size();
  this->counters_->leafCount_++;
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

  std::string key = internal_key_(path);
  std::string array_key_str = leaf_key_(path);
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
StreamVersions DB<Token>::GetStreamVersions() {
  using namespace rocksdb;

  StreamVersions versions;

  // prefix scan to get all streams

  auto iter = this->db_->NewIterator(ReadOptions());
  std::string prefix = stream_key_prefix_();
  for(iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix); iter->Next()) {
    std::string value;
    this->db_->Get(ReadOptions(), iter->key(), &value);
    stream_t stream;
    memcpy(&stream, iter->key().data() + prefix.size(), sizeof(stream));
    seqid_t seqid = *reinterpret_cast<const seqid_t *>(value.data());
    versions[stream] = seqid;
  }

  return versions;
}

template<class Token>
std::set<domid_t> DB<Token>::GetIndexedDomains(const std::string &lang) {
  using namespace rocksdb;

  std::set<domid_t> domains;

  // prefix scan to get all domains

  auto iter = this->db_->NewIterator(ReadOptions());
  std::string prefix = domain_prefix_(lang);
  for(iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix); iter->Next()) {
    domid_t domain;
    memcpy(&domain, iter->key().data() + prefix.size(), sizeof(domain));
    domains.insert(domain);
  }

  return domains;
}

template<class Token>
void DB<Token>::PutStreamVersions(StreamVersions streamVersions) {
  rocksdb::WriteBatch batch;

  for(auto entry : streamVersions) {
    stream_t stream = entry.first;
    seqid_t seqid = entry.second;

    std::string key = stream_key_(stream);
    rocksdb::Slice val(reinterpret_cast<const char *>(&seqid), sizeof(seqid));
    batch.Put(key, val);
  }

  rocksdb::Status status = this->db_->Write(rocksdb::WriteOptions(), &batch);
  assert(status.ok());
}

template<class Token>
DB<Token>::DB(const BaseDB &other, std::string key_prefix) : BaseDB(other, key_prefix)
{}

template<class Token>
DB<Token>::DB(const BaseDB &other, DBKeyInfo info) : BaseDB(other, ""), info_(info)
{}

template<class Token>
std::string DB<Token>::key_(const std::string &k) {
  return this->key_prefix_ + k;
}

std::string PerformanceCounters::DebugPerformanceSummary() const {
  std::stringstream ss;

  ss << "DB leaves: count=" << leafCount_ << " bytes=" << leafBytes_ << " time=" << leafTime_ << std::endl;
  ss << "DB internal nodes: count=" << internalCount_ << " bytes=" << internalBytes_ << " time=" << internalTime_ << std::endl;

  return ss.str();
}

// explicit template instantiation
template class DB<SrcToken>;
template class DB<TrgToken>;
template class DB<Domain>;

} // namespace sto
