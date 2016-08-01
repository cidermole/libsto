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

#include <boost/filesystem.hpp>

#include "DocumentMap.h"
#include "DB.h"

using namespace sto;

TEST(DocumentMapTests, load_v1) {
  std::string filename = "res/test.dmp";
  std::ofstream dmp(filename);

  std::vector<std::string> domain_names = {"dom1", "dom2", "dom3"};
  std::vector<size_t> line_counts = {3, 5, 1};
  size_t nlines = 0;

  // write .dmp document map (text file)

  for(size_t i = 0; i < domain_names.size(); i++) {
    dmp << domain_names[i] << " " << line_counts[i] << std::endl;
    nlines += line_counts[i];
  }
  dmp.close();

  // load and verify

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

static void remove_all(const std::string &p) {
  using namespace boost::filesystem;
  boost::system::error_code ec;
  path base(p);
  boost::filesystem::remove_all(base, ec); // ensure no leftovers
}

TEST(DocumentMapTests, save_load_append) {
  DocumentMap docmap_build;

  std::string dirname = "res/DocumentMapTests";
  remove_all(dirname);
  boost::filesystem::create_directory(dirname);

  std::vector<std::string> domain_names = {"dom1", "dom2", "dom3"};
  std::vector<size_t> line_counts = {3, 5, 1};
  size_t nlines = 0;

  // build DocumentMap in memory

  for(size_t i = 0; i < domain_names.size(); i++) {
    for(size_t j = 0; j < line_counts[i]; j++) {
      docmap_build.AddSentence(nlines, docmap_build.FindOrInsert(domain_names[i]));
      nlines++;
    }
  }

  // dump to disk

  std::shared_ptr<DB<Domain>> db(new DB<Domain>(dirname + "/docmap"));
  std::string corpusfile = dirname + "/sentmap.trk";
  docmap_build.Write(db, corpusfile);

  // load from disk
  DocumentMap docmap{db, corpusfile};

  // append
  docmap.AddSentence(nlines++, docmap.FindOrInsert("domAdd"));
  line_counts.push_back(1);
  domain_names.push_back("domAdd");
  // TODO: AddSentence() could take a domain name instead.

  // verify

  //domain_names[0] = "domx"; // make test fail

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

  //remove_all(dirname);
}
