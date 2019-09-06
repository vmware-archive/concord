// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Hash functions for our Sliver and KeyValuePair types.

#ifndef CONCORD_CONSENSUS_HASH_DEFS_H_
#define CONCORD_CONSENSUS_HASH_DEFS_H_

#include <stdlib.h>
#include "consensus/sliver.hpp"
#include "storage/blockchain_db_types.h"

using concord::consensus::Sliver;
using concord::storage::KeyValuePair;

// TODO(GG): do we want this hash function ? See also
// http://www.cse.yorku.ca/~oz/hash.html
inline size_t simpleHash(const uint8_t *data, const size_t len) {
  size_t hash = 5381;
  size_t t;

  for (size_t i = 0; i < len; i++) {
    t = data[i];
    hash = ((hash << 5) + hash) + t;
  }

  return hash;
}

namespace std {
template <>
struct hash<Sliver> {
  typedef Sliver argument_type;
  typedef std::size_t result_type;

  result_type operator()(const Sliver &t) const {
    return simpleHash(t.data(), t.length());
  }
};

template <>
struct hash<KeyValuePair> {
  typedef KeyValuePair argument_type;
  typedef std::size_t result_type;

  result_type operator()(const KeyValuePair &t) const {
    size_t keyHash = simpleHash(t.first.data(), t.first.length());
    return keyHash;
  }
};
}  // namespace std

#endif  // CONCORD_CONSENSUS_HASH_DEFS_H_
