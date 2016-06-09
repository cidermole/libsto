/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TYPES_H
#define STO_TYPES_H

#include <cstdint>

namespace sto {

/** Vocabulary ID type for internal use. The external interface is SrcToken/TrgToken. */
typedef uint32_t vid_t;


struct SrcToken {
  typedef vid_t Vid; /** vocabulary ID type */
  static constexpr Vid kInvalidVid = 0;

  Vid vid; /** vocabulary ID */

  /** construct invalid token */
  constexpr SrcToken(): vid(0) {}

  SrcToken(Vid v): vid(v) {}

  bool operator==(const SrcToken &other) const { return vid == other.vid; }
  bool operator!=(const SrcToken &other) const { return vid != other.vid; }
  bool operator<(const SrcToken &other) const { return vid < other.vid; }

  // these two make us iterable
  SrcToken &operator++() { ++vid; return *this; }
  Vid &operator*() { return vid; }
};

struct TrgToken {
  typedef vid_t Vid; /** vocabulary ID type */
  static constexpr Vid kInvalidVid = 0;

  Vid vid; /** vocabulary ID */

  /** construct invalid token */
  constexpr TrgToken(): vid(0) {}

  TrgToken(Vid v): vid(v) {}

  bool operator==(const TrgToken &other) const { return vid == other.vid; }
  bool operator!=(const TrgToken &other) const { return vid != other.vid; }
  bool operator<(const TrgToken &other) const { return vid < other.vid; }

  // these two make us iterable
  TrgToken &operator++() { ++vid; return *this; }
  Vid &operator*() { return vid; }
};

} // namespace sto

#endif //STO_TYPES_H
