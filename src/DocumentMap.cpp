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

#include "DocumentMap.h"

namespace sapt {

DocumentMap::DocumentMap() : m_sid2docid(new std::vector<tpt::docid_type>)
{}

IBias *DocumentMap::SetupDocumentBias(std::map<std::string,float> context_weights,
                                     std::ostream* log) const
{
  if(!m_sid2docid)
    throw std::runtime_error("Document bias requested but no document map loaded.");

  if (m_docname2docid.size() == 1)
    // a document bias make no sense if this corpus is single-doc
    return nullptr;

  return new StoBias(context_weights, *this);
}

tpt::docid_type DocumentMap::sid2did(sto::sid_t sid) const {
  // ug_bitext.h copypasta
  if (m_sid2docid)
    return m_sid2docid->at(sid);
  return -1;
}

void DocumentMap::LoadDocumentMap(std::string const& fname, size_t num_sents) {
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
  this->m_sid2docid.reset(new std::vector<tpt::docid_type>(num_sents));
  while(getline(docmap,buffer))
  {
    std::istringstream line(buffer);
    if (!(line>>docname)) continue; // empty line
    if (docname.size() && docname[0] == '#') continue; // comment
    tpt::docid_type docid = static_cast<tpt::docid_type>(this->m_docname2docid.size());
    this->m_docname2docid[docname] = docid;
    this->m_docname.push_back(docname);
    line >> b;
    //VERBOSE(3, "DOCUMENT MAP " << docname << " " << a << "-" << b+a << std::endl);
    for (b += a; a < b; ++a)
      (*this->m_sid2docid)[a] = docid;
  }
  if(b != num_sents)
    throw std::runtime_error(std::string("Document map doesn't match corpus! b=") + std::to_string(b) + ", num_sents=" + std::to_string(num_sents));
}

tpt::docid_type DocumentMap::FindOrInsert(const std::string &docname) {
  auto entry = m_docname2docid.find(docname);
  if(entry != m_docname2docid.end())
    return entry->second;

  // insert new entry
  tpt::docid_type docid = static_cast<tpt::docid_type>(this->m_docname2docid.size());
  m_docname2docid[docname] = docid;
  m_docname.push_back(docname);
  return 0;
}


/** @return true if this doc name is mapped */
bool DocumentMap::contains(const std::string &docname) const {
  return m_docname2docid.find(docname) != m_docname2docid.end();
}

/** look up doc id from doc name, returns -1 on failure */
tpt::docid_type DocumentMap::operator[](const std::string &docname) const {
  auto entry = m_docname2docid.find(docname);
  if(entry != m_docname2docid.end())
    return entry->second;
  else
    return static_cast<tpt::docid_type>(-1);
}

void DocumentMap::AddSentence(sto::sid_t sid, tpt::docid_type docid) {
  m_sid2docid->resize(sid+1);
  (*m_sid2docid)[sid] = docid;
}

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
