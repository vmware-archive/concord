// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Storage key comparators definition.

#ifndef CONCORD_STORAGE_COMPARATORS_H_
#define CONCORD_STORAGE_COMPARATORS_H_

#include <log4cplus/loggingmacros.h>

#ifdef USE_ROCKSDB
#include "rocksdb/comparator.h"
#include "rocksdb/slice.h"
#endif
#include "consensus/sliver.hpp"

namespace concord {
namespace storage {
// Basic comparator. Decomposes storage key into parts (type, version,
// application key).

// RocksDB
#ifdef USE_ROCKSDB
class RocksKeyComparator : public rocksdb::Comparator {
 public:
  RocksKeyComparator()
      : logger(log4cplus::Logger::getInstance(
            "concord.storage.RocksKeyComparator")) {}
  int Compare(const rocksdb::Slice& _a, const rocksdb::Slice& _b) const;

  // GG: Ignore the following methods for now:
  const char* Name() const { return "RocksKeyComparator"; }
  void FindShortestSeparator(std::string*, const rocksdb::Slice&) const {}
  void FindShortSuccessor(std::string*) const {}
  static bool InMemKeyComp(const concord::consensus::Sliver& _a,
                           const concord::consensus::Sliver& _b);

 private:
  static int ComposedKeyComparison(const log4cplus::Logger& logger,
                                   const concord::consensus::Sliver& _a,
                                   const concord::consensus::Sliver& _b);

 private:
  log4cplus::Logger logger;
};
#endif

}  // namespace storage
}  // namespace concord

#endif  // CONCORD_STORAGE_COMPARATORS_H_
