// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Interface for replica state synchronisation.

#ifndef CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H
#define CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H

#include <log4cplus/loggingmacros.h>

#include "blockchain/db_adapter.h"
#include "blockchain/db_interfaces.h"

namespace concord {
namespace consensus {

class ReplicaStateSync {
 public:
  virtual ~ReplicaStateSync() = default;

  // Synchronises replica state and returns a number of deleted blocks.
  virtual uint64_t execute(
      log4cplus::Logger &logger,
      concord::storage::blockchain::DBAdapter &bcDBAdapter,
      concord::storage::blockchain::ILocalKeyValueStorageReadOnly &kvs,
      concordUtils::BlockId lastReachableBlockId,
      uint64_t lastExecutedSeqNum) = 0;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H
