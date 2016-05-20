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

public:
  Vid vid; /** vocabulary ID */

  bool operator==(const SrcToken &other) const { return vid == other.vid; }
  bool operator!=(const SrcToken &other) const { return vid != other.vid; }
};
struct TrgToken {
  typedef vid_t Vid; /** vocabulary ID type */

public:
  Vid vid; /** vocabulary ID */

  bool operator==(const SrcToken &other) const { return vid == other.vid; }
  bool operator!=(const SrcToken &other) const { return vid != other.vid; }
};

} // namespace sto

#endif //STO_TYPES_H
