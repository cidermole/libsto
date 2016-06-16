/**
 * Author: Xidorn Quan <github@upsuper.org>
 * License: MIT License
 *
 * Quote email 2016-06-11 12:11 AM:
 * "In general, I would apply MIT License to my random code, and I'd like to do so for this as well.  - Xidorn"
 *
 * The original at https://gist.github.com/upsuper/6332576 was a textbook red-black tree algorithm.
 *
 */

#ifndef RBTREE_RBTREE_H_
#define RBTREE_RBTREE_H_

#include <memory>
#include <cstddef>
#include <cassert>
#include <utility>

namespace sto {



/**
 * Red-black tree of vids that maintains an additional partial_sum on each node.
 * Search is possible both through vids and through a size offset (binary searched for in partial_sums).
 *
 * KeyType must support these operators: !=, ==, <, >
 * ValueType must currently have a default constructor
 */
template<typename KeyType, typename ValueType>
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
    root_.reset();
    nil_.reset();
  }
  bool Remove(const KeyType& key);
  inline bool Contains(const KeyType& key) const {
    std::shared_ptr<Node> node = FindNodeOrParent(key);
    return !IsNil(node) && node->key == key;
  }
  inline size_type Count() const {
    return count_;
  }
  inline bool Empty() const {
    return count_ == 0;
  }

  ValueType& FindOrInsert(const KeyType& key, size_t add_size = 0) {
    std::pair<std::shared_ptr<Node> , bool> result = Put(key);
    if(add_size != 0)
      AddSize(result.first, add_size); // update node's and ancestors' partial_sums
    return result.first->value;
  }

  bool Find(const KeyType& key, ValueType *val = nullptr) const {
    std::shared_ptr<Node> node = FindNodeOrParent(key);
    if(!IsNil(node) && node->key == key) {
      if(val != nullptr)
        *val = node->value;
      return true;
    } else {
      return false;
    }
  }

  // debug only
  size_t ChildSize(const KeyType& key) const {
    std::shared_ptr<Node> node = FindNodeOrParent(key);
    assert(!IsNil(node) && node->key == key);
    return node->partial_sum;
  }

  size_t Size() const {
    return root_->partial_sum;
  }

  /**
   * Random access into this tree at a specific size offset.
   * Changes 'offset' to be relative into the node returned.
   * */
  ValueType& At(size_t *offset) {
    std::shared_ptr<Node> node = At(root_, *offset);
    return node->value;
  }

  void AddSize(const KeyType& key, size_t add_size) {
    std::shared_ptr<Node> node = FindNodeOrParent(key);
    assert(!IsNil(node) && node->key == key);
    AddSize(node, add_size);
  }

  /** Walk tree in-order and apply func(key, value) to each node. */
  template<typename Func>
  void Walk(Func func) {
    Walk(root_, func);
  }

 protected:
  enum Color { kRed, kBlack };
  struct Node {
    KeyType key;

    size_t partial_sum; /** size sum of this node + its children */
    size_t own_size; /** size of this node only */
    ValueType value;

    Node(std::shared_ptr<Node> p, std::shared_ptr<Node> l, std::shared_ptr<Node> r, Color c, KeyType k): key(k), partial_sum(0), own_size(0), value(), parent(p), left(l), right(r), color(c) {}

    Node(): partial_sum(0), own_size(0), value(), parent(nullptr), left(nullptr), right(nullptr) {}

  private:
    std::shared_ptr<Node> parent;
    std::shared_ptr<Node> left;
    std::shared_ptr<Node> right;
    Color color;

    friend class RBTree<KeyType, ValueType>;
  };

  /** find or insert */
  std::pair<std::shared_ptr<Node> , bool> Put(const KeyType& key);

  /** update node's and ancestors' partial_sums */
  inline void AddSize(std::shared_ptr<Node> node, size_t add_size) {
    node->own_size += add_size;

    std::shared_ptr<Node> n = node;
    while(!IsNil(n)) {
      n->partial_sum += add_size;
      n = n->parent;
    }
  }

  // note: changes offset to be relative into the node returned
  inline std::shared_ptr<Node> At(std::shared_ptr<Node> node, size_t &offset) {
    //std::shared_ptr<Node> prev = node;
    assert(offset < node->partial_sum);

    // nodes in-order like this: (left, node, right)
    while(node != nil_) {
      //prev = node;
      // to do: to work with 0-sized nodes, this should be upper_bound style!
      if(offset < node->left->partial_sum) {
        node = node->left;
      } else if(offset < node->left->partial_sum + node->own_size) {
        offset -= node->left->partial_sum;
        return node;
      } else { // offset < node->left->partial_sum + node->own_size + node->right->partial_sum == node->partial_sum
        offset -= node->left->partial_sum + node->own_size;
        node = node->right;
      }
    }
    assert(false);
    //return prev;
    return nullptr;
  }

  template<typename Func>
  void Walk(std::shared_ptr<Node> node, Func func) {
    if (node != nil_) {
      Walk(node->left, func);
      func(node->key, node->value);
      Walk(node->right, func);
    }
  }

  inline const std::shared_ptr<Node> GetRoot() const {
    return root_;
  }
  inline bool IsNil(const std::shared_ptr<Node> node) const {
    return node == nil_;
  }
  inline bool IsRed(const std::shared_ptr<Node> node) const {
    return node->color == kRed;
  }
  inline bool IsBlack(const std::shared_ptr<Node> node) const {
    return node->color == kBlack;
  }

 private:
  inline void SetRed(std::shared_ptr<Node> node) {
    assert(node != nil_);
    node->color = kRed;
  }
  inline void SetBlack(std::shared_ptr<Node> node) {
    node->color = kBlack;
  }
  inline bool IsLeftChild(const std::shared_ptr<Node> node) const {
    return node->parent->left == node;
  }
  inline bool IsRightChild(const std::shared_ptr<Node> node) const {
    return node->parent->right == node;
  }
  inline void SetLeft(std::shared_ptr<Node> node, std::shared_ptr<Node> child) {
    assert(!IsNil(node));
    node->left = child;
    if (!IsNil(child))
      child->parent = node;
  }
  inline void SetRight(std::shared_ptr<Node> node, std::shared_ptr<Node> child) {
    assert(!IsNil(node));
    node->right = child;
    if (!IsNil(child))
      child->parent = node;
  }
  inline std::shared_ptr<Node> GetSibling(const std::shared_ptr<Node> node) const {
    if (IsLeftChild(node))
      return node->parent->right;
    else if (IsRightChild(node))
      return node->parent->left;
    assert(false);
    return nullptr;
  }
  inline std::shared_ptr<Node> ReplaceChild(std::shared_ptr<Node> child, std::shared_ptr<Node> new_child) {
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

  /*
   * TODO: thread safety
   * LeftRotate() and RightRotate() need to allocate two new nodes for P and Q, make them valid,
   * then swap them.
   * Since delete of the two old nodes depends on their usage from reading threads, we must use
   * shared_ptr to do thread-safe atomic reference counting and release memory when appropriate.
   */
  inline std::shared_ptr<Node> LeftRotate(std::shared_ptr<Node> node) {
    assert(node != nil_ && node->right != nil_);
    std::shared_ptr<Node> child = node->right;
    child->partial_sum = node->partial_sum;
    ReplaceChild(node, child);
    SetRight(node, child->left);
    SetLeft(child, node);
    node->partial_sum = node->own_size + node->left->partial_sum + node->right->partial_sum;
    std::swap(node->color, child->color);
    return child;
  }
  // TODO: thread safety
  inline std::shared_ptr<Node> RightRotate(std::shared_ptr<Node> node) {
    assert(node != nil_ && node->left != nil_);
    std::shared_ptr<Node> child = node->left;
    child->partial_sum = node->partial_sum;
    ReplaceChild(node, child);
    SetLeft(node, child->right);
    SetRight(child, node);
    node->partial_sum = node->own_size + node->left->partial_sum + node->right->partial_sum;
    std::swap(node->color, child->color);
    return child;
  }
  inline std::shared_ptr<Node> ReverseRotate(std::shared_ptr<Node> node) {
    if (IsLeftChild(node))
      return RightRotate(node->parent);
    else if (IsRightChild(node))
      return LeftRotate(node->parent);
    assert(false);
    return nullptr;
  }
  inline std::shared_ptr<Node> FindNodeOrParent(const KeyType& key) const {
    std::shared_ptr<Node> node = root_;
    std::shared_ptr<Node> parent = nil_;
    while (!IsNil(node)) {
      if (node->key == key) return node;
      parent = node;
      node = node->key > key ? node->left : node->right;
    }
    return parent;
  }
  void FixInsert(std::shared_ptr<Node> node);
  void FixRemove(std::shared_ptr<Node> node);

  std::shared_ptr<Node> root_;
  std::shared_ptr<Node> nil_;
  size_type count_;

  // disallow copy and assign
  RBTree(const RBTree<KeyType, ValueType>&);
  void operator=(const RBTree<KeyType, ValueType>&);
};

/* Public */

template <typename KeyType, typename ValueType>
std::pair<std::shared_ptr<typename RBTree<KeyType, ValueType>::Node> , bool>
RBTree<KeyType, ValueType>::Put(const KeyType& key) {
  std::shared_ptr<Node> parent = FindNodeOrParent(key);
  if (!IsNil(parent) && parent->key == key)
    return std::make_pair(parent, false); // no insertion; return existing node
  std::shared_ptr<Node> node = std::make_shared<Node>(nil_, nil_, nil_, kRed, key);
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
  return std::make_pair(node, true);
}

template <typename KeyType, typename ValueType>
bool RBTree<KeyType, ValueType>::Remove(const KeyType& key) {
  std::shared_ptr<Node> node = FindNodeOrParent(key);
  std::shared_ptr<Node> child;
  if (IsNil(node) || node->key != key)
    return false;
  if (IsNil(node->right)) {
    child = node->left;
  } else if (IsNil(node->left)) {
    child = node->right;
  } else {
    std::shared_ptr<Node> sub = node->right;
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

template <typename KeyType, typename ValueType>
void RBTree<KeyType, ValueType>::FixInsert(std::shared_ptr<Node> node) {
  while (!IsBlack(node) && !IsBlack(node->parent)) {
    std::shared_ptr<Node> parent = node->parent;
    std::shared_ptr<Node> uncle = GetSibling(parent);
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

template <typename KeyType, typename ValueType>
void RBTree<KeyType, ValueType>::FixRemove(std::shared_ptr<Node> node) {
  while (!IsRed(node) && !IsNil(node->parent)) {
    std::shared_ptr<Node> sibling = GetSibling(node);
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
