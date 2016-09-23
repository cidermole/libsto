/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_LOGGABLE_H
#define STO_LOGGABLE_H

#include <memory>
#include <sstream>
#include <iostream>

// get rid of moses definitions, if applicable
#ifdef XVERBOSE
#undef XVERBOSE
#endif

// libsto version using class Loggable
#define XVERBOSE(level, msg) \
  do { \
    size_t verboseLevel = this->verboseLevel(); \
    \
    if(verboseLevel >= level) { \
      std::stringstream ss; \
      ss << msg; \
      this->LogMessage(ss.str()); \
    } \
  } while(0)
//

namespace sto {

/** Logger implementation */
class Logger {
public:
  /** called only if level is above verboseLevel */
  virtual void log(const std::string &message) = 0;

  virtual size_t verboseLevel() const = 0;
};

/** Base class for adding libsto logging support */
class Loggable {
public:
  virtual void SetupLogging(std::shared_ptr<Logger> logger) {
    verboseLevel_ = logger ? logger->verboseLevel() : 0;
    logger_ = logger;
  }

protected:
  size_t verboseLevel_ = 0;
  std::shared_ptr<Logger> logger_;

  /** called only if level is above verboseLevel */
  virtual void LogMessage(const std::string &message) const {
    if(logger_)
      logger_->log(message);
  }

  virtual size_t verboseLevel() const { return verboseLevel_; }
};

/** only used in unit tests, moses has its own with a lock. */
class DefaultLogger : public Logger {
public:
  DefaultLogger(size_t verbose) : verbose_(verbose) {}

  /** called only if level is above verboseLevel */
  virtual void log(const std::string &message) override {
    std::cerr << message;
  }

  virtual size_t verboseLevel() const override {
    return verbose_;
  }
private:
  size_t verbose_;
};


} // namespace sto

#endif //STO_LOGGABLE_H
