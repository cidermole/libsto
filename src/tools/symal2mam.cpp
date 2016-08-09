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
#include <vector>
#include <utility>

#include "Corpus.h"

using namespace std;
using namespace sto;

void read_input_lines(Corpus<AlignmentLink> &corpus) {
  string line;
  size_t a, b;
  char dash;
  vector<AlignmentLink> align;
  typename Corpus<AlignmentLink>::Sid sid = 0;

  while(getline(cin, line)) {
    istringstream buf(line);
    align.clear();
    while(buf >> a >> dash >> b)
      align.push_back(make_pair(a, b));

    corpus.AddSentence(align);

    //if(!args.quiet) log_progress(sid);
  }
  /*if(!args.quiet)
    cerr << endl;*/
}

int main(int argc, char **argv) {
  if(argc != 2) {
    cerr << "builds v3 incrementally updatable word alignment for the bitext." << endl;
    cerr << "\nusage:\n\t cat <symal> | " << argv[0]
    << " <output .mam file>" << endl;
    exit(1);
  }

  std::string mamname = argv[1];

  Corpus<AlignmentLink> corpus;

  read_input_lines(corpus);

  corpus.Write(mamname);

  return 0;
}
