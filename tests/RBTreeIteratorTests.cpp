/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <gtest/gtest.h>

#include "fisheryates.h"
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

TEST(RBTreeIteratorTests, random_tests) {
  const size_t seed = 42;
  const size_t trials = 10;
  const size_t num_keys = 100;
  std::mt19937 gen(seed);
  std::vector<size_t> keys;

  for(size_t i = 0; i < trials; i++) {
    // uniform_int_distribution takes a closed range [a,b]
    std::uniform_int_distribution<size_t> choose(1, num_keys);

    // generate random number of keys between [1,num_keys]
    random_indices_unsort(choose(gen), num_keys, gen, keys);

    RBTree<size_t, size_t> tree;

    for(auto k : keys)
      tree[k] = k;
    //tree.Print();

    std::vector<size_t> seq;
    for(auto k : tree) {
      //std::cerr << "key " << k << std::endl;
      seq.push_back(k);
      EXPECT_EQ(k, tree[k]);
    }

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys, seq);
  }
}
