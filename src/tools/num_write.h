// -*- mode: c++; indent-tabs-mode: nil; tab-width:2  -*-
// (c) 2006,2007,2008 Ulrich Germann

#pragma once

#include <iostream>

namespace tpt {

void
numwrite(std::ostream &out, uint8_t const &x) {
  out.write((const char *) &x, 1);
}

void
numwrite(std::ostream &out, uint16_t const &x) {
  char buf[2];
  buf[0] = x % 256;
  buf[1] = (x >> 8) % 256;
  out.write(buf, 2);
}

void
numwrite(std::ostream &out, uint32_t const &x) {
  char buf[4];
  buf[0] = x % 256;
  buf[1] = (x >> 8) % 256;
  buf[2] = (x >> 16) % 256;
  buf[3] = (x >> 24) % 256;
  out.write(buf, 4);
}

void
numwrite(std::ostream &out, uint64_t const &x) {
  char buf[8];
  buf[0] = x % 256;
  buf[1] = (x >> 8) % 256;
  buf[2] = (x >> 16) % 256;
  buf[3] = (x >> 24) % 256;
  buf[4] = (x >> 32) % 256;
  buf[5] = (x >> 40) % 256;
  buf[6] = (x >> 48) % 256;
  buf[7] = (x >> 56) % 256;
  out.write(buf, 8);
}

} // namespace tpt
