/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include <string>
#include <fstream>
#include <vector>

#include "DocumentMap.h"

//using namespace sto;
using namespace sapt;

TEST(DocumentMapTests, load) {
  std::string filename = "res/test.dmp";
  std::ofstream dmp(filename);

  std::vector<std::string> domain_names = {"dom1", "dom2", "dom3"};
  std::vector<size_t> line_counts = {3, 5, 1};
  size_t nlines = 0;

  for(size_t i = 0; i < domain_names.size(); i++) {
    dmp << domain_names[i] << " " << line_counts[i] << std::endl;
    nlines += line_counts[i];
  }
  dmp.close();

  DocumentMap docmap;
  docmap.LoadDocumentMap(filename, nlines);

  size_t isent = 0;
  for(size_t i = 0; i < domain_names.size(); i++) {
    for(size_t j = 0; j < line_counts[i]; j++) {
      EXPECT_EQ(i, docmap.sid2did(isent));
      isent++;
    }
    EXPECT_EQ(i, docmap[domain_names[i]]);
  }
}
