/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#ifndef STO_DISKTREENODE_H
#define STO_DISKTREENODE_H

#include <memory>

#include "TreeNode.h"
#include "SuffixArrayDisk.h"
#include "ITokenIndex.h"

namespace sto {

template<class Token> class DB;

struct IndexTypeMemory;
struct IndexTypeDisk;

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
  typedef typename TreeNode<Token, SuffixArray>::Iterator Iterator;

  /**
   * if path exists: recursively load the subtree rooted at this path.
   * if path does not exist: create an empty leaf node here.
   *
   * @param path  path to the backing directory
   */
  TreeNodeDisk(std::string path, std::shared_ptr<DB<Token>> db, size_t maxArraySize = 1000000);

  virtual ~TreeNodeDisk() = default;

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

  void Merge(IndexSpan<Token> &spanMemory, IndexSpan<Token> &spanDisk);

  void AddPosition(const Sentence<Token> &sent, Offset start, size_t depth) { assert(0); }

  /** Add an empty leaf node (SuffixArray) as a child. */
  void AddLeaf(Vid vid);

  /** @return true if child with 'vid' as the key was found, and optionally sets 'child'. */
  bool find_child_(Vid vid, TreeNodeDisk<Token> **child = nullptr);

private:
  std::string path_; /** path to the directory backing this DiskTreeNode */
  std::shared_ptr<DB<Token>> db_;
  bool sync_; /** whether to sync writes immediately to the database (causes immense write amplification!) */

  /** load child nodes below 'path' as indicated by the passed sequence of child vids. */
  void LoadSubtree(const Vid *children, size_t num_children);

  /**
   * Split this leaf node (SuffixArray) into a proper TreeNode with children.
   * @param depth: distance of TreeNode from the root of this tree
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

  /** Write vids of children to persistent storage */
  void WriteChildren();
};

} // namespace sto

#endif //STO_DISKTREENODE_H
