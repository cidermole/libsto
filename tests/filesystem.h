/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <boost/filesystem.hpp>

namespace sto {

void remove_all(const std::string &p) {
  boost::system::error_code ec;
  boost::filesystem::remove_all(p, ec);
  if(ec.value() != boost::system::errc::success)
    throw new std::runtime_error(ec.message());
}

void create_directory(const std::string &p) {
  boost::filesystem::create_directory(p);
}

} // namespace sto

#endif //FILESYSTEM_H
