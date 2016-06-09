/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_TIME_H
#define STO_TIME_H

#include <sys/time.h>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace sto {

// current time: unix timestamp with microsecond precision
double current_time() {
  struct timeval tv;
  gettimeofday(&tv,NULL);
  double cur_time = static_cast<double>(1000000ULL * tv.tv_sec + tv.tv_usec) / 1e6;
  return cur_time;
}

std::string format_time(double cur_time) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(6) << cur_time;
  return ss.str();
}

template<typename FuncBody>
double benchmark_time(FuncBody body, std::string name = "") {
  double before = current_time();
  body();
  double after = current_time();
  double elapsed = after - before;

  if(name.size() > 0) {
    std::cerr << name << " time = " << format_time(elapsed) << " s" << std::endl;
  }
  return elapsed;
}

}

#endif //STO_TIME_H
