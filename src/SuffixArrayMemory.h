/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_SUFFIXARRAYMEMORY_H
#define STO_SUFFIXARRAYMEMORY_H

#include "Corpus.h"

#include <vector>

namespace sto {

template<class Token>
using SuffixArrayMemory = std::vector<AtomicPosition<Token>>;

} // namespace sto

#endif //STO_SUFFIXARRAYMEMORY_H
