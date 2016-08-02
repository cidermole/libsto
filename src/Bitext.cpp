/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unistd.h>

#include "Bitext.h"

namespace sto {


template<typename Token>
BitextSide<Token>::BitextSide() :
    vocab(new sto::Vocab<Token>),
    corpus(new sto::Corpus<Token>(vocab.get())),
    index(new sto::TokenIndex<Token>(*corpus))
{
}

template<typename Token>
BitextSide<Token>::~BitextSide()
{}

template<typename Token>
void BitextSide<Token>::Open(std::string const base_and_lang) {
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
      domain_indexes.push_back(std::make_shared<sto::TokenIndex<Token>>(base_and_lang + "." + map[docid] + ".sfa", *corpus));

    return;
  }

  // create TokenIndex objects
  domain_indexes.clear();
  for(size_t i = 0; i < map.numDomains(); i++)
    domain_indexes.push_back(std::make_shared<sto::TokenIndex<Token>>(*corpus));

  // put each sentence into the correct domain index
  for(size_t i = 0; i < nsents; i++)
    domain_indexes[map.sid2did(i)]->AddSentence(corpus->sentence(i));
}

// explicit template instantiation
template class BitextSide<SrcToken>;
template class BitextSide<TrgToken>;

///////////////////////////////////////////////////////////////////////////////

Bitext::Bitext() : align_(new sto::Corpus<sto::AlignmentLink>)
{}

Bitext::~Bitext()
{}

void Bitext::open(std::string const base, std::string const L1, std::string const L2) {
  XVERBOSE(1, "SBitext: opening file base: " << base << "\n");
  src_.Open(base+L1);
  trg_.Open(base+L2);
  XVERBOSE(2, " sto::Corpus<AlignmentLink>()...\n");
  align_.reset(new Corpus<AlignmentLink>(base+L1+"-"+L2+".mam"));
  doc_map_.Load(base + "dmp", src_.corpus->size());

  // potentially loads global and domain indexes (instead of building them)
  XVERBOSE(1, "SBitext: CreateIndexes()...\n");
  src_.CreateIndexes(doc_map_);
  trg_.CreateIndexes(doc_map_);
  XVERBOSE(1, "SBitext: CreateIndexes() and open() done.\n");
}

void Bitext::AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent, const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) {
  std::vector<SrcToken> src;
  for(auto s : srcSent)
    src.push_back((*src_.vocab)[s]); // vocabulary insert/lookup
  src_.corpus->AddSentence(src);

  std::vector<TrgToken> trg;
  for(auto t : trgSent)
    trg.push_back((*trg_.vocab)[t]); // vocabulary insert/lookup
  trg_.corpus->AddSentence(trg);

  std::vector<AlignmentLink> align;
  for(auto p : alignment)
    align.push_back(AlignmentLink(p.first, p.second));
  align_->AddSentence(align);

  // now for the indexes, domain-specific and otherwise:

  // domain-specific first: ensures that domain-specific indexes can provide, since we query the global index for the presence of source phrases first.
  size_t docid = doc_map_.FindOrInsert(domain);
  size_t isrc = src_.corpus->size()-1, itrg = trg_.corpus->size()-1; assert(isrc == itrg);
  doc_map_.AddSentence(isrc, docid);

  if(docid >= trg_.domain_indexes.size())
    trg_.domain_indexes.push_back(std::make_shared<sto::TokenIndex<TrgToken>>(*trg_.corpus));
  trg_.domain_indexes[docid]->AddSentence(trg_.corpus->sentence(itrg)); // target side first
  if(docid >= src_.domain_indexes.size())
    src_.domain_indexes.push_back(std::make_shared<sto::TokenIndex<SrcToken>>(*src_.corpus));
  src_.domain_indexes[docid]->AddSentence(src_.corpus->sentence(isrc));

  trg_.index->AddSentence(trg_.corpus->sentence(itrg)); // target side first: ensures extraction will work
  src_.index->AddSentence(src_.corpus->sentence(isrc));
}

} // namespace sto
