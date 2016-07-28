/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_MAPPEDFILE_H
#define STO_MAPPEDFILE_H

#include <fcntl.h>
#include <string>

namespace sto {

/**
 * Memory-mapped file.
 */
class MappedFile {
public:
  MappedFile(const std::string& filename, size_t offset = 0, int oflag = O_RDONLY);
  ~MappedFile();

  /** length of the mapping in bytes, may be smaller than the file size if using an offset */
  size_t size() const { return map_len_; }

  char *ptr; /** pointer to mapped area of file data, honors offset */

  int fd() { return fd_; }

private:
  int fd_;
  size_t map_len_; /** length of the mapping, may be smaller than the file size if using an offset */
  void *page_ptr_; /** file data pointer, points to beginning of mapped page */
};

} // namespace sto

#endif //STO_MAPPEDFILE_H
