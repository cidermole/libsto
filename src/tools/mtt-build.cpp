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

#include "util/ug_thread_pool.h"
#include "util/usage.h"
#include "util/Time.h"
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
  string inputFile;         /** if not empty, use inputFile instead of stdin */
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

        ("input-file,f", po::value<string>(&inputFile)->default_value(""), "use input-file instead of stdin")

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

  return std::make_shared<BaseDB>(db_dir_name, /* bulkLoad = */ true);
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
void read_input_lines(BitextSide<Token> &side, DocumentMap &docMap, Args &args) {
  string line, w;
  vector<typename Corpus<Token>::Vid> sent;
  typename Corpus<Token>::Sid sid = 0;

  shared_ptr<istream> in(&cin, [](istream *p){});

  if(args.inputFile != "") {
    in.reset(new ifstream(args.inputFile));
    if(!in->good())
      throw runtime_error(string("bad input file ") + args.inputFile);
  }

  while(getline(*in, line)) {
    istringstream buf(line);
    sent.clear();
    while(buf >> w)
      sent.push_back(std::stoul(w));

    sid = side.AddToCorpus(sent, docMap.sid2did(sid), updateid_t{static_cast<stream_t>(-1), sid + 1});
    side.index()->AddSentence(side.corpus->sentence(sid));

    if(!args.quiet) log_progress(sid);
  }
  if(!args.quiet)
    cerr << endl;
}

static std::string now() {
  return std::string("[") + format_time(current_time()) + "] ";
}

template<class Token>
class Sorter {
public:
  Sorter(ITreeNode<Token> *node, Corpus<Token> &corpus): node_(node), corpus_(corpus) {}
  void operator()() {
    node_->EnsureSorted(corpus_);
  }
private:
  ITreeNode<Token> *node_;
  Corpus<Token> &corpus_;
};

int main(int argc, char **argv) {
  Args args(argc, argv);

  //cout << "base=" << args.base << " lang=" << args.lang << endl;

  shared_ptr<BaseDB> db = open_db(args.base + "db");

  DocumentMap docMap;
  docMap.Load(args.base + "dmp");

  BitextSide<Token> side(args.lang); // in-memory type BitextSide

  side.index()->Split(); // build the index in parts (so we can sort them in parallel)

  if(!args.quiet) cerr << now() << "before read_input_lines()" << endl;
  if(!args.quiet) util::PrintUsage(cerr);

  read_input_lines(side, docMap, args);
  cerr << "global index size=" << side.index()->span().size() << endl;

  if(!args.quiet) cerr << now() << "after read_input_lines()" << endl;
  if(!args.quiet) util::PrintUsage(cerr);


  boost::scoped_ptr<ug2::ThreadPool> tpool;
  tpool.reset(new ug2::ThreadPool(boost::thread::hardware_concurrency()));

  const auto top_span = side.index()->span();
  for(auto vid : top_span) {
    auto spans = top_span;
    spans.narrow(vid);

    Sorter<Token> sorter(spans.node(), *side.corpus);
    tpool->add(sorter);
  }

  tpool.reset();

  if(!args.quiet) cerr << now() << "after sorting" << endl;


  // TODO: debug only
  side.index()->span().node()->DebugCheckVidConsistency();


  // create domain indexes, create leaves for domain indexes
  //auto top_span = side.index()->span();
  for(auto domain : docMap) {
    auto domain_index = std::make_shared<sto::TokenIndex<Token, IndexTypeMemBuf>>(*side.corpus, -1);
    side.domain_indexes[domain] = domain_index;
    domain_index->Split(); // build individual indexes in parts, too.

    for(auto vid : top_span) {
      // this will create empty leaves if no positions belong to that domain at all
      // empty leaves will be taken care of (ignored) in Write() -> Merge()

      auto spant = domain_index->span();
      size_t spanSize = spant.narrow(vid);
      assert(spanSize == 0); // since we are merging into empty domain_indexes
      // (1) create tree entry (leaf)
      spant.node()->AddLeaf(vid);
      spant.narrow(Token{vid}); // step IndexSpan into the node just created (which contains an empty SA)
      assert(spant.in_array());
    }
  }

  if(!args.quiet) cerr << now() << "after creating leaves for domain indexes" << endl;

  // again could be parallelized
  //auto top_span = side.index()->span();
  unordered_map<domid_t, TreeNodeMemBuf<Token> *> dom_nodes;
  for(auto vid : top_span) {
    // sweep through each suffix array, add positions to the correct domain index
    auto spans = top_span;
    spans.narrow(vid);

    // step each domain into the current 'vid' leaf
    for(auto domain : docMap) {
      auto spand = side.domain_indexes[domain]->span();
      spand.narrow(vid);
      assert(spand.depth() == 1);
      dom_nodes[domain] = dynamic_cast<TreeNodeMemBuf<Token> *>(spand.node());
    }

    size_t spans_size = spans.size();
    for(size_t i = 0; i < spans_size; i++) {
      Position<Token> pos = spans[i];
      auto domain = docMap.sid2did(pos.sid);
      // "side.domain_indexes[domain].add(pos);"
      dom_nodes[domain]->AddPosition(side.corpus->sentence(pos.sid), pos.offset);
    }
  }

  cerr << "global index size=" << side.index()->span().size() << endl;
  for(auto domain : docMap) {
    // manually notify the domain indexes about seqNum -- AddPosition() is quite low-level
    side.domain_indexes[domain]->Flush(side.index()->streamVersions());

    cerr << "domain " << docMap[domain] << " index size=" << side.domain_indexes[domain]->span().size() << endl;
  }

  if(!args.quiet) cerr << now() << "after splitting into domain indexes" << endl;

  if(!args.quiet) cerr << "Writing Vocab, Corpus and TokenIndex ... " << endl;
  util::PrintUsage(std::cerr);

  side.Write(std::make_shared<DB<Token>>(*db), args.base);

  if(!args.quiet) cerr << now() << "done." << endl;
  util::PrintUsage(std::cerr);

  docMap.Write(db, args.base + "docmap.trk");

  return 0;
}
