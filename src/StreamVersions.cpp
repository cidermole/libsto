/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2016 Translated SRL                *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include <unordered_set>
#include <algorithm>

#include "StreamVersions.h"

namespace sto {

seqid_t StreamVersions::at(stream_t stream) const {
  auto it = versions_.find(stream);
  if(it != versions_.end())
    return it->second;
  else
    return 0;
}

bool StreamVersions::Update(updateid_t addition) {
  if(addition.sentence_id > at(addition.stream_id)) {
    (*this)[addition.stream_id] = addition.sentence_id;
    return true;
  }
  return false;
}

bool StreamVersions::Update(StreamVersions additions) {
  bool changes = false;
  for(auto entry : additions)
    changes = changes || Update(updateid_t{entry.first, entry.second});
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
    min.Update(updateid_t{key, std::min(a.at(key), b.at(key))});

  return min;
}

} // namespace sto
