/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unistd.h>

#include <limits>

#include "Bitext.h"
#include "DB.h"
#include "ITokenIndex.h"

#include "util/Time.h"

namespace sto {


template<typename Token>
BitextSide<Token>::BitextSide(const std::string &l, const DocumentMap &map) :
    vocab(new sto::Vocab<Token>),
    corpus(new sto::Corpus<Token>(vocab.get())),
    index(new sto::TokenIndex<Token, IndexTypeMemBuf>(*corpus, -1)),
    docMap(map),
    lang(l)
{
}

/** Load existing BitextSide from DB and disk. */
template<typename Token>
BitextSide<Token>::BitextSide(std::shared_ptr<DB<Token>> db, const std::string &base, const std::string &lang, const DocumentMap &map) :
    vocab(new sto::Vocab<Token>(db->template PrefixedDB<Token>("vocab." + lang))),
    corpus(new sto::Corpus<Token>(base + lang + ".trk", vocab.get())),
    index(new sto::TokenIndex<Token, IndexTypeDisk>("/", *corpus, db->template PrefixedDB<Token>("index." + lang))),  // note: filename is only ever used as DB prefix now (in TreeNodeDisk)
    docMap(map),
    lang(lang),
    db(db)
{
  // load domain indexes
  for(auto docid : map)
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeDisk>>(/* filename = */ "/", *corpus, db->template PrefixedDB<Token>(lang, docid));
}

template<typename Token>
BitextSide<Token>::~BitextSide()
{}

template<typename Token>
void BitextSide<Token>::Open(const std::string &base, const std::string &lang) {
  this->lang = lang;
  base_and_lang = base + lang;
  // vocabulary
  vocab.reset(new Vocab<Token>(base_and_lang+".tdx"));
  // mmapped corpus track
  XVERBOSE(2, " sto::Corpus()...\n");
  corpus.reset(new Corpus<Token>(base_and_lang+".mct", vocab.get()));
  index.reset(new sto::TokenIndex<Token, IndexTypeMemory>(*corpus));
  this->base_and_lang = base_and_lang;
}

template<typename Token>
void BitextSide<Token>::CreateGlobalIndex() {
  std::string index_file = base_and_lang + ".sfa";
  if(!access(index_file.c_str(), F_OK)) {
    // load index from disk, if possible
    // despite the slightly misleading name, IndexTypeMemory can mmap the old index format from disk
    index.reset(new sto::TokenIndex<Token, IndexTypeMemory>(index_file, *corpus));
    return;
  }

  XVERBOSE(2, " BitextSide<Token>::CreateGlobalIndex()...\n");
  index.reset(new sto::TokenIndex<Token, IndexTypeMemory>(*corpus));
  for(size_t i = 0; i < corpus->size(); i++)
    index->AddSentence(corpus->sentence(i), docMap.seqNum(i));
}

template<typename Token>
void BitextSide<Token>::CreateDomainIndexes() {
  size_t nsents = corpus->size();

  assert(docMap.numDomains() > 0); // we must have at least 1 domain... otherwise we could just load the global idx.
  std::string index1_file = base_and_lang + "." + docMap[docMap.begin()] + ".sfa";
  if(!access(index1_file.c_str(), F_OK)) {
    // load indexes from disk, if possible

    // create TokenIndex objects
    domain_indexes.clear();
    for(auto docid : docMap)
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeMemory>>(base_and_lang + "." + docMap[docid] + ".sfa", *corpus);

    return;
  }

  XVERBOSE(2, " BitextSide<Token>::CreateDomainIndexes()...\n");

  // create TokenIndex objects
  domain_indexes.clear();
  for(auto docid : docMap)
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeMemory>>(*corpus);

  // put each sentence into the correct domain index
  for(size_t i = 0; i < nsents; i++)
    domain_indexes[docMap.sid2did(i)]->AddSentence(corpus->sentence(i), docMap.seqNum(i));
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
void BitextSide<Token>::AddToDomainIndex(Sid sid, tpt::docid_type docid, seq_t seqNum) {
  if(domain_indexes.find(docid) == domain_indexes.end()) {
    if(db)
      // persisted in DB
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeDisk>>(/* filename = */ "/", *corpus, db->template PrefixedDB<Token>(lang, docid));
    else
      // memory only
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeMemBuf>>(*corpus, -1);
  }
  domain_indexes[docid]->AddSentence(corpus->sentence(sid), seqNum);
}

template<typename Token>
void BitextSide<Token>::Write(std::shared_ptr<DB<Token>> db, const std::string &base) {
  std::cerr << "BitextSide<Token>::Write()..." << std::endl;

  benchmark_time([&](){ vocab->Write(db->template PrefixedDB<Token>("vocab." + lang)); }, "vocab->Write()");

  benchmark_time([&](){ corpus->Write(base + lang + ".trk"); }, "corpus->Write()");

  benchmark_time([&](){ index->Write(db->template PrefixedDB<Token>("index." + lang)); }, "index->Write()");

  benchmark_time([&](){
    for(auto& d : domain_indexes)
      d.second->Write(db->template PrefixedDB<Token>(lang, d.first));
  }, "domain_indexes->Write()");

  std::cerr << "BitextSide<Token>::Write() done." << std::endl;
}

template<typename Token>
void BitextSide<Token>::Ack(seq_t seqNum) {
  vocab->Ack(seqNum);
  // Corpus seqNum is now maintained in DocumentMap
  // TokenIndex calls its own Ack()
}

template<typename Token>
seq_t BitextSide<Token>::seqNum() const {
  // Our seqNum = min({c.seqNum | c in components})

  seq_t seqNum = std::numeric_limits<seq_t>::max();

  seqNum = std::min(seqNum, vocab->seqNum());
  seqNum = std::min(seqNum, docMap.seqNum());
  seqNum = std::min(seqNum, index->seqNum());

  for(auto& d : domain_indexes)
    seqNum = std::min(seqNum, d.second->seqNum());

  // any component with a higher seqNum will have to ignore the repeated updates
  return seqNum;
}

// explicit template instantiation
template class BitextSide<SrcToken>;
template class BitextSide<TrgToken>;

///////////////////////////////////////////////////////////////////////////////

Bitext::Bitext(const std::string &l1, const std::string &l2) :
    l1_(l1), l2_(l2),
    doc_map_(new DocumentMap),
    src_(new BitextSide<sto::SrcToken>(l1, *doc_map_)),
    trg_(new BitextSide<sto::TrgToken>(l2, *doc_map_)),
    align_(new sto::Corpus<sto::AlignmentLink>)
{}

/** Load existing Bitext from DB and disk. */
Bitext::Bitext(const std::string &base, const std::string &l1, const std::string &l2) : Bitext(l1, l2)
{
  OpenIncremental(base);
}

Bitext::~Bitext()
{}

void Bitext::OpenIncremental(const std::string &base) {
  if(l1_ == l2_)
    throw new std::runtime_error("Bitext: src and trg languages are equal - persistence will clash");

  db_.reset(new BaseDB(base + "db"));
  doc_map_.reset(new DocumentMap(std::make_shared<DB<Domain>>(*db_), base + "docmap.trk"));
  src_.reset(new BitextSide<sto::SrcToken>(std::make_shared<DB<SrcToken>>(*db_), base, l1_, *doc_map_));
  trg_.reset(new BitextSide<sto::TrgToken>(std::make_shared<DB<TrgToken>>(*db_), base, l2_, *doc_map_));
  align_.reset(new sto::Corpus<sto::AlignmentLink>(base + l1_ + "-" + l2_ + ".mam"));
}

void Bitext::OpenLegacy(const std::string &base) {
  src_->Open(base, l1_);
  trg_->Open(base, l2_);
  XVERBOSE(2, " sto::Corpus<AlignmentLink>()...\n");
  align_.reset(new Corpus<AlignmentLink>(base+l1_+"-"+l2_+".mam"));
  doc_map_->Load(base + "dmp", src_->corpus->size());

  // potentially loads global and domain indexes (instead of building them)
  XVERBOSE(1, "Bitext: CreateIndexes()...\n");
  src_->CreateIndexes();
  trg_->CreateIndexes();
  XVERBOSE(1, "Bitext: CreateIndexes() and open() done.\n");
}

void Bitext::Open(const std::string &base) {
  std::string db_dir = base + "db";
  if(!access(db_dir.c_str(), F_OK)) {
    // DB exists -> v3 incremental file format
    XVERBOSE(1, "Bitext: opening file base in persistent incremental update mode: " << base << "\n");
    OpenIncremental(base);
  } else {
    // assume legacy v1/v2 file format
    XVERBOSE(1, "Bitext: opening legacy file base with in-memory update mode: " << base << "\n");
    OpenLegacy(base);
  }
}

void Bitext::AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent, const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) {
  // (1) add to corpus first:

  // order of these three does not matter
  auto isrc = src_->AddToCorpus(srcSent);
  auto itrg = trg_->AddToCorpus(trgSent);
  seq_t fakeSeqNum = isrc + 1; // TODO add real var from API
  // note: TODO: seq_t currently assumes that the first update starts with seq_t seqNum = 1; (seqNum=0 would be ignored, since we init everything there)
  seq_t seqNum = fakeSeqNum;
  assert(isrc == itrg);
  align_->AddSentence(std::vector<AlignmentLink>{alignment.begin(), alignment.end()});

  // (2) indexes: also store seqNum of Corpus

  auto docid = doc_map_->FindOrInsert(domain);
  doc_map_->AddSentence(isrc, docid, seqNum);
  doc_map_->Ack(seqNum);

  // (3) domain-specific first: ensures that domain-specific indexes can provide, since we query the global index for the presence of source phrases first.

  // currently, order of these two does not matter here (we query global index first)
  trg_->AddToDomainIndex(itrg, docid, seqNum);
  src_->AddToDomainIndex(isrc, docid, seqNum);

  // (4) global index last - everything should be stored by the time readers see a new global source index entry

  // target side first: ensures extraction will work
  trg_->index->AddSentence(trg_->corpus->sentence(itrg), seqNum);
  src_->index->AddSentence(src_->corpus->sentence(isrc), seqNum);

  src_->Ack(seqNum);
  trg_->Ack(seqNum);
}

/** Write to (empty) DB and disk. */
void Bitext::Write(const std::string &base) {
  /*
   * Directory layout: base="phrase_tables/bitext.", l1="fr", l2="en"
   *
   * bitext.fr-en.six   word alignment (sentence offsets)
   * bitext.fr-en.mam                  (sequence of alignment pairs)
   * // should be 'bitext.align.trk' in my opinion, but left naming for compatibility reasons ('mmt build' creates these filenames for v1)
   *
   * bitext.db/         RocksDB: vocabulary, token index
   *
   * bitext.docmap.six  mapping sentence ID -> domain ID
   * bitext.docmap.trk
   *
   * bitext.en.six      corpus side 2 (sentence offsets)
   * bitext.en.trk                    (sequence of token IDs)
   *
   * bitext.fr.six      corpus side 1
   * bitext.fr.trk
   */

  std::shared_ptr<BaseDB> db = std::make_shared<BaseDB>(base + "db");
  src_->Write(std::make_shared<DB<SrcToken>>(*db), base);
  trg_->Write(std::make_shared<DB<TrgToken>>(*db), base);
  align_->Write(base + l1_ + "-" + l2_ + ".mam");
  doc_map_->Write(std::make_shared<DB<Domain>>(*db), base + "docmap.trk");
}

void Bitext::SetupLogging(std::shared_ptr<Logger> logger) {
  src_->SetupLogging(logger);
  trg_->SetupLogging(logger);
  doc_map_->SetupLogging(logger);
  Loggable::SetupLogging(logger);
}

} // namespace sto
