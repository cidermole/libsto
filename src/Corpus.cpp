/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "Corpus.h"
#include "Types.h"

namespace sto {

/* Create empty corpus */
template<class Token>
Corpus<Token>::Corpus() : vocab_(nullptr)
{}

/* Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
template<class Token>
Corpus<Token>::Corpus(const std::string &filename, const Vocab<Token> &vocab) : vocab_(&vocab) {
  track_.reset(new MappedFile(filename));
  CorpusTrackHeader &header = *reinterpret_cast<CorpusTrackHeader*>(track_->ptr);
  trackHeader_ = header;

  // read and interpret file header(s), find sentence index
  if(header.versionMagic == tpt::INDEX_V2_MAGIC) {
    // legacy v2 corpus, with concatenated track and sentence index
    sentIndex_.reset(new MappedFile(filename, header.legacy_startIdx)); // this maps some memory twice, because we leave the index as mapped in track_, but it's shared mem anyway.
    sentIndexHeader_.versionMagic = header.versionMagic;
    sentIndexHeader_.idxSize = header.legacy_idxSize;
  } else if(header.versionMagic == tpt::INDEX_V3_MAGIC) {
    // there is a separate sentence index file
    std::string prefix = filename.substr(0, filename.find(".trk"));
    sentIndex_.reset(new MappedFile(prefix + ".six", header.legacy_startIdx));
    SentIndexHeader &idxHeader = *reinterpret_cast<SentIndexHeader*>(sentIndex_->ptr);
    sentIndexHeader_ = idxHeader;
    sentIndex_->ptr += sizeof(SentIndexHeader);
  } else {
    throw std::runtime_error(std::string("unknown version magic in ") + filename);
  }
  // maybe it would be nicer if the headers read themselves, without mmap usage.
}

// explicit template instantiation
template class Corpus<SrcToken>;
template class Corpus<TrgToken>;

} // namespace sto
