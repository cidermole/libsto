/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "util/rbtree.hpp"

using namespace sto;

TEST(RBTreeIteratorTests, tree) {
  RBTree<int, int> tree;

  tree[2]=2;
  tree[1]=1;
  tree[4]=4;
  tree[3]=3;
  tree[7]=7;

  //tree.Print();

  std::vector<int> seq;
  for(auto k : tree) {
    //std::cerr << "key " << k << std::endl;
    seq.push_back(k);
  }

  std::vector<int> expected_seq = {1, 2, 3, 4, 7};
  EXPECT_EQ(expected_seq, seq);
}


TEST(RBTreeIteratorTests, single) {
  RBTree<int, int> tree;

  tree[2]=2;

  //tree.Print();

  std::vector<int> seq;
  for(auto k : tree) {
    //std::cerr << "key " << k << std::endl;
    seq.push_back(k);
  }

  std::vector<int> expected_seq = {2};
  EXPECT_EQ(expected_seq, seq);
}


TEST(RBTreeIteratorTests, empty) {
  RBTree<int, int> tree;

  //tree.Print();

  std::vector<int> seq;
  for(auto k : tree) {
    //std::cerr << "key " << k << std::endl;
    seq.push_back(k);
  }

  std::vector<int> expected_seq = {};
  EXPECT_EQ(expected_seq, seq);
}
