/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cassert>
#include <algorithm>
#include <sstream>

#include <boost/filesystem.hpp>
#include <unistd.h>

#include "Corpus.h"
#include "Types.h"


namespace sto {

/* Create empty corpus */
template<class Token>
Corpus<Token>::Corpus(const Corpus<Token>::Vocabulary *vocab) : vocab_(vocab), trackTokens_(nullptr), sentIndexEntries_(nullptr), sentIndexEntrySize_(1), writable_(false)
{
  dyn_sentIndex_.push_back(0);
  sentIndexHeader_.idxSize = 0; // no static entries

  init_index_type();
}

/* Load corpus from mtt-build .mtt format or from split corpus/sentidx. */
template<class Token>
Corpus<Token>::Corpus(const std::string &filename, const Corpus<Token>::Vocabulary *vocab) : vocab_(vocab), writable_(false), track_filename_(filename) {
  track_.reset(new MappedFile(filename, /* offset = */ 0, O_RDWR));
  CorpusTrackHeader &header = *reinterpret_cast<CorpusTrackHeader *>(track_->ptr);
  trackHeader_ = header;

  static_assert(sizeof(SentInfo) % sizeof(Vid) == 0, "data structure alignment of SentInfo header for corpus track does not match Vid alignment");

  // read and interpret file header(s), find sentence index
  if (header.versionMagic == tpt::INDEX_V2_MAGIC) {
    // legacy v2 corpus, with concatenated track and sentence index
    sentIndex_.reset(new MappedFile(filename,
                                    header.legacy_startIdx)); // this maps some memory twice, because we leave the index as mapped in track_, but it's shared mem anyway.
    sentIndexHeader_.versionMagic = header.versionMagic;
    sentIndexHeader_.idxSize = header.legacy_idxSize;
  } else if (header.versionMagic == tpt::CORPUS_V31_MAGIC) {
    // there is a separate sentence index file
    std::string prefix = filename.substr(0, filename.find_last_of('.'));
    sentIndex_.reset(new MappedFile(prefix + ".six", /* offset = */ 0, O_RDWR));
    SentIndexHeader &idxHeader = *reinterpret_cast<SentIndexHeader *>(sentIndex_->ptr);
    sentIndexHeader_ = idxHeader;
    sentIndex_->ptr += sizeof(SentIndexHeader);
    writable_ = true;
    ftrack_ = fdopen(track_->fd(), "wb+");
    findex_ = fdopen(sentIndex_->fd(), "wb+");
  } else {
    throw std::runtime_error(std::string("unknown version magic in ") + filename);
  }
  trackTokens_ = reinterpret_cast<Vid *>(track_->ptr + sizeof(CorpusTrackHeader));
  sentIndexEntries_ = reinterpret_cast<SentIndexEntry *>(sentIndex_->ptr);
  // maybe it would be nicer if the headers read themselves, without mmap usage.

  dyn_sentIndex_.push_back(0);

  init_index_type();

  // TODO: debug only
  if(header.versionMagic == tpt::CORPUS_V31_MAGIC)
    DebugVerifyConsistency();

  streamVersions_ = ComputeStreamVersions();
}

template<class Token>
void Corpus<Token>::DebugVerifyConsistency() {
  // check that the amount of tokens referenced by the index file .six is really there

  const Vid *b = trackTokens_; // including SentInfo
  const Vid *e = size() != 0 ? end(size()-1) : b;

  size_t size_vid = e - b; // track itself should be this size, in number of Vids
  size_t size_bytes = sizeof(CorpusTrackHeader) + size_vid * sizeof(Vid); // index says track file should be this size

  if(track_->size() == size_bytes)
    return; // everything OK


  // XVERBOSE never arrives because Loggable isn't set up at the time the ctor runs
  std::cerr << "DebugVerifyConsistency(): .trk should be " << size_bytes << " bytes, but is " << track_->size() << " bytes\n";

  if(track_->size() > size_bytes)
    return;

  size_t nsents_est;
  for(nsents_est = size(); nsents_est != (size_t)-1; nsents_est--) {
    e = end(nsents_est-1);
    size_vid = e - b;
    size_bytes = sizeof(CorpusTrackHeader) + size_vid * sizeof(Vid); // index says track file should be this size
    if(size_bytes < track_->size())
      break;
  }

  std::cerr << "DebugVerifyConsistency(): .trk size roughly equals " << nsents_est << " sentences stored.\n";

  std::cerr << "Printing last 2k sto_updateid_t:\n";

  for(size_t i = nsents_est - 2001; i < nsents_est; i++) {
    auto inf = info(i).vid;
    std::cerr << "sid=" << i << " sto_updateid_t{" << static_cast<uint32_t>(inf.stream_id) << ", " << inf.sentence_id << "}\n" << std::endl;
  }
}

template<class Token>
void Corpus<Token>::init_index_type() {
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
Corpus<Token>::~Corpus() {
  if(ftrack_)
    fclose(ftrack_);
  if(findex_)
    fclose(findex_);
}

template<class Token>
const typename Corpus<Token>::Vid* Corpus<Token>::begin(Sid sid) const {
  return track_pos_(sid, 0);
}

template<class Token>
const typename Corpus<Token>::Vid* Corpus<Token>::end(Sid sid) const {
  return track_pos_(sid, 1);
}

template<class Token>
const typename Corpus<Token>::Vid* Corpus<Token>::track_pos_(Sid sid, Sid is_end) const {
  // static track
  if(sid < sentIndexHeader_.idxSize) {
    // provide the trailing sentinel here (note that idxSize excludes it)
    return trackTokens_
           + (sid + 1) * kSentInfoSizeToks // add an offset for SentInfo
           + sentIndexEntries_[sid + is_end] / sentIndexEntrySize_; // number of Vid tokens
  }

  // dynamic track
  sid -= sentIndexHeader_.idxSize;
  assert(sid < dyn_sentIndex_.size() - 1);
  // end() provides the trailing sentinel here (note that dyn_sentIndex_.size() includes it)
  return dyn_track_.data() + dyn_sentIndex_[sid + is_end];
}

template<class Token>
Sentence<Token> Corpus<Token>::sentence(Sid sid) const {
  return Sentence<Token>(*this, sid);
}

template<class Token>
SentInfo Corpus<Token>::info(Sid sid) const {
  const SentInfo *sentInfo = nullptr;

  if(sid < sentIndexHeader_.idxSize) {
    // static track: SentInfo is stored in the corpus track before the first token
    sentInfo = reinterpret_cast<const SentInfo *>(begin(sid) - kSentInfoSizeToks);
  } else {
    // dynamic track
    sid -= sentIndexHeader_.idxSize;
    assert(sid < dyn_sentIndex_.size() - 1);
    sentInfo = &dyn_track_info_[sid];
  }

  return *sentInfo;
}

template<class Token>
typename Corpus<Token>::Sid Corpus<Token>::AddSentence(const std::vector<Token> &sent, SentInfo info) {
  for(auto token : sent) {
    if(vocab_)
      vocab_->at(token); // access the Token to ensure it is contained in vocabulary (throws exception otherwise)
    dyn_track_.push_back(token.vid);
  }
  dyn_track_info_.push_back(info);
  dyn_sentIndex_.push_back(static_cast<SentIndexEntry>(dyn_track_.size()));

  if(writable_) {
    XVERBOSE(2, "Corpus::AddSentence() -> WriteSentence()\n");
    WriteSentence();
  } else {
    XVERBOSE(2, "Corpus::AddSentence(), but Corpus is not writable\n");
  }
  return size()-1;
}

template<class Token>
typename Corpus<Token>::Sid Corpus<Token>::AddSentenceIncremental(const std::vector<Token> &sent, SentInfo sentInfo) {
  if(!streamVersions_.Update(sentInfo.vid)) {
    // ignore update if too old

    XVERBOSE(1, "Corpus::AddSentenceIncremental() ignoring sto_updateid_t{"
        << static_cast<uint32_t>(sentInfo.vid.stream_id) << ","
        << static_cast<uint64_t>(sentInfo.vid.sentence_id)
        << "} because in that stream we are already at " << streamVersions_[sentInfo.vid.stream_id] << "\n");

    // find old Sid: search backwards to the sentence added in an update before
    for(Sid i = size()-1; i != static_cast<Sid>(-1); i--)
      if(info(i).vid.stream_id == sentInfo.vid.stream_id && info(i).vid.sentence_id == sentInfo.vid.sentence_id)
        return i;

    // since this code only runs on startup, it is probably not unreasonable to crash there if we are inconsistent
    throw std::runtime_error("Corpus inconsistent: sto_updateid_t has been applied, but cannot find it");
  }

  return AddSentence(sent, sentInfo);
}

/** write out the entire corpus in v3 format */
template<class Token>
void Corpus<Token>::Write(const std::string &filename) {
  std::string prefix = filename.substr(0, filename.find_last_of('.'));
  std::string indexfile = prefix + ".six";

  FILE *track = fopen(filename.c_str(), "wb");
  if(!track)
    throw std::runtime_error(std::string("failed to open ") + filename + " for writing");

  FILE *index = fopen(indexfile.c_str(), "wb");
  if(!index) {
    fclose(track);
    throw std::runtime_error(std::string("failed to open ") + filename + " for writing");
  }

  CorpusTrackHeader trkHeader;
  if(fwrite(&trkHeader, sizeof(CorpusTrackHeader), 1, track) != 1)
    throw std::runtime_error("Corpus: fwrite() failed");
  if(sentIndexHeader_.idxSize) {
    // we have a static index
    size_t idx_size = sentIndexEntries_[sentIndexHeader_.idxSize] / sentIndexEntrySize_;
    if(fwrite(begin(0), sizeof(Vid), idx_size, track) != idx_size)
      throw std::runtime_error("Corpus: fwrite() failed");
  }
  for(size_t i = 0; i < dyn_sentIndex_.size()-1; i++) {
    if(fwrite(dyn_track_info_.data() + i, sizeof(SentInfo), 1, track) != 1)
      throw std::runtime_error("Corpus: fwrite() failed");

    size_t tokens = dyn_sentIndex_[i+1] - dyn_sentIndex_[i];
    if(fwrite(dyn_track_.data() + dyn_sentIndex_[i], sizeof(Vid), tokens, track) != tokens)
      throw std::runtime_error("Corpus: fwrite() failed");
  }

  fclose(track);


  SentIndexHeader idxHeader;
  size_t idx_static = sentIndexHeader_.idxSize ? sentIndexHeader_.idxSize : 0; // excluding trailing sentinel
  size_t idx_nentries = idx_static + dyn_sentIndex_.size() - 1; // -1: remove trailing sentinel explicitly included in dyn_sentIndex_
  idxHeader.idxSize = static_cast<uint32_t>(idx_nentries);

  if(fwrite(&idxHeader, sizeof(SentIndexHeader), 1, index) != 1)
    throw std::runtime_error("Corpus: fwrite() failed");

  size_t dyn_offset = 0;
  if(sentIndexHeader_.idxSize) {
    // we have a static index
    auto *end = sentIndexEntries_ + sentIndexHeader_.idxSize;
    for(auto *p = sentIndexEntries_; p != end; p++) {
      auto e = *p;
      //e *= sentIndexEntrySize_; // compute the size format for disk. NO! is mmapped from disk, so it's already the right format. We just save the same format.
      if(fwrite(&e, sizeof(SentIndexEntry), 1, index) != 1)
        throw std::runtime_error("Corpus: fwrite() failed");
    }
    // trailing sentinel is written below.
    size_t idx_size = sentIndexEntries_[sentIndexHeader_.idxSize] / sentIndexEntrySize_;
    dyn_offset = idx_size;
  }
  for(auto e : dyn_sentIndex_) {
    e += dyn_offset; // add static index size
    e *= sentIndexEntrySize_; // compute the size format for disk
    if(fwrite(&e, sizeof(SentIndexEntry), 1, index) != 1)
      throw std::runtime_error("Corpus: fwrite() failed");
  }

  fclose(index);
}

template<class Token>
void Corpus<Token>::WriteSentence() {
  assert(writable_);

  /*
   * Write track first, and index afterwards. Index holds the sentence count and offsets,
   * so if we crash in-between we will later read only the valid, completed sentences.
   * The next append would then overwrite the partial sentence from the last valid position.
   */

  // append to track

  // end at the time of first Corpus construction, in tokens
  size_t static_ntoks = sentIndexEntries_[sentIndexHeader_.idxSize] / sentIndexEntrySize_;
  size_t dyn_isent = dyn_sentIndex_.size()-2;
  size_t dyn_ntoks_before = dyn_sentIndex_[dyn_isent]; // size (in tokens) of dynamically added tokens, already written
  size_t dyn_ntoks_after = dyn_sentIndex_[dyn_isent+1];
  if(fseek(ftrack_, sizeof(CorpusTrackHeader) + (static_ntoks + sentIndexHeader_.idxSize * kSentInfoSizeToks + dyn_ntoks_before + dyn_isent * kSentInfoSizeToks) * sizeof(Vid), SEEK_SET))
    throw std::runtime_error("Corpus: fseek() failed on track");

  if(fwrite(&dyn_track_info_[dyn_isent], sizeof(SentInfo), 1, ftrack_) != 1)
    throw std::runtime_error("Corpus: fwrite() failed on track");

  size_t ntoks = dyn_ntoks_after - dyn_ntoks_before;
  if(fwrite(&dyn_track_[dyn_ntoks_before], sizeof(Vid), ntoks, ftrack_) != ntoks)
    throw std::runtime_error("Corpus: fwrite() failed on track");

  // enforce order: track write must be completed
  fflush(ftrack_);
  fsync(track_->fd());

  // update index: append first, then update counts in header
  SentIndexEntry entry = static_cast<SentIndexEntry>((static_ntoks + dyn_ntoks_after) * sentIndexEntrySize_);
  size_t idx_nentries_before = sentIndexHeader_.idxSize + 1 + dyn_sentIndex_.size() - 1 - 1; // in entries. trailing sentinels balance out (excluded in static, included in dynamic)
  if(fseek(findex_, sizeof(SentIndexHeader) + idx_nentries_before * sizeof(SentIndexEntry), SEEK_SET))
    throw std::runtime_error("Corpus: fseek() failed on index");
  if(fwrite(&entry, sizeof(SentIndexEntry), 1, findex_) != 1)
    throw std::runtime_error("Corpus: fwrite() failed on index");

  // only update header on disk, not in memory (since mmap still contains the old number of sentences)
  SentIndexHeader header = sentIndexHeader_;
  header.idxSize = static_cast<uint32_t>(idx_nentries_before);
  fseek(findex_, 0, SEEK_SET);
  if(fwrite(&header, sizeof(header), 1, findex_) != 1)
    throw std::runtime_error("Corpus: fwrite() failed on index");

  // flush write, so we can report completed persistence to upper layer
  fflush(findex_);
  fsync(sentIndex_->fd());
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

template<class Token>
StreamVersions Corpus<Token>::ComputeStreamVersions() const {
  StreamVersions versions;
  for(Sid i = 0; i < size(); i++)
    versions.Update(info(i).vid);
  return versions;
}

// explicit template instantiation
template class Corpus<SrcToken>;
template class Corpus<TrgToken>;
template class Corpus<AlignmentLink>;
template class Corpus<SentInfo>;

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
    return Token{Corpus<Token>::Vocabulary::kEosVid}; // implicit </s>
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
bool Position<Token>::compare(const Position<Token> &other, const Corpus<Token> &corpus, bool pos_order_dupes) const {
  // should request Sentence object from Corpus instead
  Sentence<Token> sentThis(corpus, sid);
  Sentence<Token> sentOther(corpus, other.sid);

  // this sorts by vid (not by surface form)
  bool smaller = std::lexicographical_compare(sentThis.begin_ + offset, sentThis.begin_ + sentThis.size_,
                                              sentOther.begin_ + other.offset, sentOther.begin_ + sentOther.size_);

  bool larger = std::lexicographical_compare(sentOther.begin_ + other.offset, sentOther.begin_ + sentOther.size_,
                                             sentThis.begin_ + offset, sentThis.begin_ + sentThis.size_);

  if(!smaller && !larger && pos_order_dupes) {
    // compared equal in Tokens? tolerate duplicate suffixes in different sentences by additional ordering
    return sid < other.sid || (sid == other.sid && offset < other.offset);
  } else {
    return smaller;
  }
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
Position<Token> Position<Token>::add(size_t offset, const Corpus<Token> &corpus) const {
  assert(corpus.sentence(this->sid).size() + 1 - this->offset >= offset + 1);
  return Position(this->sid, this->offset + offset);
}

template<class Token>
std::string Position<Token>::DebugStr(const Corpus<Token> &corpus) const {
  std::stringstream ss;
  Sentence<Token> sent = corpus.sentence(sid);
  ss << "[sid=" << static_cast<int>(sid) << " offset=" << static_cast<int>(offset) << "]";
  // print a few words
  size_t nwords_max = 4;
  for(size_t i = 0; i < nwords_max && i + offset <= sent.size(); i++)
    ss << " " << static_cast<int>(add(i, corpus).vid(corpus));
  return ss.str();
}

// explicit template instantiation
template class Position<SrcToken>;
template class Position<TrgToken>;

} // namespace sto
