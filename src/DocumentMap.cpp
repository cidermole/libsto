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
#include "DB.h"

namespace sto {

DocumentMap::DocumentMap() : sent_info_(new Corpus<SentInfo>())
{}

/** Load existing DocumentMap from DB and disk. */
DocumentMap::DocumentMap(std::shared_ptr<BaseDB> db, const std::string &corpus_file) :
    docname2id_(db->template PrefixedDB<Domain>("dmp")), // add another prefix, so we do not collide with normal Vocab DB entries
    sent_info_(new Corpus<SentInfo>(corpus_file))
{
  StreamVersions corpusVersions = sent_info_stream_versions_();
  StreamVersions vocabVersions = docname2id_.streamVersions();

  // any component with a higher seqNum will have to ignore the repeated updates
  streamVersions_ = StreamVersions::Min(corpusVersions, vocabVersions);
}

sapt::IBias *DocumentMap::SetupDocumentBias(std::map<std::string,float> context_weights,
                                     std::ostream* log) const
{
  throw std::runtime_error("not implemented - construct a StoBias() directly");
  return nullptr;
}

tpt::docid_type DocumentMap::sid2did(sto::sid_t sid) const {
  return sent_info_->begin(sid)->domid;
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
  assert(sent_info_->size() == 0);
  while(getline(docmap,buffer))
  {
    std::istringstream line(buffer);
    if (!(line>>docname)) continue; // empty line
    if (docname.size() && docname[0] == '#') continue; // comment
    tpt::docid_type docid = docname2id_[docname];
    line >> b;
    //VERBOSE(3, "DOCUMENT MAP " << docname << " " << a << "-" << b+a << std::endl);
    for (b += a; a < b; ++a) {
      AddSentence(/* sid = */ a, docid, sto_updateid_t{kLegacyDiskStream, a + 1}); // fake stream -1, fake seqNum
    }
  }
  assert(b == sent_info_->size());
  if(b != num_sents && num_sents != static_cast<size_t>(-1))
    throw std::runtime_error(std::string("Document map doesn't match corpus! b=") + std::to_string(b) + ", num_sents=" + std::to_string(num_sents));

  XVERBOSE(1, "DocumentMap::Load() loaded " << numDomains() << " domains, " << (sent_info_->size()) << " sentences.\n");
}

tpt::docid_type DocumentMap::FindOrInsert(const std::string &docname, sto_updateid_t version) {
  tpt::docid_type id = docname2id_[docname];
  streamVersions_.Update(version);
  return id;
}


/** @return true if this doc name is mapped */
bool DocumentMap::contains(const std::string &docname) const {
  return docname2id_.contains(docname);
}

/** look up doc id from doc name, returns -1 on failure */
tpt::docid_type DocumentMap::operator[](const std::string &docname) const {
  return docname2id_.at(docname);
}

/** look up doc name from doc id, returns "" on failure */
std::string DocumentMap::operator[](tpt::docid_type docid) const {
  return docname2id_.at(docid);
}

sto_updateid_t DocumentMap::version(Corpus<SentInfo>::Sid sid) const {
  const sent_info_t *info = sent_info_->begin(sid);
  return sto_updateid_t{info->stream_id, info->sentence_id};
}

typename DocumentMap::iterator DocumentMap::begin() const {
  return docname2id_.begin();
}

typename DocumentMap::iterator DocumentMap::end() const {
  return docname2id_.end();
}

void DocumentMap::AddSentence(sid_t sid, tpt::docid_type docid, sto_updateid_t version) {
  size_t next_sid = sent_info_->size();
  if(sid > next_sid)
    throw std::runtime_error("DocumentMap::AddSentence() currently only supports sequential addition of sentence IDs.");

  seqid_t sentInfoSeqId = streamVersions_.at(version.stream_id);
  assert(version.sentence_id > sentInfoSeqId);
  if(version.sentence_id <= sentInfoSeqId)
    return;

  // Corpus::AddSentence() flushes by default
  sent_info_->AddSentence(std::vector<SentInfo>{SentInfo{docid, version}});
  streamVersions_[version.stream_id] = version.sentence_id;
  docname2id_.Flush(streamVersions_);
}

/** Write to (empty) DB and disk. */
void DocumentMap::Write(std::shared_ptr<BaseDB> db, const std::string &corpus_file) {
  docname2id_.Write(db->template PrefixedDB<Domain>("dmp"));
  sent_info_->Write(corpus_file);
  docname2id_.Flush(streamVersions_);
}

StreamVersions DocumentMap::sent_info_stream_versions_() {
  typedef typename Corpus<SentInfo>::Sid Sid;
  StreamVersions versions;
  Sid info_size = sent_info_->size();
  for(Sid i = 0; i < info_size; i++) {
    sto_updateid_t upd = version(i);
    if(upd.sentence_id < versions.at(upd.stream_id)) {
      assert(0 && "update IDs should be sequential");
      continue;
    }
    versions[upd.stream_id] = upd.sentence_id;
  }
  return versions;
}

// ----------------------------------------------------------------------------

StoBias::StoBias(std::map<std::string, float> &context_weights, const Corpus<sto::SrcToken> &corpus) : corpus_(corpus)
{
  // lookup domain IDs
  float total = 0;
  for(auto cw : context_weights) {
    bias_[std::stoul(cw.first)] = cw.second;
    total += cw.second;
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
  auto entry = bias_.find(corpus_.info(sid).vid.domid);
  return (entry != bias_.end()) ? entry->second : 0;
}

} // namespace sapt
