/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2016 Translated SRL                *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unordered_set>
#include <algorithm>
#include <sstream>

#include "StreamVersions.h"

namespace sto {

seqid_t StreamVersions::at(stream_t stream) const {
  auto it = versions_.find(stream);
  if(it != versions_.end())
    return it->second;
  else
    return is_max_ ? static_cast<seqid_t>(-1) : 0;
}

bool StreamVersions::Update(sto_updateid_t addition) {
  if(addition.sentence_id > at(addition.stream_id)) {
    (*this)[addition.stream_id] = addition.sentence_id;
    return true;
  }
  return false;
}

bool StreamVersions::Update(StreamVersions additions) {
  bool changes = false;
  for(auto entry : additions)
    changes = changes || Update(sto_updateid_t{entry.first, entry.second});
  return changes;
}

StreamVersions StreamVersions::Min(StreamVersions a, StreamVersions b) {
  StreamVersions min;
  std::unordered_set<stream_t> keys;

  for(auto e : a)
    keys.insert(e.first);
  for(auto e : b)
    keys.insert(e.first);

  for(auto key : keys)
    min.Update(sto_updateid_t{key, std::min(a.at(key), b.at(key))});

  return min;
}

bool StreamVersions::operator==(const StreamVersions &other) const {
  std::unordered_set<stream_t> keys;

  for(auto e : (*this))
    keys.insert(e.first);
  for(auto e : other)
    keys.insert(e.first);

  for(auto key : keys)
    if(this->at(key) != other.at(key))
      return false;

  return true;
}

std::string StreamVersions::DebugStr() const {
  std::stringstream ss;
  ss << "StreamVersions(";
  for(auto e : (*this))
    ss << " " << static_cast<uint32_t>(e.first) << "=" << static_cast<uint64_t>(e.second);
  ss << ")";
  return ss.str();
}

} // namespace sto
