/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2016 Translated SRL                *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_STREAMVERSIONS_H
#define STO_STREAMVERSIONS_H

#include <string>
#include <unordered_map>

#include "Types.h"

namespace sto {

/** maintains persistence sequence number for all streams */
class StreamVersions {
public:
  typedef typename std::unordered_map<stream_t, seqid_t>::const_iterator iterator;

  seqid_t at(stream_t stream) const;

  seqid_t &operator[](stream_t stream) { return versions_[stream]; }

  static StreamVersions Min(StreamVersions a, StreamVersions b);

  /** returns true if addition was new */
  bool Update(StreamVersions additions);

  /** returns true if addition was new */
  bool Update(sto_updateid_t addition);

  iterator begin() const { return versions_.begin(); }
  iterator end() const { return versions_.end(); }

  StreamVersions() : is_max_(false) {}

  static StreamVersions Max() { return StreamVersions(true); }

  std::string DebugStr() const;

private:
  std::unordered_map<stream_t, seqid_t> versions_;
  bool is_max_;

  StreamVersions(bool max) : is_max_(max) {}
};

} // namespace sto

#endif /* STO_STREAMVERSIONS_H */

