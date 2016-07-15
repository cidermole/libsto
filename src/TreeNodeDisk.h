/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DISKTREENODE_H
#define STO_DISKTREENODE_H

#include "TreeNode.h"
#include "SuffixArrayDisk.h"

class TokenIndexTests_TreeNodeDisk_Test;

namespace sto {

/**
 * Like TreeNode, an internal representation used by TokenIndex,
 * represents a word and its possible suffix extensions.
 *
 * Geared towards a disk-based hybrid suffix tree/array.
 *
 * The tree part is currently stored as an open directory structure in the filesystem itself.
 * Leaves (suffix array chunks) can be memory mapped.
 */
template<class Token>
class TreeNodeDisk : public TreeNode<Token, SuffixArrayDisk<Token>> {
public:
  typedef SuffixArrayDisk<Token> SuffixArray;
  typedef typename TreeNode<Token, SuffixArray>::Vid Vid;
  typedef typename TreeNode<Token, SuffixArray>::Offset Offset;
  typedef typename TreeNode<Token, SuffixArray>::SuffixArrayT SuffixArrayT; // to do: mmap

  /**
   * if path exists: recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   *
   * @param path  path to the backing directory
   */
  TreeNodeDisk(std::string path, size_t maxArraySize = 1000000);

  /** Set the path to the directory backing this DiskTreeNode. */
  //void SetPath(const std::string &path) { path_ = path; }

  /**
   * For now, leaves only. Merge the entire IndexSpan into this leaf.
   * Writes to a temporary file first, before moving it to replace the old suffix array.
   *
   * Assumes there is at most one writer at all times (one process, and only one writing thread).
   *
   * @param addSpan  TreeNode span to be merged in (span over the same vid); either TokenIndex<Token>::Span or SuffixArrayPositionSpan
   * @param corpus   Positions belong to this Corpus
   * @param depth    distance of this TreeNode from the root
   */
  template<class PositionSpan>
  void MergeLeaf(const PositionSpan &addSpan, const Corpus<Token> &corpus, Offset depth);

  void AddPosition(const Sentence<Token> &sent, Offset start, size_t depth) { assert(0); }

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNodeDisk<Token> **child = nullptr);

private:
  friend class ::TokenIndexTests_TreeNodeDisk_Test;

  std::string path_; /** path to the directory backing this DiskTreeNode */

  /** load child nodes as indicated by directory tree structure in 'path' */
  void LoadSubtree();

  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   * depth: distance of TreeNode from the root of this tree
   */
  void SplitNode(const Corpus<Token> &corpus, Offset depth);

  /**
   * Creates the nested directory name for a given vid.
   * E.g. for vid=0x7a120, "0..07a/0..07a120" (two levels to avoid too many directory entries)
   */
  static std::string child_sub_path(Vid vid);

  /** full path to /array file backing the leaf */
  std::string array_path() { return path_ + "/array"; }

  /** full path to directory backing child with given vid. */
  std::string child_path(Vid vid) { return path_ + "/" + child_sub_path(vid); }

  /** factory function for TreeNode::SplitNode() */
  TreeNodeDisk<Token> *make_child_(Vid vid, typename SuffixArray::iterator first, typename SuffixArray::iterator last, const Corpus<Token> &corpus, Offset depth);

  /** Take ownership of 'first', and write array at this node level. */
  void WriteArray(SuffixArrayPosition<Token> **first, SuffixArrayPosition<Token> *last);
};

} // namespace sto

#endif //STO_DISKTREENODE_H
