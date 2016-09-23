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
BitextSide<Token>::BitextSide(const std::string &l) :
    corpus(new sto::Corpus<Token>()),
    lang(l)
{
  domain_indexes[kGlobalDomain] = std::make_shared<sto::TokenIndex<Token, IndexTypeMemBuf>>(*corpus, /* maxLeafSize = */ -1);
}

/** Load existing BitextSide from DB and disk. */
template<typename Token>
BitextSide<Token>::BitextSide(std::shared_ptr<DB<Token>> db, const std::string &base, const std::string &lang) :
    corpus(new sto::Corpus<Token>(base + lang + ".trk")),
    lang(lang),
    db(db)
{
  // load global and domain indexes (global has docid==kGlobalDomain)
  std::set<domid_t> domains = db->GetIndexedDomains(lang);
  for(auto docid : domains)
    domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeDisk>>(/* filename = */ "", *corpus, db->template PrefixedDB<Token>(lang, docid));
}

template<typename Token>
BitextSide<Token>::~BitextSide()
{}

template<typename Token>
typename BitextSide<Token>::Sid BitextSide<Token>::AddToCorpus(const std::vector<BitextSide<Token>::Vid> &sent, domid_t domain, updateid_t version) {
  std::vector<Token> toks;
  for(auto t : sent)
    toks.push_back(t);
  return corpus->AddSentenceIncremental(toks, SentInfo{domain, version});
}

template<typename Token>
void BitextSide<Token>::AddToDomainIndex(Sid sid, tpt::docid_type docid, updateid_t version) {
  if(domain_indexes.find(docid) == domain_indexes.end()) {
    if(db)
      // persisted in DB
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeDisk>>(/* filename = */ "", *corpus, db->template PrefixedDB<Token>(lang, docid));
    else
      // memory only
      domain_indexes[docid] = std::make_shared<sto::TokenIndex<Token, IndexTypeMemBuf>>(*corpus, -1);
    domain_indexes[docid]->SetupLogging(this->logger_);
  }
  domain_indexes[docid]->AddSentence(corpus->sentence(sid), version);
}

template<typename Token>
void BitextSide<Token>::Write(std::shared_ptr<DB<Token>> db, const std::string &base) {
  std::cerr << "BitextSide<Token>::Write() of lang=" << lang << "..." << std::endl;

  benchmark_time([&](){ corpus->Write(base + lang + ".trk"); }, "corpus->Write()");

  benchmark_time([&](){
    for(auto& d : domain_indexes)
      d.second->Write(db->template PrefixedDB<Token>(lang, d.first));
  }, "domain_indexes->Write()");

  std::cerr << "BitextSide<Token>::Write() done." << std::endl;
}

template<typename Token>
StreamVersions BitextSide<Token>::streamVersions() const {
  // Our seqNum = min({c.seqNum | c in components})

  StreamVersions versions = StreamVersions::Max();

  versions = StreamVersions::Min(versions, corpus->streamVersions());

  for(auto& d : domain_indexes)
    versions = StreamVersions::Min(versions, d.second->streamVersions());

  // any component with a higher seqNum will have to ignore the repeated updates
  return versions;
}

template<typename Token>
BitextSide<Token>::DomainIterator::DomainIterator(const citer domain_indexes_it, const citer domain_indexes_end_it)
    : it_(domain_indexes_it), end_(domain_indexes_end_it)
{
  if(it_ != end_ && it_->first == kGlobalDomain)
    ++it_;
}

template<typename Token>
typename BitextSide<Token>::DomainIterator &BitextSide<Token>::DomainIterator::operator++() {
  ++it_;
  // skip kGlobalDomain
  if(it_ != end_ && it_->first == kGlobalDomain)
    ++it_;
  return *this;
}

template<typename Token>
void BitextSide<Token>::SetupLogging(std::shared_ptr<Logger> logger) {
  corpus->SetupLogging(logger);
  for(auto& d : domain_indexes)
    d.second->SetupLogging(logger);
  Loggable::SetupLogging(logger);
}

// explicit template instantiation
template class BitextSide<SrcToken>;
template class BitextSide<TrgToken>;

///////////////////////////////////////////////////////////////////////////////

Bitext::Bitext(const std::string &l1, const std::string &l2) :
    l1_(l1), l2_(l2),
    src_(new BitextSide<sto::SrcToken>(l1)),
    trg_(new BitextSide<sto::TrgToken>(l2)),
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
  src_.reset(new BitextSide<sto::SrcToken>(std::make_shared<DB<SrcToken>>(*db_), base, l1_));
  trg_.reset(new BitextSide<sto::TrgToken>(std::make_shared<DB<TrgToken>>(*db_), base, l2_));
  align_.reset(new sto::Corpus<sto::AlignmentLink>(base + l1_ + "-" + l2_ + ".mam"));

  XVERBOSE(2, "Bitext: src global index size=" << src_->domain_indexes[kGlobalDomain]->span().size() << "\n");
  XVERBOSE(2, "Bitext: trg global index size=" << trg_->domain_indexes[kGlobalDomain]->span().size() << "\n");
}

void Bitext::Open(const std::string &base) {
  std::string db_dir = base + "db";
  if(!access(db_dir.c_str(), F_OK)) {
    // DB exists -> v3 incremental file format
    XVERBOSE(1, "Bitext: opening file base in persistent incremental update mode: " << base << "\n");
    OpenIncremental(base);
  } else {
    // assume legacy v1/v2 file format
    throw std::runtime_error("no support for legacy v1/v2 Bitext anymore.");
  }
}

std::vector<mmt::updateid_t> Bitext::GetLatestUpdatesIdentifier() {
  // convert StreamVersions -> vector<mmt::updateid_t>
  StreamVersions versions = streamVersions();
  std::vector<mmt::updateid_t> updateids;
  for(auto stream : versions)
    updateids.push_back(mmt::updateid_t{stream.first, stream.second});
  return updateids;
}

void
Bitext::Add(const mmt::updateid_t &version, const mmt::domain_t domain,
            const std::vector<mmt::wid_t> &srcSent, const std::vector<mmt::wid_t> &trgSent,
            const mmt::alignment_t &alignment)
{
  XVERBOSE(2, "Bitext::Add() of updateid_t{"
      << static_cast<uint32_t>(version.stream_id) << ","
      << static_cast<uint64_t>(version.sentence_id)
      << "}\n");

  // we assume that the first update starts with sentence_id = 1
  // (sentence_id=0 would be ignored, since we init everything there)

  // (1) add to corpus first:
  XVERBOSE(2, "Bitext::Add() - Corpus\n");

  // order of these three does not matter
  auto isrc = src_->AddToCorpus(srcSent, domain, version);
  auto itrg = trg_->AddToCorpus(trgSent, domain, version);
  assert(isrc == itrg);
  align_->AddSentence(std::vector<AlignmentLink>{alignment.begin(), alignment.end()}, SentInfo{domain, version});

  // (3) domain-specific first: ensures that domain-specific indexes can provide, since we query the global index for the presence of source phrases first.
  XVERBOSE(2, "Bitext::Add() - AddToDomainIndex(" << domain << ")\n");

  // currently, order of these two does not matter here (we query global index first)
  trg_->AddToDomainIndex(itrg, domain, version);
  src_->AddToDomainIndex(isrc, domain, version);

  // (4) global index last - everything should be stored by the time readers see a new global source index entry
  XVERBOSE(2, "Bitext::Add() - AddToDomainIndex(kGlobalDomain)\n");

  // target side first: ensures extraction will work
  trg_->AddToDomainIndex(itrg, kGlobalDomain, version);
  src_->AddToDomainIndex(isrc, kGlobalDomain, version);
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
   * bitext.db/         RocksDB: token index
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
}

void Bitext::SetupLogging(std::shared_ptr<Logger> logger) {
  src_->SetupLogging(logger);
  trg_->SetupLogging(logger);
  Loggable::SetupLogging(logger);
}

StreamVersions Bitext::streamVersions() const {
  // Our seqNum = min({c.seqNum | c in components})
  // any component with a higher seqNum will have to ignore the repeated updates
  StreamVersions versions = StreamVersions::Max();

  versions = StreamVersions::Min(versions, src_->streamVersions());
  versions = StreamVersions::Min(versions, trg_->streamVersions());
  versions = StreamVersions::Min(versions, align_->streamVersions());

  return versions;
}


template<class Token> constexpr domid_t BitextSide<Token>::kGlobalDomain;

constexpr domid_t Bitext::kGlobalDomain;

} // namespace sto
