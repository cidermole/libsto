/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DISKTREENODE_H
#define STO_DISKTREENODE_H

#include "TreeNode.h"
#include "TokenIndex.h"

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
  typedef typename TreeNode<Token, SuffixArray>::SuffixArrayT SuffixArrayT; // to do: mmap

  /**
   * if path exists: recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   *
   * @param path  path to the backing directory
   */
  TreeNodeDisk(const std::string &path);

  /** Set the path to the directory backing this DiskTreeNode. */
  //void SetPath(const std::string &path) { path_ = path; }

  /**
   * For now, leaves only. Merge the entire IndexSpan into this leaf.
   * Writes to a temporary file first, before moving it to replace the old suffix array.
   * @param curSpan  a span of this entire TreeNode
   * @param addSpan  a span of a TreeNode to be merged in (span over the same vid)
   */
  void Merge(typename TokenIndex<Token>::Span &curSpan, typename TokenIndex<Token>::Span &addSpan);

private:
  friend class ::TokenIndexTests_TreeNodeDisk_Test;

  /**
   * Creates the nested directory name for a given vid.
   * E.g. for vid=0x7a120, "0..07a/0..07a120" (two levels to avoid too many directory entries)
   */
  static std::string child_sub_path(Vid vid);

  /** full path to /array file backing leaves */
  std::string array_path() { return path_ + "/array"; }

  std::string path_; /** path to the directory backing this DiskTreeNode */
};

} // namespace sto

#endif //STO_DISKTREENODE_H
