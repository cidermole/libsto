/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_BITEXT_H
#define STO_BITEXT_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "TokenIndex.h"
#include "DocumentMap.h"
#include "IncrementalBitext.h"
#include "Loggable.h"

namespace sto {

class BaseDB;

/**
 * For a single language side, holds sto::Corpus, sto::Vocab and sto::TokenIndex.
 */
template<typename Token>
struct BitextSide : public sto::Loggable {
  typedef typename Corpus<Token>::Sid Sid;

  std::shared_ptr<sto::Vocab<Token>> vocab;
  std::shared_ptr<sto::Corpus<Token>> corpus;
  std::shared_ptr<sto::ITokenIndex<Token>> index;

  std::unordered_map<Domain::Vid, std::shared_ptr<sto::ITokenIndex<Token>>> domain_indexes;
  std::string base_and_lang; /** e.g. "phrase_tables/model.en" */
  std::string lang; /** 2-letter language code */
  std::shared_ptr<DB<Token>> db;

  /** Create empty BitextSide */
  BitextSide(const std::string &lang);

  /**
   * Load existing BitextSide from DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext." -- we'll store into base+lang
   */
  BitextSide(std::shared_ptr<DB<Token>> db, const std::string &base, const std::string &lang, const DocumentMap &map);

  ~BitextSide();

  /** Load v1/v2 vocabulary and corpus track. Does not index or load indexes. */
  void Open(const std::string &base, const std::string &lang);

  void CreateIndexes(const DocumentMap &map) { CreateGlobalIndex(); CreateDomainIndexes(map); }

  /**
   * Create the global index.
   * * if present, load it from disk (old v1/v2 format)
   * * otherwise, index the entire corpus
   */
  void CreateGlobalIndex();

  /**
   * Create the domain-specific indexes.
   * * if present, load them from disk (old v1/v2 format)
   * * otherwise, index the entire corpus, putting each sentence into the correct index
   */
  void CreateDomainIndexes(const DocumentMap &map);

  /** Add sentence to Vocab and Corpus. */
  Sid AddToCorpus(const std::vector<std::string> &sent);

  /** Add a sentence to the domain index docid. Sentence should already be added via AddToCorpus(). */
  void AddToDomainIndex(Sid sid, tpt::docid_type docid);

  /**
   * Write to (empty) DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  void Write(std::shared_ptr<DB<Token>> db, const std::string &base, const DocumentMap &map);
};

/**
 * Incrementally updatable Bitext - aggregation of backing libsto objects.
 *
 * This is a base class implementing the persistence interface IncrementalBitext.
 * For queries, use class SBitext (in moses).
 */
class Bitext : public virtual sto::IncrementalBitext, public sto::Loggable {
public:
  /** Create an empty Bitext. */
  Bitext(const std::string &l1, const std::string &l2);

  /**
   * Load existing Bitext from DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  Bitext(const std::string &base, const std::string &l1, const std::string &l2);

  virtual ~Bitext();


  virtual void Open(const std::string &base);

  // to do: change IncrementalBitext interface (add (l1, l2) constructor), and remove this method
  virtual void open(std::string const base, std::string const L1, std::string const L2) override { Open(base); }

  virtual void AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent, const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) override;

  /**
   * Write to (empty) DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  virtual void Write(const std::string &base) override;

  virtual void SetupLogging(std::shared_ptr<Logger> logger) override;

protected:
  std::string l1_; /** source language 2-letter code */
  std::string l2_; /** targetlanguage 2-letter code */
  std::shared_ptr<BaseDB> db_;
  DocumentMap doc_map_; /** housekeeping for individual domain names, IDs */
  BitextSide<sto::SrcToken> src_;
  BitextSide<sto::TrgToken> trg_;
  std::shared_ptr<sto::Corpus<sto::AlignmentLink>> align_;
};

} // namespace sto

#endif //STO_BITEXT_H
