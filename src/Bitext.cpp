/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unistd.h>

#include "Bitext.h"
#include "DB.h"

namespace sto {


template<typename Token>
BitextSide<Token>::BitextSide(const std::string &l) :
    vocab(new sto::Vocab<Token>),
    corpus(new sto::Corpus<Token>(vocab.get())),
    index(new sto::TokenIndex<Token>(*corpus)),
    lang(l)
{
}

/** Load existing BitextSide from DB and disk. */
template<typename Token>
BitextSide<Token>::BitextSide(std::shared_ptr<DB<Token>> db, const std::string &base, const std::string &lang, const DocumentMap &map) :
    vocab(new sto::Vocab<Token>(db->template PrefixedDB<Token>(lang))),
    corpus(new sto::Corpus<Token>(base + lang + ".trk", vocab.get())),
    index(new sto::TokenIndex<Token>("/", *corpus, db->template PrefixedDB<Token>(lang)))  // note: filename is only ever used as DB prefix now.
{
  // load domain indexes
  for(auto docid : map)
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token>>(/* filename = */ "/", *corpus, db->template PrefixedDB<Token>(lang, docid));
}

template<typename Token>
BitextSide<Token>::~BitextSide()
{}

template<typename Token>
void BitextSide<Token>::Open(const std::string &base, const std::string &lang) {
  this->base = base;
  this->lang = lang;
  base_and_lang = base + lang;
  // token index
  vocab.reset(new Vocab<Token>(base_and_lang+".tdx"));
  // mapped corpus track
  XVERBOSE(2, " sto::Corpus()...\n");
  corpus.reset(new Corpus<Token>(base_and_lang+".mct", vocab.get()));
  index.reset(new sto::TokenIndex<Token>(*corpus));
  this->base_and_lang = base_and_lang;
}

template<typename Token>
void BitextSide<Token>::CreateGlobalIndex() {
  std::string index_file = base_and_lang + ".sfa";
  if(!access(index_file.c_str(), F_OK)) {
    // load index from disk, if possible
    index.reset(new sto::TokenIndex<Token>(index_file, *corpus));
    return;
  }

  XVERBOSE(2, " BitextSide<Token>::CreateGlobalIndex()...\n");
  index.reset(new sto::TokenIndex<Token>(*corpus));
  for(size_t i = 0; i < corpus->size(); i++)
    index->AddSentence(corpus->sentence(i));
}

template<typename Token>
void BitextSide<Token>::CreateDomainIndexes(const DocumentMap &map) {
  XVERBOSE(2, " BitextSide<Token>::CreateDomainIndexes()...\n");
  size_t nsents = corpus->size();

  assert(map.numDomains() > 0); // we must have at least 1 domain... otherwise we could just load the global idx.
  std::string index1_file = base_and_lang + "." + map[map.begin()] + ".sfa";
  if(!access(index1_file.c_str(), F_OK)) {
    // load indexes from disk, if possible

    // create TokenIndex objects
    domain_indexes.clear();
    for(auto docid : map)
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token>>(base_and_lang + "." + map[docid] + ".sfa", *corpus);

    return;
  }

  // create TokenIndex objects
  domain_indexes.clear();
  for(auto docid : map)
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token>>(*corpus);

  // put each sentence into the correct domain index
  for(size_t i = 0; i < nsents; i++)
    domain_indexes[map.sid2did(i)]->AddSentence(corpus->sentence(i));
}

template<typename Token>
typename BitextSide<Token>::Sid BitextSide<Token>::AddToCorpus(const std::vector<std::string> &sent) {
  std::vector<Token> toks;
  for(auto t : sent)
    toks.push_back((*vocab)[t]); // vocabulary insert/lookup
  corpus->AddSentence(toks);
  return corpus->size() - 1;
}

template<typename Token>
void BitextSide<Token>::AddToDomainIndex(Sid sid, tpt::docid_type docid) {
  if(domain_indexes.find(docid) == domain_indexes.end())
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token>>(*corpus);
  domain_indexes[docid]->AddSentence(corpus->sentence(sid));
}

template<typename Token>
void BitextSide<Token>::Write(std::shared_ptr<DB<Token>> db, const std::string &base, const DocumentMap &map) {
  vocab->Write(db->template PrefixedDB<Token>(lang));
  corpus->Write(base + lang + ".trk");
  index->Write(db->template PrefixedDB<Token>(lang));

  for(auto docid : map)
    domain_indexes[docid]->Write(db->template PrefixedDB<Token>(lang, docid));
}

// explicit template instantiation
template class BitextSide<SrcToken>;
template class BitextSide<TrgToken>;

///////////////////////////////////////////////////////////////////////////////

Bitext::Bitext(const std::string &l1, const std::string &l2) :
    l1_(l1), l2_(l2),
    src_(l1),
    trg_(l2),
    align_(new sto::Corpus<sto::AlignmentLink>)
{}

/** Load existing Bitext from DB and disk. */
Bitext::Bitext(std::shared_ptr<BaseDB> db, const std::string &base, const std::string &l1, const std::string &l2) :
    l1_(l1), l2_(l2),
    doc_map_(std::make_shared<DB<Domain>>(*db), base + "docmap.trk"),
    src_(std::make_shared<DB<SrcToken>>(*db), base, l1, doc_map_),
    trg_(std::make_shared<DB<TrgToken>>(*db), base, l2, doc_map_),
    align_(new sto::Corpus<sto::AlignmentLink>(base + "align.trk"))
{}

Bitext::~Bitext()
{}

void Bitext::Open(const std::string &base) {
  XVERBOSE(1, "SBitext: opening file base: " << base << "\n");
  src_.Open(base, l1_);
  trg_.Open(base, l2_);
  XVERBOSE(2, " sto::Corpus<AlignmentLink>()...\n");
  align_.reset(new Corpus<AlignmentLink>(base+l1_+"-"+l2_+".mam"));
  doc_map_.Load(base + "dmp", src_.corpus->size());

  // potentially loads global and domain indexes (instead of building them)
  XVERBOSE(1, "SBitext: CreateIndexes()...\n");
  src_.CreateIndexes(doc_map_);
  trg_.CreateIndexes(doc_map_);
  XVERBOSE(1, "SBitext: CreateIndexes() and open() done.\n");
}

void Bitext::AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent, const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) {
  // (1) add to corpus first:

  // order of these three does not matter
  auto isrc = src_.AddToCorpus(srcSent);
  auto itrg = trg_.AddToCorpus(trgSent);
  assert(isrc == itrg);
  align_->AddSentence(std::vector<AlignmentLink>{alignment.begin(), alignment.end()});

  // indexes:

  auto docid = doc_map_.FindOrInsert(domain);
  doc_map_.AddSentence(isrc, docid);

  // (2) domain-specific first: ensures that domain-specific indexes can provide, since we query the global index for the presence of source phrases first.

  // currently, order of these two does not matter here (we query global index first)
  trg_.AddToDomainIndex(itrg, docid);
  src_.AddToDomainIndex(isrc, docid);

  // (3) global index last - everything should be stored by the time readers see a new global source index entry

  // target side first: ensures extraction will work
  trg_.index->AddSentence(trg_.corpus->sentence(itrg));
  src_.index->AddSentence(src_.corpus->sentence(isrc));
}

/** Write to (empty) DB and disk. */
void Bitext::Write(std::shared_ptr<BaseDB> db, const std::string &base) {
  src_.Write(std::make_shared<DB<SrcToken>>(*db), base + l1_, doc_map_);
  trg_.Write(std::make_shared<DB<TrgToken>>(*db), base + l2_, doc_map_);
  align_->Write(base + "align.trk");
  doc_map_.Write(std::make_shared<DB<Domain>>(*db), base + "docmap.trk");
}

} // namespace sto
