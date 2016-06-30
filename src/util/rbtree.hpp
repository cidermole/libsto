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

#include <sstream>

#include <memory>
#include <cstddef>
#include <cassert>
#include <utility>

namespace sto {



/**
 * Red-black tree of vids that maintains an additional partial_sum on each node.
 * Search is possible both through vids and through a size offset (binary searched for in partial_sums).
 *
 * Thread safety guarantees:
 *
 * There may be a single thread writing to the RBTree at any time.
 * Write access MUST be sequentially ordered.  to do: add a write lock to enforce this.
 *
 * Writing may occur concurrently with multiple threads reading. In this case, every call to the RBTree
 * presents a consistent (valid) state, but with a best-effort presentation of write results to readers.
 *
 * Template types:
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

  /** finds the entry for key, or inserts a new empty entry. */
  ValueType& operator[](const KeyType& key) {
    return FindOrInsert(key, /* add_size = */ 0);
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

  void Print() {
    PrintTree(root_);
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

    Node(): partial_sum(0), own_size(0), value(), parent(), left(nullptr), right(nullptr) {}

  private:
    std::weak_ptr<Node> parent; /** only used for modifying access, hence we don't need reference counting (and also break reference cycles here) */
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
      n = n->parent.lock();
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


  // debug only
  template<typename Func>
  void WalkNodePre(std::weak_ptr<Node> n, Func func, size_t depth = 0) {
    const Node *node = n.lock().get();
    func(n, depth);
    if (n.lock() != nil_ && (node->left != nil_ || node->right != nil_)) {
      WalkNodePre(node->left, func, depth + 1);
      WalkNodePre(node->right, func, depth + 1);
    }
  }

  // debug only
  void PrintTree(std::weak_ptr<Node> par) {
    std::cerr << " PrintTree parent.use_count()=" << par.use_count() << std::endl;

    //func(key, value)
    std::shared_ptr<Node> nil = nil_;
    std::cerr << std::endl;
    WalkNodePre(root_, [&nil](std::weak_ptr<Node> n, size_t depth) {
      std::stringstream ss; for(size_t i = 0; i < depth; i++) ss << " ";
      if(n.lock() == nil) {
        std::cerr << ss.str() << "nil_" << std::endl;
        return;
      }
      // there is a stacked order of locking/unlocking if you do this (multiple locks are acquired and released after the statement):
      //std::cerr << ss.str() << "vid=" << n.lock()->key << " (s use_count=" << (n.lock().use_count() - 1) << ", w use_count=" << n.use_count() << ") " << n.lock().get() << std::endl;
      std::shared_ptr<Node> node = n.lock();
      //std::cerr << ss.str() << "vid=" << node->key << " (s use_count=" << (node.use_count() - 1) << ") " << node.get() << std::endl;
      std::cerr << ss.str() << "vid=" << node->key << " (partial_sum=" << node->partial_sum << " own_size=" << node->own_size << ") " << node.get() << std::endl;
    });
    std::cerr << std::endl;
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
    return node->parent.lock()->left == node;
  }
  inline bool IsRightChild(const std::shared_ptr<Node> node) const {
    return node->parent.lock()->right == node;
  }
  inline void SetLeft(std::shared_ptr<Node> node, std::shared_ptr<Node> child) {
    assert(!IsNil(node));
    if (!IsNil(child))
      child->parent = node;
    node->left = child;
  }
  inline void SetRight(std::shared_ptr<Node> node, std::shared_ptr<Node> child) {
    assert(!IsNil(node));
    if (!IsNil(child))
      child->parent = node;
    node->right = child;
  }
  inline std::shared_ptr<Node> GetSibling(const std::shared_ptr<Node> node) const {
    if (IsLeftChild(node))
      return node->parent.lock()->right;
    else if (IsRightChild(node))
      return node->parent.lock()->left;
    assert(false);
    return nullptr;
  }
  inline std::shared_ptr<Node> ReplaceChild(std::shared_ptr<Node> child, std::shared_ptr<Node> new_child) {
    std::shared_ptr<Node> parent = child->parent.lock();
    if (IsNil(parent)) {
      new_child->parent = nil_;
      root_ = new_child;
    } else if (IsLeftChild(child)) {
      SetLeft(parent, new_child);
    } else if (IsRightChild(child)) {
      SetRight(parent, new_child);
    } else { assert(false); }
    return new_child;
  }

  /** See comments on RightRotate() */
  inline std::shared_ptr<Node> LeftRotate(std::shared_ptr<Node> node, std::shared_ptr<Node> keepRef) {
    assert(node != nil_ && node->right != nil_);
    std::shared_ptr<Node> child = node->right;

    // Node(parent, left, right, color, key)
    std::shared_ptr<Node> p = std::make_shared<Node>(nil_ /* set below */, node->left, child->left, child->color, node->key); p->value = node->value;
    std::shared_ptr<Node> q = std::make_shared<Node>(node, p, child->right, node->color, child->key); q->value = child->value;
    p->parent = q;
    q->own_size = child->own_size;
    q->partial_sum = node->partial_sum;
    p->own_size = node->own_size;
    p->partial_sum = p->own_size + p->left->partial_sum + p->right->partial_sum;
    ReplaceChild(node, q);

    // keep the reference to the node just inserted
    if(node == keepRef) {
      *node = *p;
      ReplaceChild(p, node);
      p = node;
    }
    if(child == keepRef) {
      *child = *q;
      ReplaceChild(q, child);
      q = child;
      p->parent = q;
    }

    // before, (a, b, c) nodes still have their old parents.
    if(p->left != nil_) p->left->parent = p;
    if(p->right != nil_) p->right->parent = p;
    if(q->right != nil_) q->right->parent = q;

    return q;
  }
  /**
   * LeftRotate() and RightRotate() need to allocate two new nodes for P and Q, make them valid,
   * then swap them.
   * Since delete of the two old nodes depends on their usage from reading threads, we must use
   * shared_ptr to do thread-safe atomic reference counting and release memory when appropriate.
   * Note that the newly inserted node, keepRef, is kept in the tree structure, for Put() reference re-use.
   */
  inline std::shared_ptr<Node> RightRotate(std::shared_ptr<Node> node, std::shared_ptr<Node> keepRef) {
    assert(node != nil_ && node->left != nil_);
    std::shared_ptr<Node> child = node->left;

    // p, q: see picture of nodes at https://en.wikipedia.org/wiki/Tree_rotation#Illustration
    //
    // for thread safety, we first build the right-rotated tree fragment separately,
    // and then swap it in in a valid state.

    // Node(parent, left, right, color, key)
    std::shared_ptr<Node> q = std::make_shared<Node>(nil_ /* set below */, child->right, node->right, child->color, node->key); q->value = node->value;
    std::shared_ptr<Node> p = std::make_shared<Node>(node, child->left, q, node->color, child->key); p->value = child->value;
    q->parent = p;
    p->own_size = child->own_size;
    p->partial_sum = node->partial_sum;
    q->own_size = node->own_size;
    q->partial_sum = q->own_size + q->left->partial_sum + q->right->partial_sum;
    ReplaceChild(node, p);

    // keep the reference to the node just inserted
    /*
     * As an additional complication, the Put() implementation keeps the reference to the node just inserted.
     * We could either search for that node again, or we can re-use that node's memory as we do now.
     */
    if(node == keepRef) {
      *node = *q;
      ReplaceChild(q, node);
      q = node;
    }
    if(child == keepRef) {
      *child = *p;
      ReplaceChild(p, child);
      p = child;
      q->parent = p;
    }

    // before, (a, b, c) nodes still have their old parents.
    // for thread safety: node->parent is never used by reading access, only by writes, so we are safe to update them.
    if(p->left != nil_) p->left->parent = p;
    if(q->left != nil_) q->left->parent = q;
    if(q->right != nil_) q->right->parent = q;

    return p;
    /*
     * Since delete of the two old nodes depends on their usage from reading threads, we use
     * shared_ptr to do thread-safe atomic reference counting and release memory when readers
     * have finished using them.
     */
  }
  inline std::shared_ptr<Node> ReverseRotate(std::shared_ptr<Node> node, std::shared_ptr<Node> keepRef) {
    if (IsLeftChild(node))
      return RightRotate(node->parent.lock(), keepRef);
    else if (IsRightChild(node))
      return LeftRotate(node->parent.lock(), keepRef);
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
  void FixInsert(const std::shared_ptr<Node> node);
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
  FixInsert(node); // reallocates memory for some nodes, but keeps the 'node' reference.
  // (we re-use that node's memory in FixInsert(). see LeftRotate())
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
void
RBTree<KeyType, ValueType>::FixInsert(const std::shared_ptr<Node> inserted) {
  std::shared_ptr<Node> node = inserted;

  while (!IsBlack(node) && !IsBlack(node->parent.lock())) {
    std::shared_ptr<Node> parent = node->parent.lock();
    std::shared_ptr<Node> uncle = GetSibling(parent);
    if (IsRed(uncle)) {
      SetBlack(uncle);
      SetBlack(parent);
      SetRed(parent->parent.lock());
      node = parent->parent.lock();
    } else {  // IsBlack(uncle)
      if (IsLeftChild(node) != IsLeftChild(parent))
        parent = ReverseRotate(node, /* preserve = */ inserted);
      node = ReverseRotate(parent, /* preserve = */ inserted);
    }
  }
  if (IsNil(node->parent.lock()))
    SetBlack(node);
}

template <typename KeyType, typename ValueType>
void RBTree<KeyType, ValueType>::FixRemove(std::shared_ptr<Node> target) {
  std::shared_ptr<Node> node = target;

  while (!IsRed(node) && !IsNil(node->parent.lock())) {
    std::shared_ptr<Node> sibling = GetSibling(node);
    if (IsRed(sibling)) {
      ReverseRotate(sibling, /* preserve = */ target);
      sibling = GetSibling(node);
    }
    if (IsBlack(sibling->left) && IsBlack(sibling->right)) {
      SetRed(sibling);
      node = node->parent.lock();
    } else {
      if (IsLeftChild(sibling) && !IsRed(sibling->left))
        sibling = LeftRotate(sibling, /* preserve = */ target);
      else if (IsRightChild(sibling) && !IsRed(sibling->right))
        sibling = RightRotate(sibling, /* preserve = */ target);
      ReverseRotate(sibling, /* preserve = */ target);
      node = GetSibling(node->parent.lock());
    }
  }
  SetBlack(node);
}

} // namespace sto

#endif // RBTREE_RBTREE_H_
