/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_RANGE_H
#define STO_RANGE_H

#include <cstddef>

namespace sto {

struct Range {
  size_t begin;
  size_t end;

  size_t size() const { return end - begin; }
};

} // namespace sto

#endif //STO_RANGE_H
