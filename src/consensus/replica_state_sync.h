// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Interface for replica state synchronisation.

#ifndef CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H
#define CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H

#include <log4cplus/loggingmacros.h>
#include "storage/blockchain_db_adapter.h"
#include "storage/blockchain_db_types.h"

namespace concord {
namespace consensus {

class ReplicaStateSync {
 public:
  virtual ~ReplicaStateSync() = default;

  // Synchronises replica state and returns a number of deleted blocks.
  virtual uint64_t execute(log4cplus::Logger &logger,
                           concord::storage::BlockchainDBAdapter &bcDBAdapter,
                           concord::storage::ILocalKeyValueStorageReadOnly &kvs,
                           concord::storage::BlockId lastReachableBlockId,
                           uint64_t lastExecutedSeqNum) = 0;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_KVB_REPLICA_STATE_SYNC_H
