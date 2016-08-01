/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <set>

#include "DocumentMap.h"

using namespace sto;

TEST(DocumentMapTests, load_v1) {
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
  docmap.Load(filename, nlines);

  // don't assume any particular start for the domain ID (Vocab will start mapping at ID 2)
  std::set<size_t> domain_ids;
  size_t isent = 0;
  for(size_t i = 0; i < domain_names.size(); i++) {
    size_t domain_id = docmap[domain_names[i]];
    std::cerr << "domain '" << domain_names[i] << "' got id = " << domain_id << std::endl;

    for(size_t j = 0; j < line_counts[i]; j++) {
      EXPECT_EQ(domain_id, docmap.sid2did(isent)) << "domain ID of each sentence ID should be consistent with domain ID of domain name sequence";
      isent++;
    }

    domain_ids.insert(domain_id);
  }
  EXPECT_EQ(domain_names.size(), domain_ids.size()) << "each domain should have a unique ID";
  EXPECT_EQ(domain_names.size(), docmap.numDomains());

  EXPECT_FALSE(docmap.contains("foobar"));
}
