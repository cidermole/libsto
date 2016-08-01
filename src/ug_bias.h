// -*- mode: c++; indent-tabs-mode: nil; tab-width:2  -*-
#pragma once

#include "tpt_typedefs.h"

namespace sapt
{

/**
 * Provides bias weights for BitextSampler.
 *
 * Simplification of SamplingBias from ug_sampling_bias.h
 */
class IBias {
public:
  virtual ~IBias() {}

  /**
   * Get a vector of unnormalized domain biases, in descending order of score.
   * Each pair is <float, doc_index>
   */
  virtual void getRankedBias(std::vector<std::pair<float, tpt::docid_type> >& bias) const = 0;

  /** sentence bias for sentence ID sid. */
  virtual float operator[](const tpt::sid_type sid) const = 0;
};

} // namespace sapt
