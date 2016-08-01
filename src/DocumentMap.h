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
#include <cassert>
#include <memory>

#include "Types.h"
#include "ug_bias.h"

namespace sapt {

/**
 * Maps between sentence IDs and domain IDs (domains are called 'documents' in Mmsapt lingo),
 * and domain names and domain IDs.
 */
struct DocumentMap {
  std::vector<std::string> m_docname; /** doc names */
  std::map<std::string, tpt::docid_type>  m_docname2docid; /** maps from doc names to ids */
  std::shared_ptr<std::vector<tpt::docid_type>>   m_sid2docid; /** maps from sentences to docs (ids) */

  DocumentMap();

  /** Create a new DocumentBias object with bias weights for Mmsapt from a map of domain names -> weights. */
  IBias *SetupDocumentBias(std::map<std::string,float> context_weights,
                                       std::ostream* log) const;

  tpt::docid_type sid2did(sto::sid_t sid) const;

  const std::vector<tpt::docid_type>& sid2docids() const { assert(m_sid2docid); return *m_sid2docid; }

  size_t numDomains() const { return m_docname.size(); }

  tpt::docid_type FindOrInsert(const std::string &docname);

  /** @return true if this doc name is mapped */
  bool contains(const std::string &docname) const;

  /** look up doc id from doc name, returns -1 on failure */
  tpt::docid_type operator[](const std::string &docname) const;

  /** add to sentence ID -> document ID mapping */
  void AddSentence(sto::sid_t sid, tpt::docid_type docid);

  /**
   * fname: *.dmp (document map) filename
   * num_sents: number of sentences in corpus
   *
   * example.dmp:  (doc_name num_sents), in corpus order
   *
   * europarl 1954
   * ibm 1717
   * microsoft 1823
   */
  void LoadDocumentMap(std::string const& fname, size_t num_sents);
};

/** Domain bias for BitextSampler, backed by libsto DocumentMap. */
class StoBias : public IBias {
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

} // namespace sapt

#endif //STO_DOCUMENTMAP_H
