/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_INCREMENTAL_BITEXT_H
#define STO_INCREMENTAL_BITEXT_H

#include <string>
#include <vector>
#include <utility>

namespace sto {

/**
 * Incremental interface used to add sentence pairs to the Bitext.
 */
class IncrementalBitext {
public:
  /**
   * Open an existing bitext as specified by base pathname and language pair.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/model."
   */
  virtual void Open(const std::string &base) = 0;

  /**
   * Add a training sentence pair.
   *
   * @param srcSent    vector of tokens of the source sentence
   * @param trgSent    vector of tokens of the target sentence
   * @param alignment  vector of aligned source-target token positions, zero-based indexing
   * @param domain     identifier for the domain; if it does not exist, create a new domain
   *
   * note: this same docstring exists in MMT project, src/Moses/native/src/wrapper/MosesDecoder.h
   */
  virtual void AddSentencePair(const std::vector<std::string> &srcSent, const std::vector<std::string> &trgSent,
                               const std::vector<std::pair<size_t, size_t>> &alignment, const std::string &domain) = 0;

  /**
   * Write to (empty) DB and disk.
   *
   * @param base  base pathname prefix, e.g. "phrase_tables/bitext."
   */
  virtual void Write(const std::string &base) = 0;
};

/**
 * Interface for a class that has an IncrementalBitext.
 * Implemented by Mmsapt to provide the IncrementalBitext interface to outside users.
 */
class HasIncrementalBitext {
public:
  virtual IncrementalBitext *GetIncrementalBitext() const = 0;
};

} // namespace sto

#endif //STO_INCREMENTAL_BITEXT_H
