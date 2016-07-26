/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <memory>

#include "Vocab.h"
#include "Types.h"
#include <cstring>
#include "rocksdb/db.h"

namespace sto {

template<class Token>
Vocab<Token>::Vocab(rocksdb::DB *db) : size_(1), db_(db) {
  // TODO: if db passed, check: exists -> load from DB, unexists -> store </s> sentinel to DB

  if(db_) {
    std::string key_str = vid_key(kEOS);
    std::string discarded;

    if(db_->Get(rocksdb::ReadOptions(), key_str, &discarded).ok()) {
      // if vocab exists -> load from DB
      db_load();

      // TODO DRY
      surface2id_["</s>"] = kEOS;
      id2surface_[kEOS] = "</s>";

      // vid == kEOS must not be used by any word because we use it in TokenIndex as a sentinel.
      Token eos = at("</s>");
      assert(eos.vid == kEOS);

      return;
    } else {
      // if not exists -> store </s> sentinel
      db_put_pair(kEOS, kEOSSurface);

      // to do: why not call the generic update code (once it exists) even for </s>? and get rid of below code outside of if(db)
    }
  }

  // insert </s> sentinel to ensure it has the lowest possible vid
  // vid == kEOS must not be used by any word because we use it in TokenIndex as a sentinel.
  Token eos = operator[]("</s>");
  assert(eos.vid == kEOS);
  assert(size_ == 2);
}

template<class Token>
Vocab<Token>::Vocab(const std::string &filename, rocksdb::DB *db) : size_(0) /* set later */, db_(db) {
  load_ugsapt_tdx(filename);

  // TODO remove UNK from vocab. or build with mtt-build -unk '</s>'
  surface2id_["</s>"] = kEOS;
  id2surface_[kEOS] = "</s>";

  // vid == kEOS must not be used by any word because we use it in TokenIndex as a sentinel.
  Token eos = at("</s>");
  assert(eos.vid == kEOS);
}

template<class Token>
const std::string& Vocab<Token>::operator[](const Token token) const {
  return id2surface_.at(token.vid);
}

template<class Token>
Token Vocab<Token>::operator[](const std::string &surface) {
  auto result = surface2id_.find(surface);
  if(result != surface2id_.end()) {
    // retrieve result
    return Token{result->second};
  } else {
    // insert
    Vid id = size_++;
    surface2id_[surface] = id;
    id2surface_[id] = surface;
    if(db_)
      db_put_pair(id, surface);
    return Token{id};
  }
}

template<class Token>
std::string Vocab<Token>::at(const Token token) const {
  return id2surface_.at(token.vid);
}

template<class Token>
std::string Vocab<Token>::at_vid(Vid vid) const {
  return id2surface_.at(vid);
}

template<class Token>
Token Vocab<Token>::at(const std::string &surface) const {
  return Token{surface2id_.at(surface)};
}

template<class Token>
Token Vocab<Token>::begin() const {
  return Token{1};
}

template<class Token>
Token Vocab<Token>::end() const {
  return Token{size_};
}

struct UGVocabHeader {
  uint32_t size; /** size of vocabulary (and index) */
  uint32_t unk_vid; /** vocabulary ID of UNK word */
};

struct UGIndexEntry {
  uint32_t offset; /** offset of surface string */
  uint32_t vid; /** vocabulary ID */
};

off_t fsize(const char *filename) {
  struct stat st;

  if (stat(filename, &st) == 0)
    return st.st_size;

  throw std::runtime_error(std::string("failed to stat file ") + filename);
}

template<class Token>
void Vocab<Token>::load_ugsapt_tdx(const std::string &filename) {
  /* Load vocabulary from mtt-build .tdx format */

  UGVocabHeader header;
  FILE *fin = fopen(filename.c_str(), "rb");
  if(fin == nullptr)
    throw std::runtime_error(std::string("failed to open file ") + filename);

  fread(&header, sizeof(header), 1, fin);

  std::unique_ptr<UGIndexEntry[]> index(new UGIndexEntry[header.size]);
  fread(index.get(), sizeof(UGIndexEntry), header.size, fin);

  size_t strings_size =
      fsize(filename.c_str())
      - sizeof(UGVocabHeader)
      - sizeof(UGIndexEntry) * header.size;

  std::unique_ptr<char[]> strings(new char[strings_size]);
  fread(strings.get(), sizeof(char), strings_size, fin);
  fclose(fin);

  for(size_t i = 0; i < header.size; i++) {
    char *s = strings.get() + index[i].offset;
    Vid id = index[i].vid;
    std::string surface = s;
    surface2id_[surface] = id;
    id2surface_[id] = surface;
  }
  size_ = header.size;
}

template<class Token>
std::string Vocab<Token>::vid_key(Vid vid) {
  char key_str[4 + sizeof(Vid)] = "vid_";
  memcpy(&key_str[4], &vid, sizeof(Vid));
  std::string key(key_str, 4 + sizeof(Vid));
  return key;
}

template<class Token>
std::string Vocab<Token>::surface_key(const std::string &surface) {
  return "srf_" + surface;
}

template<class Token>
void Vocab<Token>::db_put_pair(Vid vid, const std::string &surface) {
  // TODO: transaction (either both or neither - though only vid assumes surface is there)
  std::string sk = surface_key(surface); // ensure that underlying byte storage lives long enough
  std::string vk = vid_key(vid);
  db_->Put(rocksdb::WriteOptions(), sk, rocksdb::Slice(reinterpret_cast<const char *>(&vid), sizeof(vid)));
  db_->Put(rocksdb::WriteOptions(), vk, surface);
}

template<class Token>
void Vocab<Token>::db_load() {
  using namespace rocksdb;

  // do a prefix scan for vid -> surface, to get all vids

  Vid maxVid = 1;
  auto iter = db_->NewIterator(ReadOptions());
  std::string prefix = "vid_";
  for(iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix); iter->Next()) {
    std::string surface;
    db_->Get(ReadOptions(), iter->key(), &surface);
    Vid id;
    memcpy(&id, iter->key().data() + prefix.size(), sizeof(id));

    id2surface_[id] = surface;
    surface2id_[surface] = id;
    maxVid = std::max(maxVid, id);
  }
  size_ = maxVid + 1;
}

template<class Token>
constexpr typename DummyVocab<Token>::Vid DummyVocab<Token>::kEOS;

template<class Token>
constexpr typename Vocab<Token>::Vid Vocab<Token>::kEOS;

template<class Token>
constexpr char Vocab<Token>::kEOSSurface[];

// explicit template instantiation
template class Vocab<SrcToken>;
template class Vocab<TrgToken>;

template class DummyVocab<AlignmentLink>;

} // namespace sto
