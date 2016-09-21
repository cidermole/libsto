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
#include "DB.h"

namespace sto {

template<class Token>
Vocab<Token>::Vocab(std::shared_ptr<DB<Token>> db) : size_(0), db_(db) {
  if(db_) {
    bool exists = db_load();
    if(!exists) {
      // store </s> sentinel if necessary
      if(Token::kEosVid != Token::kInvalidVid)
        db_->PutVocabPair(kEosVid, kEosSurface);
      if(Token::kUnkVid != Token::kInvalidVid)
        db_->PutVocabPair(kUnkVid, kUnkSurface);

      db_->PutSeqNum(seqNum_);
    }
    seqNum_ = db_->GetSeqNum();
  }
  put_sentinels();
}

template<class Token>
Vocab<Token>::Vocab(const std::string &filename) : size_(0) /* set later */ {
  ugsapt_load(filename);
  put_sentinels();
  seqNum_ = 1; // for legacy data, to make tests happy
}

template<class Token>
std::string Vocab<Token>::operator[](const Token token) const {
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
    size_++;
    Vid id = std::stoi(surface);
    surface2id_[surface] = id;
    id2surface_[id] = surface;
    if(db_)
      db_->PutVocabPair(id, surface);
    return Token{id};
  }
}

template<class Token>
std::string Vocab<Token>::at(const Token token) const {
  //return id2surface_.at(token.vid);
  return std::to_string(token.vid);
}

template<class Token>
std::string Vocab<Token>::at_vid(Vid vid) const {
  //return id2surface_.at(vid);
  return std::to_string(vid);
}

template<class Token>
Token Vocab<Token>::at(const std::string &surface) const {
  return Token{static_cast<Vid>(std::stoi(surface))};
/*
  auto it = surface2id_.find(surface);
  if(it == surface2id_.end())
    return Token{kUnkVid};
  return Token{surface2id_.at(surface)};
*/
}

template<class Token>
typename Vocab<Token>::VocabIterator Vocab<Token>::begin() const {
  // for regular Tokens, this should include </s>
  // for other Vocab use (see DocumentMap), we start from the first valid entry
  //return Token{1};

  return VocabIterator(id2surface_.begin(), id2surface_.end());
}

template<class Token>
typename Vocab<Token>::VocabIterator Vocab<Token>::end() const {
  return VocabIterator(id2surface_.end(), id2surface_.end());
}

template<class Token>
bool Vocab<Token>::contains(const std::string &surface) const {
  return surface2id_.find(surface) != surface2id_.end();
}

template<class Token>
void Vocab<Token>::Write(std::shared_ptr<DB<Token>> db) const {
  // ensure that DB does not have a vocabulary here
  std::unordered_map<Vid, std::string> tmp;
  db->LoadVocab(tmp);
  if(tmp.size())
    throw std::runtime_error("Vocab::Write() does not yet support overwrite."); // is it valid to issue Delete() to RocksDB while iterating?

  // write entries
  typename std::unordered_map<Vid, std::string>::const_iterator it = id2surface_.begin();
  for(; it != id2surface_.end(); ++it)
    db->PutVocabPair(it->first, it->second);
}

template<class Token>
void Vocab<Token>::Ack(seq_t seqNum) {
  assert(seqNum > seqNum_);
  if(seqNum <= seqNum_)
    return;

  seqNum_ = seqNum;

  if(db_) {
    db_->PutSeqNum(seqNum_);
    db_->Flush();
  }
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
void Vocab<Token>::ugsapt_load(const std::string &filename) {
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
bool Vocab<Token>::db_load() {
  size_ = db_->LoadVocab(id2surface_); // note: affects size of empty Vocab
  for(auto p : id2surface_)
    surface2id_[p.second] = p.first;
  return id2surface_.size() > 0;
}

template<class Token>
void Vocab<Token>::put_sentinels() {
  // needed for this vocab type, and not loaded from DB?
  if(Token::kEosVid != Token::kInvalidVid && id2surface_.find(kEosVid) == id2surface_.end()) {
    surface2id_[kEosSurface] = kEosVid;
    id2surface_[kEosVid] = kEosSurface;
    size_++;

    // vid == kEOS must not be used by any word because we use it in TokenIndex as a sentinel.
    // </s> must also have the lowest vid for correctness of comparing shorter sequences as less in TokenIndex.
    assert(at(kEosSurface).vid == kEosVid);
  }

  if(Token::kUnkVid != Token::kInvalidVid && id2surface_.find(kUnkVid) == id2surface_.end()) {
    surface2id_[kUnkSurface] = kUnkVid;
    id2surface_[kUnkVid] = kUnkSurface;
    size_++;
  }
}

template<class Token> constexpr typename DummyVocab<Token>::Vid DummyVocab<Token>::kEosVid;

template<class Token> constexpr typename Vocab<Token>::Vid Vocab<Token>::kEosVid;
template<class Token> constexpr char Vocab<Token>::kEosSurface[];

template<class Token> constexpr typename Vocab<Token>::Vid Vocab<Token>::kUnkVid;
template<class Token> constexpr char Vocab<Token>::kUnkSurface[];

// explicit template instantiation
template class Vocab<SrcToken>;
template class Vocab<TrgToken>;
template class Vocab<Domain>;

template class DummyVocab<AlignmentLink>;

} // namespace sto
