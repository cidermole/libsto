/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cassert>
#include <algorithm>
#include "Corpus.h"
#include "Types.h"

namespace sto {

/* Create empty corpus */
template<class Token>
Corpus<Token>::Corpus() : vocab_(nullptr), sentIndexEntries_(nullptr)
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
  trackTokens_ = reinterpret_cast<Vid*>(track_->ptr + sizeof(CorpusTrackHeader));
  sentIndexEntries_ = reinterpret_cast<SentIndexEntry*>(sentIndex_->ptr);
  // maybe it would be nicer if the headers read themselves, without mmap usage.
}

template<class Token>
typename Corpus<Token>::Vid* Corpus<Token>::begin(Sid sid) {
  assert(sid < sentIndexHeader_.idxSize + 1); // idxSize excludes the trailing sentinel
  return trackTokens_ + sentIndexEntries_[sid];
}

// explicit template instantiation
template class Corpus<SrcToken>;
template class Corpus<TrgToken>;

// --------------------------------------------------------

template<class Token>
Sentence<Token>::Sentence(Corpus<Token> &corpus, Sid sid) : corpus_(&corpus), sid_(sid) {
  begin_ = corpus.begin(sid);
  size_ = corpus.begin(sid + 1) - begin_;
}

template<class Token>
Token Sentence<Token>::operator[](size_t i) const {
  assert(i < size_);
  return Token{begin_[i]};
}

// explicit template instantiation
template class Sentence<SrcToken>;
template class Sentence<TrgToken>;

// --------------------------------------------------------

template<class Token>
Position<Token>::Position(Corpus<Token> &corpus, Sid sid, Offset offset) : corpus_(&corpus), sid_(sid), offset_(offset)
{}

template<class Token>
bool Position<Token>::operator<(const Position<Token> &other) {
  assert(corpus_ == other.corpus_);

  Sentence<Token> sentThis(*corpus_, sid_);
  Sentence<Token> sentOther(*corpus_, other.sid_);

  // this uses Token::operator<(), which sorts by vid (not by surface form)
  return std::lexicographical_compare(sentThis.begin_, sentThis.begin_ + sentThis.size_,
                                      sentOther.begin_, sentOther.begin_ + sentOther.size_);
}

// explicit template instantiation
template class Position<SrcToken>;
template class Position<TrgToken>;

} // namespace sto
