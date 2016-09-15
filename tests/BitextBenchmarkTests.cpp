/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "filesystem.h"
#include "DocumentMap.h"
#include "Bitext.h"
#include "DB.h"

namespace po=boost::program_options;
using namespace std;
using namespace sto;

typedef sto::SrcToken Token; // note: either SrcToken or TrgToken, these two are binary-compatible

/** program arguments, shamelessly copied from Uli */
struct Args {
  bool quiet       = false; /** no progress reporting */
  bool incremental = false; /** (unused)  build / grow vocabs automatically */
  string baseName;          /** base name for all files (includes lang suffix) */
  string base;              /** base name, e.g. "/phrase_tables/model." */
  string lang;              /** 2-letter language code */
  string mttFile;           /** name of actual track file */
  string docMap;            /** optional document map name */
  int with_global_index;    /** whether to build a global token index from all domains (1 if so) */

  Args() {}

  Args(int ac, char* av[]) {
    interpret_args(ac, av);
  }

  void interpret_args(int ac, char* av[]) {
    po::variables_map vm;
    po::options_description o("Options");
    o.add_options()

        ("help,h",  "print this message")

        ("quiet,q", po::bool_switch(&quiet), "don't print progress information")

        ("incremental,i", po::bool_switch(&incremental), "incremental mode; rewrites vocab files!")

        //("vocab-base,v", po::value<string>(&vocabBase), "base name of various vocabularies")

        ("output,o", po::value<string>(&baseName), "base file name of the resulting file(s)")

        //("sfa,s", po::value<int>(&with_sfas)->default_value(1), "also build suffix arrays")

        //("pfa,p", po::value<int>(&with_pfas)->default_value(0)->implicit_value(1), "also build prefix arrays")

        ("doc-map,m", po::value<string>(&docMap)->default_value(""), "use a document map and build separate indices")

        ("global-index,g", po::value<int>(&with_global_index)->default_value(0)->implicit_value(1), "build a global index (default without --doc-map)")

      //("dca,d", po::value<int>(&with_dcas)->default_value(0)->implicit_value(1), "also build dependency chain arrays")

      //("conll,c", po::bool_switch(&is_conll), "corpus is in CoNLL format (default: plain text)")

      //("unk,u", po::value<string>(&UNK)->default_value("UNK"), "label for unknown tokens")

      // ("map,m", po::value<string>(&vmap), "map words to word classes for indexing")

        ;

    po::options_description h("Hidden Options");
    h.add_options()
        ;
    h.add(o);
    po::positional_options_description a;
    a.add("output",1);

    po::store(po::command_line_parser(ac,av)
                  .options(h)
                  .positional(a)
                  .run(),vm);
    po::notify(vm);
    if (vm.count("help") || !vm.count("output"))
    {
      cerr << "builds v3 incrementally updatable index for one language side of the bitext." << endl;
      cerr << "\nusage:\n\t cat <corpus> | " << av[0]
      << " [options] <output .mtt file>" << endl;
      cerr << o << endl;
      exit(1);
    }
    mttFile = baseName + ".mct";

    size_t dotpos = baseName.find_last_of('.');
    if(dotpos == string::npos)
      throw runtime_error(string("could not parse output file '") + baseName + "' (should be of format base.lang) into base and lang");
    base = baseName.substr(0, dotpos + 1); // base includes trailing dot
    lang = baseName.substr(dotpos + 1);

    // default without docmap: build global index
    if(docMap.length() == 0 && !with_global_index)
      with_global_index = 1;
  }
};

shared_ptr<BaseDB> open_db(string db_dir_name) {
  using namespace boost::filesystem;

  // two instances of mtt-build run (for source and target lang).
  // nobody expects the empty directory!!!
  if(!exists(db_dir_name))
    create_directory(db_dir_name);

  return std::make_shared<BaseDB>(db_dir_name);
}

void log_progress(size_t ctr) {
  if (ctr % 100000 == 0) {
    if (ctr) cerr << endl;
    cerr << setw(12) << ctr / 1000 << "K sentences processed ";
  } else if (ctr % 10000 == 0) {
    cerr << ".";
  }
}


/**
 * Reads all input lines from stdin.
 *
 * For each line, add to:
 * * Vocab
 * * Corpus
 * * TokenIndex global
 * * TokenIndex domain-specific
 */
void read_input_lines(std::istream &is, BitextSide<Token> &side, DocumentMap &docMap, Args &args) {
  string line, w;
  vector<string> sent;
  typename Corpus<Token>::Sid sid = 0;

  bool domains = (docMap.numDomains() > 0);

  while(getline(is, line)) {
    istringstream buf(line);
    sent.clear();
    while(buf >> w)
      sent.push_back(w);

    sid = side.AddToCorpus(sent);
    side.index->AddSentence(side.corpus->sentence(sid));

    if(!args.quiet) log_progress(sid);

    if(domains) {
      auto docid = docMap.sid2did(sid);
      side.AddToDomainIndex(sid, docid, sid + 1);
    }
  }
  if(!args.quiet)
    cerr << endl;
}

TEST(BitextBenchmarkTests, benchmark_build) {
  Args args;

  //args.base = "/home/david/Info/ownCloud/mmt/src-nosync/MMT/engines/TrainingBenchmarkTests-0613d35c-8440-4a59-afc6-7c836a00c591/models/phrase_tables/model.en.";

  std::string dirname = "res/BitextBenchmarkTests";
  sto::remove_all(dirname);
  sto::create_directory(dirname);

  args.base = dirname;
  args.lang = "en";

  //cout << "base=" << args.base << " lang=" << args.lang << endl;

  shared_ptr<BaseDB> db = open_db(args.base + "db");

  DocumentMap docMap;
  //docMap.Load(args.base + "dmp");
  docMap.Load("/tmp/model.dmp.50k"); // europarl 10000

  BitextSide<Token> side(args.lang, docMap); // in-memory type BitextSide

  std::ifstream ifs("/tmp/model.en.50k");
  // to do: for speed, we could read as the old mtt-build, sort in parallel, and then create the TokenIndex.
  read_input_lines(ifs, side, docMap, args);

}
