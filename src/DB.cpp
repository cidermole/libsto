/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "DB.h"

#include "rocksdb/db.h"
#include "rocksdb/slice_transform.h"

namespace sto {

template<class Token>
DB<Token>::DB(const std::string &basePath) {
  rocksdb::DB *db = nullptr;
  rocksdb::Options options;

  options.create_if_missing = true;
  //options.use_fsync = true;
  options.prefix_extractor.reset(rocksdb::NewFixedPrefixTransform(DB::KEY_PREFIX_LEN));

  //std::cerr << "opening DB " << basePath << " ..." << std::endl;
  rocksdb::Status status = rocksdb::DB::Open(options, basePath, &db);

  assert(status.ok());
  db_.reset(db);
}

template<class Token>
DB<Token>::~DB() {
  //std::cerr << "closing DB." << std::endl;
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
void DB<Token>::AddVocabPair(Vid vid, const std::string &surface) {
  // TODO: transaction (either both or neither - though only vid assumes surface is there)

  std::string sk = surface_key_(surface); // ensure that underlying byte storage lives long enough
  std::string vk = vid_key_(vid);

  // in presence of vid, LoadVocab() assumes surface will be there as well
  db_->Put(rocksdb::WriteOptions(), sk, rocksdb::Slice(reinterpret_cast<const char *>(&vid), sizeof(vid)));
  db_->Put(rocksdb::WriteOptions(), vk, surface);
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


// explicit template instantiation
template class DB<SrcToken>;
template class DB<TrgToken>;

} // namespace sto
