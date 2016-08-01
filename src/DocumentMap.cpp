/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cassert>

#include "DocumentMap.h"

namespace sto {

DocumentMap::DocumentMap() : sid2docid_(new Corpus<Domain>(&docname2id_))
{}

/** Load existing DocumentMap from DB and disk. */
DocumentMap::DocumentMap(std::shared_ptr<DB<Domain>> db, const std::string &corpus_file) : docname2id_(db), sid2docid_(new Corpus<Domain>(corpus_file, &docname2id_))
{}

sapt::IBias *DocumentMap::SetupDocumentBias(std::map<std::string,float> context_weights,
                                     std::ostream* log) const
{
  if (docname2id_.size() == 1)
    // a document bias make no sense if this corpus is single-doc
    return nullptr;

  return new StoBias(context_weights, *this);
}

tpt::docid_type DocumentMap::sid2did(sto::sid_t sid) const {
  return *sid2docid_->begin(sid);
}

void DocumentMap::Load(std::string const& fname, size_t num_sents) {
  if(access(fname.c_str(), F_OK))
    return; // file does not exist

  // ug_mm_bitext.h copypasta: load_document_map()

  std::ifstream docmap(fname.c_str());
  // the docmap file should list the documents in the corpus
  // in the order in which they appear with one line per document:
  // <docname> <number of lines / sentences>
  //
  // in the future, we might also allow listing documents with
  // sentence ranges.
  std::string buffer,docname; size_t a=0,b=0;
  assert(docname2id_.size() == 0);
  assert(sid2docid_->size() == 0);
  while(getline(docmap,buffer))
  {
    std::istringstream line(buffer);
    if (!(line>>docname)) continue; // empty line
    if (docname.size() && docname[0] == '#') continue; // comment
    tpt::docid_type docid = docname2id_[docname];
    line >> b;
    //VERBOSE(3, "DOCUMENT MAP " << docname << " " << a << "-" << b+a << std::endl);
    for (b += a; a < b; ++a) {
      AddSentence(a, docid);
    }
  }
  assert(b == sid2docid_->size());
  if(b != num_sents)
    throw std::runtime_error(std::string("Document map doesn't match corpus! b=") + std::to_string(b) + ", num_sents=" + std::to_string(num_sents));
}

tpt::docid_type DocumentMap::FindOrInsert(const std::string &docname) {
  return docname2id_[docname];
}


/** @return true if this doc name is mapped */
bool DocumentMap::contains(const std::string &docname) const {
  return docname2id_.contains(docname);
}

/** look up doc id from doc name, returns -1 on failure */
tpt::docid_type DocumentMap::operator[](const std::string &docname) const {
  return docname2id_.at(docname);
}

void DocumentMap::AddSentence(sto::sid_t sid, tpt::docid_type docid) {
  size_t next_sid = sid2docid_->size();
  if(next_sid != sid)
    throw std::runtime_error("DocumentMap::AddSentence() currently only supports sequential addition of sentence IDs.");
  sid2docid_->AddSentence(std::vector<Domain>{docid});
}

/** Write to (empty) DB and disk. */
void DocumentMap::Write(std::shared_ptr<DB<Domain>> db, const std::string &corpus_file) {
  WriteVocab(db);
  sid2docid_->Write(corpus_file);
}

void DocumentMap::WriteVocab(std::shared_ptr<DB<Domain>> db) {
  Vocab<Domain> target(db); // persistent Vocab with DB backing
  assert(target.size() == 0); // make sure that DB is empty (should not contain a Vocab, so we start from scratch)

  for(auto vid : docname2id_) {
    target[docname2id_.at(vid)]; // insert vids in order
    assert(target.at(vid) == docname2id_.at(vid)); // inserting them in order means the surface forms should be equal as well
  }
}

// ----------------------------------------------------------------------------

StoBias::StoBias(std::map<std::string, float> &context_weights, const DocumentMap &map) : map_(map)
{
  // lookup domain IDs
  float total = 0;
  for(auto cw : context_weights) {
    if(map_.contains(cw.first)) {
      bias_[map_[cw.first]] = cw.second;
      total += cw.second;
    }
  }

  // normalize weights
  if(total != 0.0) {
    for(auto &b : bias_)
      b.second /= total;
  }
}

/**
 * Get a vector of unnormalized domain biases, in descending order of score.
 * Each pair is <float, doc_index>
 */
void StoBias::getRankedBias(std::vector<std::pair<float, tpt::docid_type>>& bias) const {
  bias.clear();
  for(auto it : bias_)
    bias.push_back(std::make_pair(it.second, it.first));
  std::sort(bias.begin(), bias.end(), std::greater<std::pair<float, tpt::docid_type>>()); // sort descending
}

/** sentence bias for sentence ID sid. */
float StoBias::operator[](const tpt::sid_type sid) const {
  auto entry = bias_.find(map_.sid2did(sid));
  return (entry != bias_.end()) ? entry->second : 0;
}

} // namespace sapt
