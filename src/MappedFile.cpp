/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string>
#include <stdexcept>

#include "MappedFile.h"

namespace sto {

MappedFile::MappedFile(const std::string& filename, size_t offset) {
  int fd;
  struct stat sb;

  size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

  if((fd = open(filename.c_str(), O_RDONLY)) == -1)
    throw std::runtime_error(std::string("open() failed on ") + filename);
  if(fstat(fd, &sb) == -1) {
    close(fd);
    throw std::runtime_error(std::string("fstat() failed on ") + filename);
  }
  map_len_ = static_cast<size_t>(sb.st_size) - offset;

  if(map_len_ == 0) {
    // special case for handling empty files (MAP_FAILED -> EINVAL if length is 0, see http://linux.die.net/man/2/mmap)
    page_ptr_ = ptr = nullptr;
    fd_ = 0;
    return;
  }

  if((page_ptr_ = mmap(0, map_len_, PROT_READ, MAP_SHARED, fd, offset - offset % page_size)) == MAP_FAILED) {
    close(fd);
    throw std::runtime_error(std::string("mmap(): MAP_FAILED on ") + filename);
  }
  fd_ = fd;
  ptr = reinterpret_cast<char *>(page_ptr_) + offset % page_size;
}

MappedFile::~MappedFile() {
  if(map_len_) {
    munmap(page_ptr_, map_len_);
    close(fd_);
  }
}

} // namespace sto
