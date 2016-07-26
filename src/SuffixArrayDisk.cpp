/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "SuffixArrayDisk.h"
#include "rocksdb/db.h"

#include <iostream>

namespace sto {

template<class Token>
SuffixArrayDisk<Token>::SuffixArrayDisk(const std::string &filename, rocksdb::DB *db) {
  rocksdb::Slice key = filename;
  std::string value;

  rocksdb::Status status = db->Get(rocksdb::ReadOptions(), key, &value);
  std::cerr << "Get on DB=" << db << " key=" << filename << " status.ok()=" << status.ok() << " IsNotFound()=" << status.IsNotFound() << std::endl;

  /*
  if(status.IsNotFound()) {
    std::string empty;
    db->Put(rocksdb::WriteOptions(), key, empty); // necessary? we can just pretend. assuming that value stays empty then...
    value = empty;
  }
  */
  if(status.IsNotFound())
    assert(value.size() == 0);

  // keep data block cached in RAM
  value_ = value;

  array_ = reinterpret_cast<SuffixArrayPosition<Token> *>(const_cast<char *>(value_.c_str()));
  length_ = value_.size() / sizeof(SuffixArrayPosition<Token>);
}

// explicit template instantiation
template class SuffixArrayDisk<SrcToken>;
template class SuffixArrayDisk<TrgToken>;

} // namespace sto