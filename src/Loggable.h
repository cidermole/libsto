/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_LOGGABLE_H
#define STO_LOGGABLE_H

// get rid of moses definitions, if applicable
#ifdef XVERBOSE
#undef XVERBOSE
#endif

// libsto version using class Loggable
#define XVERBOSE(level, msg) \
  do { \
    size_t verboseLevel = this->verboseLevel(); \
    std::ostream *log = this->log(); \
    \
    if(verboseLevel >= level && log) { \
      (*log) << msg; \
    } \
  } while(0)
//

namespace sto {

/** libsto logging support */
class Loggable {
protected:
  size_t verboseLevel_ = 0;
  std::ostream *log_ = nullptr;

  virtual size_t verboseLevel() const { return verboseLevel_; }
  virtual std::ostream *log() const { return log_; }
};

} // namespace sto

#endif //STO_LOGGABLE_H
