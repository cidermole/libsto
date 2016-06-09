/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_USAGE_H
#define STO_USAGE_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <stdint.h>

namespace util {

// Time in seconds since process started.  Zero on unsupported platforms.
double WallTime();

// User + system time.
double CPUTime();

// Resident usage in bytes.
uint64_t RSSMax();

void PrintUsage(std::ostream &to);

// Determine how much physical memory there is.  Return 0 on failure.
uint64_t GuessPhysicalMemory();

} // namespace util

#endif //STO_USAGE_H
