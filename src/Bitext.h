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
#include "mmt/IncrementalModel.h"
#include "Loggable.h"

namespace sto {

class BaseDB;

/**
 * For a single language side, holds sto::Corpus, sto::Vocab and sto::TokenIndex.
 */
template<typename Token>
struct BitextSide : public sto::Loggable {
  typedef typename Corpus<Token>::Sid Sid;
  typedef typename Corpus<Token>::Vid Vid;
  typedef std::unordered_map<Domain::Vid, std::shared_ptr<sto::ITokenIndex<Token>>> DomainIndexMap;

  static constexpr domid_t kGlobalDomain = static_cast<domid_t>(-1);

  std::shared_ptr<sto::Corpus<Token>> corpus;

  DomainIndexMap domain_indexes;
  std::string base_and_lang; /** e.g. "phrase_tables/model.en" */
  std::string lang; /** 2-letter language code */
  std::shared_ptr<DB<Token>> db;

  /**
   * Create empty BitextSide in-memory.
   *
   * This is intended for bootstrapping a Bitext from scratch,
   * hence it uses IndexTypeMemBuf for speed and so its indexes are never split, and lazy-sorted in span().
   */
  BitextSide(const std::string &lang);

  /**
   * Load existing BitextSide from DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext." -- we'll store into base+lang
   */
  BitextSide(std::shared_ptr<DB<Token>> db, const std::string &base, const std::string &lang);

  ~BitextSide();

  /** Add sentence to Vocab and Corpus. */
  Sid AddToCorpus(const std::vector<Vid> &sent, domid_t domain, updateid_t version);

  /** Add a sentence to the domain index docid. Sentence should already be added via AddToCorpus(). */
  void AddToDomainIndex(Sid sid, tpt::docid_type docid, updateid_t version);

  /** global index */
  std::shared_ptr<sto::ITokenIndex<Token>> index() { return domain_indexes[kGlobalDomain]; }

  /**
   * Write to (empty) DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  void Write(std::shared_ptr<DB<Token>> db, const std::string &base);

  /** Current persistence sequence number. */
  StreamVersions streamVersions() const;

  size_t numDomains() const { return domain_indexes.size()-1; /* -1 to exclude kGlobalDomain */ }

  class DomainIterator {
  public:
    typedef typename DomainIndexMap::const_iterator citer;

    DomainIterator(const citer domain_indexes_it, const citer domain_indexes_end_it);
    /** skips kGlobalDomain */
    DomainIterator &operator++();
    size_t operator*() const { return it_->first; }
    bool operator!=(const DomainIterator &other) const { return it_ != other.it_; }
  private:
    citer it_;
    citer end_;
  };

  DomainIterator begin() const { return DomainIterator(domain_indexes.begin(), domain_indexes.end()); }
  DomainIterator end() const { return DomainIterator(domain_indexes.end(), domain_indexes.end()); }

  virtual void SetupLogging(std::shared_ptr<Logger> logger) override;
};

/**
 * Collection of word-aligned sentence pairs, which are indexed for fast lookup of phrases.
 *
 * Each sentence pair belongs to a specific 'domain' (called 'document' within moses).
 * We keep both global and domain-specific indexes.
 *
 * Allows updating (appending sentence pairs), which is disk-persistent if the incremental variant was opened.
 *
 *
 * This is a base class implementing the persistence interface.
 * For queries, use class SBitext (in moses).
 */
class Bitext : public virtual mmt::IncrementalModel, public sto::Loggable {
public:
  typedef typename sto::Corpus<sto::SrcToken>::Vid Vid;

  static constexpr domid_t kGlobalDomain = BitextSide<sto::SrcToken>::kGlobalDomain;

  /**
   * Create an empty Bitext in-memory.
   *
   * @param l1  source language 2-letter code
   * @param l2  target language 2-letter code
   */
  Bitext(const std::string &l1, const std::string &l2);

  /**
   * Load existing incremental Bitext from disk, opening it in read/append mode.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   * @param l1    source language 2-letter code
   * @param l2    target language 2-letter code
   */
  Bitext(const std::string &base, const std::string &l1, const std::string &l2);

  virtual ~Bitext();

  /** Opens incremental Bitext in read/append mode. */
  void OpenIncremental(const std::string &base);

  /**
   * Auto-detect the type of Bitext and open it.
   *
   * Opens legacy Bitext in read-only mode.
   * Opens incremental Bitext in read/append mode.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/model." for legacy Bitext. Suggest "phrase_tables/bitext." for incremental Bitext
   */
  virtual void Open(const std::string &base);

  /**
   * Add a word-aligned sentence pair to a specific domain.
   * See mmt::IncrementalModel for detailed interface description.
   */
  virtual void
  Add(const mmt::updateid_t &id, const mmt::domain_t domain,
      const std::vector<mmt::wid_t> &source, const std::vector<mmt::wid_t> &target,
      const mmt::alignment_t &alignment) override;

  /** Get the "update version numbers" of this model. See mmt::IncrementalModel for detailed interface description. */
  virtual std::vector<mmt::updateid_t> GetLatestUpdatesIdentifier() override;

  /**
   * Write to (empty) DB and disk.
   *
   * Useful to load a legacy bitext (which is not updatable) and write it out in the new format.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  virtual void Write(const std::string &base);

  virtual void SetupLogging(std::shared_ptr<Logger> logger) override;

protected:
  std::string l1_; /** source language 2-letter code */
  std::string l2_; /** targetlanguage 2-letter code */
  std::shared_ptr<BaseDB> db_;
  std::shared_ptr<BitextSide<sto::SrcToken>> src_;
  std::shared_ptr<BitextSide<sto::TrgToken>> trg_;
  std::shared_ptr<sto::Corpus<sto::AlignmentLink>> align_;

  /** Current persistence sequence number. */
  StreamVersions streamVersions() const;
};

} // namespace sto

#endif //STO_BITEXT_H
