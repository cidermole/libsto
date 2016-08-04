/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "Bitext.h"

#include "filesystem.h"

using namespace sto;

TEST(BitextTests, create_empty_write) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  Bitext bitext("fr", "en");
  bitext.Write(dirname + "/bitext.");

  //remove_all(dirname);
}

TEST(BitextTests, create_empty_write_read) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  Bitext bitext("fr", "en");
  bitext.Write(base);

  Bitext read(base, "fr", "en");

  //remove_all(dirname);
}
