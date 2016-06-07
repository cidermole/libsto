// modified under the following, original copyright

// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef QHASHMAP_HPP
#define QHASHMAP_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>

struct QHashMapDefaultAlloc {
  void* New(size_t sz) { return operator new(sz); }
  static void Delete(void* p) { operator delete(p); }
};

template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator = QHashMapDefaultAlloc>
class QHashMap {
  QHashMap& operator=(const QHashMap&) = delete;
public:
  // The default capacity.  This is used by the call sites which want
  // to pass in a non-default AllocationPolicy but want to use the
  // default value of capacity specified by the implementation.
  static const size_t kDefaultHashMapCapacity = 8;

  // initial_capacity is the size of the initial hash map;
  // it must be a power of 2 (and thus must not be 0).
  QHashMap(size_t capacity = kDefaultHashMapCapacity,
           Allocator allocator = Allocator());
  QHashMap(const QHashMap& x, Allocator allocator = Allocator());
  ~QHashMap();

  // HashMap entries are (key, value, hash) triplets.
  // Some clients may not need to use the value slot
  // (e.g. implementers of sets, where the key is the value).
  struct Entry {
    KeyType first;
    ValueType second;
    //AuxType size;
    AuxType partial_sum; // these are in the order of entries in map_

    AuxType size() const { return second->size(); }
  };

  // If an entry with matching key is found, Lookup()
  // returns that entry. If no matching entry is found,
  // but insert is set, a new entry is inserted with
  // corresponding key, key hash, and NULL value.
  // Otherwise, NULL is returned.
  Entry* Lookup(KeyType key, bool insert, Allocator allocator = Allocator())
#if defined(__GNUC__) || defined(__llvm__) || defined(__clang__)
      __attribute__((always_inline)) // inline the function even if -Os is used
#endif
  ;

  // Removes the entry with matching key.
  // XXX no support for partial sums yet!
  void Remove(Entry* p);
  bool Remove(KeyType key);

  // Empties the hash map (occupancy() == 0).
  void Clear();

  // The number of (non-empty) entries in the table.
  size_t size() const { return occupancy_; }

  // The capacity of the table. The implementation
  // makes sure that occupancy is at most 80% of
  // the table capacity.
  size_t capacity() const { return capacity_; }

  // Iteration
  //
  // for (Entry* p = map.Start(); p != NULL; p = map.Next(p)) {
  //   ...
  // }
  //
  // If entries are inserted during iteration, the effect of
  // calling Next() is undefined.
  Entry* Start() const;
  Entry* Next(Entry* p) const;

  Entry* Prev(Entry* p) const;

  void swap(QHashMap& other) {
    std::swap(map_, other.map_);
    std::swap(capacity_, other.capacity_);
    std::swap(occupancy_, other.occupancy_);
  }

  /** update partial sums after the entry 'from' just modified/inserted. */
  void UpdatePartialSums(Entry* from);

  /**
   * like upper_bound()-1 search on a hash-ordered array of partial_sums.
   */
  Entry* FindPartialSumBound(const AuxType& val);

private:
  Entry* map_;
  size_t capacity_;
  size_t occupancy_;

  Entry* map_end() const { return map_ + capacity_; }
  Entry* Probe(KeyType key);
  void Initialize(size_t capacity, Allocator allocator = Allocator());
  void Resize(Allocator allocator = Allocator());

  /**
   * like upper_bound() search on a hash-ordered array of partial_sums.
   * note: return value may include map_end()
   */
  Entry* PartialSumUpperBound(Entry* first, Entry* last, const AuxType& val);

public:
  class iterator {
    iterator operator++(int); // disabled
  public:
    iterator& operator++() {
      entry_ = map_->Next(entry_);
      return *this;
    }

    Entry* operator*() { return entry_; }
    Entry* operator->() { return entry_; }
    bool operator==(const iterator& other) { return entry_ == other.entry_; }
    bool operator!=(const iterator& other) { return entry_ != other.entry_; }

  private:
    iterator(const QHashMap* map, Entry* entry)
        : map_(map), entry_(entry) {}

    const QHashMap* map_;
    Entry* entry_;

    friend class QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>;
  };

  iterator begin() const { return iterator(this, this->Start()); }
  iterator end() const { return iterator(this, NULL); }
  iterator find(KeyType key)
#if defined(__GNUC__) || defined(__llvm__) || defined(__clang__)
  __attribute__((always_inline)) // inline the function even if -Os is used
#endif
  {
    return iterator(this, this->Lookup(key, false));
  }
  void erase(const iterator& i) {
    Remove(i.entry_);
  }
};

template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::QHashMap(
    size_t initial_capacity, Allocator allocator) {
  Initialize(initial_capacity, allocator);
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::QHashMap(
    const QHashMap& x, Allocator allocator) {
  map_ = static_cast<Entry*>(allocator.New(x.capacity_ * sizeof(Entry)));
  capacity_ = x.capacity_;
  occupancy_ = x.occupancy_;
  std::copy(x.map_, x.map_ + x.capacity_, map_);
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::~QHashMap() {
  Allocator::Delete(map_);
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Lookup(
    KeyType key, bool insert, Allocator allocator) {
  // Find a matching entry.
  Entry* p = Probe(key);
  if (p->first != KeyTraits::null()) {
    return p;
  }

  // No entry found; insert one if necessary.
  if (insert) {
    p->first = key;
    // p->second = NULL;
    occupancy_++;

    // Grow the map if we reached >= 80% occupancy.
    if (occupancy_ + occupancy_/4 >= capacity_) {
      Resize(allocator);
      p = Probe(key);
    }

    //UpdatePartialSums(p); // TODO: we cannot immediately call back into size(), since we just inserted an Entry with nullptr TreeNode. Needs to be done externally.

    return p;
  }

  // No entry found and none inserted.
  return NULL;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline void QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Remove(
    Entry* p) {
  // To remove an entry we need to ensure that it does not create an empty
  // entry that will cause the search for another entry to stop too soon. If all
  // the entries between the entry to remove and the next empty slot have their
  // initial position inside this interval, clearing the entry to remove will
  // not break the search. If, while searching for the next empty entry, an
  // entry is encountered which does not have its initial position between the
  // entry to remove and the position looked at, then this entry can be moved to
  // the place of the entry to remove without breaking the search for it. The
  // entry made vacant by this move is now the entry to remove and the process
  // starts over.
  // Algorithm from http://en.wikipedia.org/wiki/Open_addressing.

  // This guarantees loop termination as there is at least one empty entry so
  // eventually the removed entry will have an empty entry after it.
  assert(occupancy_ < capacity_);

  // p is the candidate entry to clear. q is used to scan forwards.
  Entry* q = p;  // Start at the entry to remove.
  while (true) {
    // Move q to the next entry.
    q = q + 1;
    if (q == map_end()) {
      q = map_;
    }

    // All entries between p and q have their initial position between p and q
    // and the entry p can be cleared without breaking the search for these
    // entries.
    if (q->first == KeyTraits::null()) {
      break;
    }

    // Find the initial position for the entry at position q.
    Entry* r = map_ + (KeyTraits::hash(q->first, capacity_) & (capacity_ - 1));

    // If the entry at position q has its initial position outside the range
    // between p and q it can be moved forward to position p and will still be
    // found. There is now a new candidate entry for clearing.
    if ((q > p && (r <= p || r > q)) ||
        (q < p && (r <= p && r > q))) {
      *p = *q;
      p = q;
    }
  }

  // Clear the entry which is allowed to en emptied.
  p->first = KeyTraits::null();
  occupancy_--;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline bool QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Remove(
    KeyType key) {
  // Lookup the entry for the key to remove.
  Entry* p = Probe(key);
  if (p->first == KeyTraits::null()) {
    // Key not found nothing to remove.
    return false;
  }
  Remove(p);
  return true;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline void QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Clear() {
  // Mark all entries as empty.
  const Entry* end = map_end();
  for (Entry* p = map_; p < end; p++) {
    p->first = KeyTraits::null();
    p->partial_sum = 0; // only necessary for map_[0], but nice to have everywhere
  }
  occupancy_ = 0;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Start() const {
  return Next(map_ - 1);
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Next(Entry* p) const {
  const Entry* end = map_end();
  assert(map_ - 1 <= p && p < end);
  for (p++; p < end; p++) {
    if (p->first != KeyTraits::null()) {
      return p;
    }
  }
  return NULL;
}

template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Prev(Entry* p) const {
  const Entry* end = map_end();
  assert(map_ < p && p <= end);
  for (p--; p >= map_; p--) {
    if (p->first != KeyTraits::null()) {
      return p;
    }
  }
  return NULL;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Probe(KeyType key) {
  assert(key != KeyTraits::null());

  assert((capacity_ & (capacity_ - 1)) == 0);
  Entry* p = map_ + (KeyTraits::hash(key, capacity_) & (capacity_ - 1));
  const Entry* end = map_end();
  assert(map_ <= p && p < end);

  assert(occupancy_ < capacity_);  // Guarantees loop termination.
  while (p->first != KeyTraits::null()
         && (KeyTraits::hash(key, capacity_) != KeyTraits::hash(p->first, capacity_) ||
             ! KeyTraits::equals(key, p->first))) {
    p++;
    if (p >= end) {
      p = map_;
    }
  }

  return p;
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline void QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Initialize(
    size_t capacity, Allocator allocator) {
  assert((capacity & (capacity - 1)) == 0);
  map_ = reinterpret_cast<Entry*>(allocator.New(capacity * sizeof(Entry)));
  capacity_ = capacity;
  Clear();
}


template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
inline void QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Resize(
    Allocator allocator) {
  Entry* map = map_;
  size_t n = occupancy_;

  // Allocate larger map.
  Initialize(capacity_ * 2, allocator);

  // Rehash all current entries.
  for (Entry* p = map; n > 0; p++) {
    if (p->first != KeyTraits::null()) {
      Lookup(p->first, true)->second = p->second;
      n--;
    }
  }

  // Delete old map.
  Allocator::Delete(map);
}

/** update partial sums after the entry 'from' just modified/inserted. */
template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
void QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::UpdatePartialSums(Entry* from) {
  size_t partial_sum = from->partial_sum;

  for(Entry* p = from; p != nullptr; p = Next(p)) {
    p->partial_sum = partial_sum;
    partial_sum += p->size();
  }
}

template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::FindPartialSumBound(const AuxType& val) {
  Entry* entry = Prev(PartialSumUpperBound(map_, map_end(), val));
  assert(entry >= map_ && entry < map_end());
  return entry;
}

/** like upper_bound() search on a hash-ordered array of partial_sums. */
template<typename KeyType, typename ValueType, typename AuxType, class KeyTraits, class Allocator>
typename QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::Entry*
QHashMap<KeyType, ValueType, AuxType, KeyTraits, Allocator>::PartialSumUpperBound(Entry* first, Entry* last, const AuxType& val) {
  Entry *it, *it_old;
  size_t count, step;

  assert((map_ <= first && first < map_end()) && (map_ <= last && last <= map_end()) && (first < last)); // valid ends, non-zero size range

  // all positioning in capacity units (all map_ slots, not just occupied entries)
  count = last - first; // rough estimate

  // see reference impl of upper_bound() at http://www.cplusplus.com/reference/algorithm/upper_bound/
  while(count > 0) {
    it = first;
    step = count / 2;
    it_old = it;
    it += step; it = Prev(it+1); // rough advance(it, step)

    //it = (it == nullptr) ? map_end() : it; // guard against Next()'s implementation of end??
    assert(it != nullptr);

    step = it - it_old; // actually taken step size
    if(step == -1)
      return last; // if the range shrunk due to a consecutive hole at the end (reached into via ++it), we need to return
    if(!(val < it->partial_sum)) {
      first = ++it;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  assert(map_ <= first && first <= map_end()); // note: may include map_end()
  return first;
}

#endif  // QHASHMAP_HPP
