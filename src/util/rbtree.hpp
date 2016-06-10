/**
 * Author: Xidorn Quan <github@upsuper.org>
 *
 * Kudos: https://gist.github.com/upsuper/6332576
 *
 * It's a textbook algorithm. I am shamelessly copying this into an open source project.
 */

#ifndef RBTREE_RBTREE_H_
#define RBTREE_RBTREE_H_

#include <cstddef>
#include <cassert>
#include <utility>

namespace sto {

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

template <class Key>
class RBTree {
 public:
  typedef std::size_t size_type;

  inline RBTree() : nil_(new Node), count_(0) {
    root_ = nil_;
    nil_->parent = nil_;
    nil_->left = nil_;
    nil_->right = nil_;
    nil_->color = kBlack;
  }
  inline ~RBTree() {
    FreeSubtree(root_);
    delete nil_;
  }
  bool Put(const Key& key);
  bool Remove(const Key& key);
  inline bool Contains(const Key& key) const {
    Node *node = FindNodeOrParent(key);
    return !IsNil(node) && node->key == key;
  }
  inline size_type Count() const {
    return count_;
  }
  inline bool Empty() const {
    return count_ == 0;
  }

 protected:
  enum Color { kRed, kBlack };
  struct Node {
    Node *parent;
    Node *left;
    Node *right;
    Color color;
    Key key;
  };

  inline const Node *GetRoot() const {
    return root_;
  }
  inline bool IsNil(const Node *node) const {
    return node == nil_;
  }
  inline bool IsRed(const Node *node) const {
    return node->color == kRed;
  }
  inline bool IsBlack(const Node *node) const {
    return node->color == kBlack;
  }

 private:
  inline void SetRed(Node *node) {
    assert(node != nil_);
    node->color = kRed;
  }
  inline void SetBlack(Node *node) {
    node->color = kBlack;
  }
  inline bool IsLeftChild(const Node *node) const {
    return node->parent->left == node;
  }
  inline bool IsRightChild(const Node *node) const {
    return node->parent->right == node;
  }
  inline void SetLeft(Node *node, Node *child) {
    assert(!IsNil(node));
    node->left = child;
    if (!IsNil(child))
      child->parent = node;
  }
  inline void SetRight(Node *node, Node *child) {
    assert(!IsNil(node));
    node->right = child;
    if (!IsNil(child))
      child->parent = node;
  }
  inline Node *GetSibling(const Node *node) const {
    if (IsLeftChild(node))
      return node->parent->right;
    else if (IsRightChild(node))
      return node->parent->left;
    assert(false);
  }
  inline Node *ReplaceChild(Node *child, Node *new_child) {
    if (IsNil(child->parent)) {
      root_ = new_child;
      new_child->parent = nil_;
    } else if (IsLeftChild(child)) {
      SetLeft(child->parent, new_child);
    } else if (IsRightChild(child)) {
      SetRight(child->parent, new_child);
    } else { assert(false); }
    return new_child;
  }
  inline Node *LeftRotate(Node *node) {
    assert(node != nil_ && node->right != nil_);
    Node *child = node->right;
    ReplaceChild(node, child);
    SetRight(node, child->left);
    SetLeft(child, node);
    std::swap(node->color, child->color);
    return child;
  }
  inline Node *RightRotate(Node *node) {
    assert(node != nil_ && node->left != nil_);
    Node *child = node->left;
    ReplaceChild(node, child);
    SetLeft(node, child->right);
    SetRight(child, node);
    std::swap(node->color, child->color);
    return child;
  }
  inline Node *ReverseRotate(Node *node) {
    if (IsLeftChild(node))
      return RightRotate(node->parent);
    else if (IsRightChild(node))
      return LeftRotate(node->parent);
    assert(false);
  }
  inline Node *FindNodeOrParent(const Key& key) const {
    Node *node = root_;
    Node *parent = nil_;
    while (!IsNil(node)) {
      if (node->key == key) return node;
      parent = node;
      node = node->key > key ? node->left : node->right;
    }
    return parent;
  }
  void FixInsert(Node *node);
  void FixRemove(Node *node);
  void FreeSubtree(Node *root) {
    if (root != nil_) {
      FreeSubtree(root->left);
      FreeSubtree(root->right);
      delete root;
    }
  }

  Node *root_;
  Node *nil_;
  size_type count_;

  DISALLOW_COPY_AND_ASSIGN(RBTree<Key>);
};

/* Public */

template <class Key>
bool RBTree<Key>::Put(const Key& key) {
  Node *parent = FindNodeOrParent(key);
  if (!IsNil(parent) && parent->key == key)
    return false;
  Node *node = new Node{nil_, nil_, nil_, kRed, key};
  if (IsNil(parent)) {
    root_ = node;
  } else {  // !IsNil(parent)
    if (key < parent->key)
      SetLeft(parent, node);
    else
      SetRight(parent, node);
  }
  FixInsert(node);
  ++count_;
  return true;
}

template <class Key>
bool RBTree<Key>::Remove(const Key& key) {
  Node *node = FindNodeOrParent(key);
  Node *child;
  if (IsNil(node) || node->key != key)
    return false;
  if (IsNil(node->right)) {
    child = node->left;
  } else if (IsNil(node->left)) {
    child = node->right;
  } else {
    Node *sub = node->right;
    while (!IsNil(sub->left))
      sub = sub->left;
    node->key = std::move(sub->key);
    node = sub;
    child = sub->right;
  }
  child = IsNil(child) ? node : ReplaceChild(node, child);
  if (IsBlack(node))
    FixRemove(child);
  if (node == child)
    ReplaceChild(node, nil_);
  delete node;
  --count_;
  return true;
}

/* Private */

template <class Key>
void RBTree<Key>::FixInsert(Node *node) {
  while (!IsBlack(node) && !IsBlack(node->parent)) {
    Node *parent = node->parent;
    Node *uncle = GetSibling(parent);
    if (IsRed(uncle)) {
      SetBlack(uncle);
      SetBlack(parent);
      SetRed(parent->parent);
      node = parent->parent;
    } else {  // IsBlack(uncle)
      if (IsLeftChild(node) != IsLeftChild(parent))
        parent = ReverseRotate(node);
      node = ReverseRotate(parent);
    }
  }
  if (IsNil(node->parent))
    SetBlack(node);
}

template <class Key>
void RBTree<Key>::FixRemove(Node *node) {
  while (!IsRed(node) && !IsNil(node->parent)) {
    Node *sibling = GetSibling(node);
    if (IsRed(sibling)) {
      ReverseRotate(sibling);
      sibling = GetSibling(node);
    }
    if (IsBlack(sibling->left) && IsBlack(sibling->right)) {
      SetRed(sibling);
      node = node->parent;
    } else {
      if (IsLeftChild(sibling) && !IsRed(sibling->left))
        sibling = LeftRotate(sibling);
      else if (IsRightChild(sibling) && !IsRed(sibling->right))
        sibling = RightRotate(sibling);
      ReverseRotate(sibling);
      node = GetSibling(node->parent);
    }
  }
  SetBlack(node);
}

} // namespace sto

#endif // RBTREE_RBTREE_H_
