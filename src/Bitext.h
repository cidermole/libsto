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

#include "TokenIndex.h"
#include "DocumentMap.h"
#include "IncrementalBitext.h"
#include "Loggable.h"

namespace sto {



/**
 * For a single language side, holds sto::Corpus, sto::Vocab and sto::TokenIndex.
 */
template<typename Token>
struct BitextSide : public sto::Loggable {
  std::shared_ptr<sto::Vocab<Token>> vocab;
  std::shared_ptr<sto::Corpus<Token>> corpus;
  std::shared_ptr<sto::TokenIndex<Token>> index;

  std::vector<std::shared_ptr<sto::TokenIndex<Token>>> domain_indexes;
  std::string base_and_lang;

  /** Construct empty corpus side */
  BitextSide();
  ~BitextSide();

  /** Load vocabulary and corpus track */
  void Open(std::string const base_and_lang);

  void CreateIndexes(const DocumentMap &map) { CreateGlobalIndex(); CreateDomainIndexes(map); }

  /**
   * Create the global index.
   * * if present, load it from disk
   * * otherwise, index the entire corpus
   */
  void CreateGlobalIndex();

  /**
   * Create the domain-specific indexes.
   * * if present, load them from disk
   * * otherwise, index the entire corpus, putting each sentence into the correct index
   */
  void CreateDomainIndexes(const DocumentMap &map);
};

/**
 * Incrementally updatable collection of libsto objects.
 */
class Bitext : public virtual sto::IncrementalBitext, public sto::Loggable {
public:
  Bitext();
  virtual ~Bitext();

  virtual void open(std::string const base, std::string const L1, std::string const L2) override;

  virtual void AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent, const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) override;

protected:
  BitextSide<sto::SrcToken> src_;
  BitextSide<sto::TrgToken> trg_;

  std::shared_ptr<sto::Corpus<sto::AlignmentLink>> align_;

  DocumentMap doc_map_; /** housekeeping for individual domain names, IDs */
};

} // namespace sto

#endif //STO_BITEXT_H
