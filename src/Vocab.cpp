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
Vocab<Token>::Vocab(std::shared_ptr<DB<Token>> db) : size_(2), db_(db) {
  if(db_) {
    bool exists = db_load();
    if(!exists) {
      // store </s> sentinel
      db_->AddVocabPair(kEOS, kEOSSurface);
    }
  }
  put_eos();
}

template<class Token>
Vocab<Token>::Vocab(const std::string &filename) : size_(0) /* set later */ {
  ugsapt_load(filename);
  put_eos();
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
      db_->AddVocabPair(id, surface);
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
void Vocab<Token>::put_eos() {
  surface2id_["</s>"] = kEOS;
  id2surface_[kEOS] = "</s>";

  // vid == kEOS must not be used by any word because we use it in TokenIndex as a sentinel.
  // </s> must also have the lowest vid for correctness of comparing shorter sequences as less in TokenIndex.
  Token eos = at("</s>");
  assert(eos.vid == kEOS);
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
