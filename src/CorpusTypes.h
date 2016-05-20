/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_CORPUSTYPES_H
#define STO_CORPUSTYPES_H

#include <cinttypes>

#include "tpt_typedefs.h"

namespace sto {

typedef uint64_t VersionMagic;

/**
 * 'corpus.trk' file header (v3 format)
 * The v3 format consists of two files: the actual corpus track in 'corpus.trk'
 * and the sentence index in 'corpus.six' (version magic is used to detect this).
 *
 * This is layout compatible with the v2 format in MttHeader from tpt_typedefs.h
 */
struct CorpusTrackHeader {
  VersionMagic versionMagic;
  uint64_t legacy_startIdx;  /** Legacy support for loading v2 format. Use SentIndexHeader! */
  uint32_t legacy_idxSize;   /** Legacy support for loading v2 format. Use SentIndexHeader! */
  uint32_t totalWords;       /** Total number of tokens in all sentences */
};

/**
 * 'corpus.six' file header (v3 format)
 * See description of CorpusTrackHeader.
 */
struct SentIndexHeader {
  VersionMagic versionMagic;
  uint32_t idxSize; /** number of sentences. excludes the trailing sentinel */
};

/**
 * Sentence index entry in 'corpus.six' (v3 format) or in the index section in 'corpus.mct' (v2 format).
 * Points from sentence IDs to the position in the corpus track (vocabulary ID sequence offset in number of tokens).
 */
typedef uint32_t SentIndexEntry;

} // namespace sto

#endif //STO_CORPUSTYPES_H
