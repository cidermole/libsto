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
  bitext.Write(dirname + "/bitext."); // write an empty Bitext

  //remove_all(dirname);
}

TEST(BitextTests, create_empty_write_read) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  Bitext bitext("fr", "en");
  bitext.Write(base); // write an empty Bitext

  // just a rudimentary test to ensure loading from disk does not fail;
  // class Bitext does not provide read access, so the remaining tests must be from moses.

  Bitext read(base, "fr", "en");

  //remove_all(dirname);
}

TEST(BitextTests, create_add_write_read) {
  std::string dirname = "res/BitextTests";
  remove_all(dirname);
  create_directory(dirname);

  std::string base = dirname + "/bitext.";
  Bitext bitext("fr", "en");

  // to add stuff, there needs to be an empty Bitext before
  bitext.Write(base); // write an empty Bitext


  {
    Bitext writable(base, "fr", "en");

    writable.AddSentencePair(
        std::vector<std::string>{"source", "words"},
        std::vector<std::string>{"target", "sentence", "words"},
        std::vector<std::pair<size_t, size_t>>{{0,0}, {1,2}},
        "dom1"
    );
  }


  // just a rudimentary test to ensure loading from disk does not fail;
  // class Bitext does not provide read access, so the remaining tests must be from moses.

  Bitext read(base, "fr", "en");

  //remove_all(dirname);
}
