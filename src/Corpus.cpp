/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cassert>
#include <algorithm>
#include <sstream>

#include "Corpus.h"
#include "Types.h"

namespace sto {

/* Create empty corpus */
template<class Token>
Corpus<Token>::Corpus(const Corpus<Token>::Vocabulary *vocab) : vocab_(vocab), sentIndexEntries_(nullptr), sentIndexEntrySize_(1)
{
  dyn_sentIndex_.push_back(0);
  sentIndexHeader_.idxSize = 0; // no static entries
}

/* Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
template<class Token>
Corpus<Token>::Corpus(const std::string &filename, const Corpus<Token>::Vocabulary *vocab) : vocab_(vocab) {
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

  dyn_sentIndex_.push_back(0);

  // hack for byte counts in word alignment: divide each entry in sentIndexEntries_ by sentIndexEntrySize_
  switch(Token::kIndexType) {
    case CorpusIndexAccounting::IDX_CNT_ENTRIES: // corpus track
      sentIndexEntrySize_ = 1;
      break;
    case CorpusIndexAccounting::IDX_CNT_BYTES: // word alignment
      sentIndexEntrySize_ = sizeof(Token);
      break;
    default:
      throw std::runtime_error("Corpus: unknown idx_type");
  }
}

template<class Token>
const typename Corpus<Token>::Vid* Corpus<Token>::begin(Sid sid) const {
  // static track
  if(sid < sentIndexHeader_.idxSize) {
    // we never provide the trailing sentinel here (note that idxSize excludes it)
    return trackTokens_ + sentIndexEntries_[sid] / sentIndexEntrySize_;
  }

  // dynamic track
  sid -= sentIndexHeader_.idxSize;
  assert(sid < dyn_sentIndex_.size() - 1);
  // we never provide the trailing sentinel here (note that dyn_sentIndex_.size() includes it)
  return dyn_track_.data() + dyn_sentIndex_[sid];
}

template<class Token>
const typename Corpus<Token>::Vid* Corpus<Token>::end(Sid sid) const {
  // static track
  if(sid < sentIndexHeader_.idxSize) {
    // provide the trailing sentinel here (note that idxSize excludes it)
    return trackTokens_ + sentIndexEntries_[sid + 1] / sentIndexEntrySize_;
  }

  // dynamic track
  sid -= sentIndexHeader_.idxSize;
  assert(sid < dyn_sentIndex_.size() - 1);
  // provide the trailing sentinel here (note that dyn_sentIndex_.size() includes it)
  return dyn_track_.data() + dyn_sentIndex_[sid + 1];
}

template<class Token>
Sentence<Token> Corpus<Token>::sentence(Sid sid) const {
  return Sentence<Token>(*this, sid);
}

template<class Token>
void Corpus<Token>::AddSentence(const std::vector<Token> &sent) {
  for(auto token : sent) {
    if(vocab_)
      vocab_->at(token); // access the Token to ensure it is contained in vocabulary (throws exception otherwise)
    dyn_track_.push_back(token.vid);
  }
  dyn_sentIndex_.push_back(static_cast<SentIndexEntry>(dyn_track_.size()));
}

template<class Token>
const typename Corpus<Token>::Vocabulary& Corpus<Token>::vocab() const {
  assert(vocab_);
  return *vocab_;
}

template<class Token>
typename Corpus<Token>::Sid Corpus<Token>::size() const {
  return sentIndexHeader_.idxSize + static_cast<Sid>(dyn_sentIndex_.size() - 1);
}

template<class Token>
size_t Corpus<Token>::numTokens() const {
  // retrieve the trailing sentinel token offset, which gives the total number of corpus tokens
  // (implicit </s> per sentence not stored in track => each sentence takes up exactly its token count in the track)

  // static + dynamic
  return sentIndexEntries_[size()] / sentIndexEntrySize_ + dyn_sentIndex_.back();
}

// explicit template instantiation
template class Corpus<SrcToken>;
template class Corpus<TrgToken>;
template class Corpus<AlignmentLink>;

// --------------------------------------------------------

template<class Token>
Sentence<Token>::Sentence() : corpus_(nullptr), sid_(0), begin_(nullptr), size_(0)
{}

template<class Token>
Sentence<Token>::Sentence(const Corpus<Token> &corpus, Sid sid) : corpus_(&corpus), sid_(sid) {
  begin_ = corpus.begin(sid);
  size_ = corpus.end(sid) - begin_;
}

template<class Token>
Sentence<Token>::Sentence(const Sentence<Token> &o) : corpus_(o.corpus_), sid_(o.sid_), begin_(o.begin_), size_(o.size_)
{}

template<class Token>
Sentence<Token>::Sentence(const Sentence<Token> &&o) : corpus_(o.corpus_), sid_(o.sid_), begin_(o.begin_), size_(o.size_)
{}

template<class Token>
Token Sentence<Token>::operator[](size_t i) const {
  assert(i <= size_);
  if(i == size_)
    return Token{Corpus<Token>::Vocabulary::kEOS}; // implicit </s>
  else
    return Token{begin_[i]};
}

template<class Token>
std::string Sentence<Token>::surface() const {
  std::stringstream ss;
  if(size() > 0)
    ss << corpus_->vocab()[Token{begin_[0]}];
  for(size_t i = 1; i < size(); i++)
    ss << " " << corpus_->vocab()[Token{begin_[i]}];
  return ss.str();
}

// explicit template instantiation
template class Sentence<SrcToken>;
template class Sentence<TrgToken>;
template class Sentence<AlignmentLink>;

// --------------------------------------------------------

template<class Token>
bool Position<Token>::compare(const Position<Token> &other, const Corpus<Token> &corpus) const {
  // should request Sentence object from Corpus instead
  Sentence<Token> sentThis(corpus, sid);
  Sentence<Token> sentOther(corpus, other.sid);

  // this uses Token::operator<(other, this), which sorts by vid (not by surface form)
  return std::lexicographical_compare(sentOther.begin_ + other.offset, sentOther.begin_ + sentOther.size_,
                                      sentThis.begin_ + offset, sentThis.begin_ + sentThis.size_);
}

template<class Token>
bool Position<Token>::operator==(const Position& other) const {
  return this->sid == other.sid && this->offset == other.offset;
}

template<class Token>
std::string Position<Token>::surface(const Corpus<Token> &corpus) const {
  return corpus.vocab()[corpus.sentence(sid)[offset]];
}

template<class Token>
typename Position<Token>::Vid Position<Token>::vid(const Corpus<Token> &corpus) const {
  return corpus.sentence(sid)[offset].vid;
}

template<class Token>
Token Position<Token>::token(const Corpus<Token> &corpus) const {
  return Token{corpus.sentence(sid)[offset].vid};
}

template<class Token>
Position<Token> Position<Token>::add(Offset offset, const Corpus<Token> &corpus) const {
  assert(static_cast<Offset>(corpus.sentence(this->sid).size()) + 1 - this->offset >= offset + 1);
  return Position(this->sid, this->offset + offset);
}

// explicit template instantiation
template class Position<SrcToken>;
template class Position<TrgToken>;

} // namespace sto
