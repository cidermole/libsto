/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DOCUMENTMAP_H
#define STO_DOCUMENTMAP_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>

#include "Types.h"
#include "Vocab.h"
#include "Corpus.h"
#include "Loggable.h"
#include "ug_bias.h"

namespace sto {

class BaseDB;

/**
 * Maps between sentence IDs and domain IDs (domains are called 'documents' in Mmsapt lingo),
 * and domain names and domain IDs.
 */
class DocumentMap : public Loggable {
public:
  /** Create an empty DocumentMap. */
  DocumentMap();

  /** Load existing DocumentMap from DB and disk. */
  DocumentMap(std::shared_ptr<BaseDB> db, const std::string &corpus_file);

  /** Create a new DocumentBias object with bias weights for Mmsapt from a map of domain names -> weights. */
  sapt::IBias *SetupDocumentBias(std::map<std::string,float> context_weights,
                                       std::ostream* log) const;

  tpt::docid_type sid2did(sto::sid_t sid) const;

  size_t numDomains() const { return docname2id_.size(); }

  tpt::docid_type FindOrInsert(const std::string &docname);

  /** @return true if this doc name is mapped */
  bool contains(const std::string &docname) const;

  /** look up doc id from doc name, returns -1 on failure */
  tpt::docid_type operator[](const std::string &docname) const;

  /** look up doc name from doc id, returns "" on failure */
  std::string operator[](tpt::docid_type docid) const;

  /** add to sentence ID -> document ID mapping */
  void AddSentence(sto::sid_t sid, tpt::docid_type docid);

  /**
   * Load document map from a v1 sapt .dmp file.
   *
   * fname: *.dmp (document map) filename
   * num_sents: number of sentences in corpus
   *
   * example.dmp:  (doc_name num_sents), in corpus order
   *
   * europarl 1954
   * ibm 1717
   * microsoft 1823
   */
  void Load(std::string const& fname, size_t num_sents);

  /** Write to (empty) DB and disk. */
  void Write(std::shared_ptr<BaseDB> db, const std::string &corpus_file);

  Domain begin() const;
  Domain end() const;

private:
  Vocab<Domain> docname2id_;                  /** document name to document id mapping */
  std::shared_ptr<Corpus<Domain>> sid2docid_; /** sentence id to document id mapping */
};

/** Domain bias for BitextSampler, backed by libsto DocumentMap. */
class StoBias : public sapt::IBias {
public:
  StoBias(std::map<std::string, float> &context_weights, const DocumentMap &map);
  virtual ~StoBias() = default;

  /**
   * Get a vector of unnormalized domain biases, in descending order of score.
   * Each pair is <float, doc_index>
   */
  virtual void getRankedBias(std::vector<std::pair<float, tpt::docid_type>>& bias) const override;

  /** sentence bias for sentence ID sid. */
  virtual float operator[](const tpt::sid_type sid) const override;

private:
  std::unordered_map<tpt::docid_type, float> bias_;
  const DocumentMap &map_;
};

} // namespace sto

#endif //STO_DOCUMENTMAP_H
