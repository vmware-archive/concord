// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_CONSENSUS_REPLICA_STATE_SYNC_IMP_H_
#define CONCORD_CONSENSUS_REPLICA_STATE_SYNC_IMP_H_

#include "blockchain/db_adapter.h"
#include "blockchain/db_interfaces.h"
#include "blockchain/db_types.h"
#include "consensus/replica_state_sync.h"

namespace concord {
namespace consensus {

class ReplicaStateSyncImp : public ReplicaStateSync {
 public:
  ~ReplicaStateSyncImp() override = default;

  uint64_t execute(
      log4cplus::Logger &logger,
      concord::storage::blockchain::DBAdapter &bcDBAdapter,
      concord::storage::blockchain::ILocalKeyValueStorageReadOnly &kvs,
      concordUtils::BlockId lastReachableBlockId,
      uint64_t lastExecutedSeqNum) override;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_REPLICA_STATE_SYNC_IMP_H_
