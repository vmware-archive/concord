// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "replica_state_sync_imp.hpp"

#include "storage/concord_metadata_storage.h"

using concord::storage::BlockchainDBAdapter;
using concord::storage::BlockId;
using concord::storage::ConcordMetadataStorage;
using concord::storage::ILocalKeyValueStorageReadOnly;
using concord::storage::Key;
using concord::storage::KeyManipulator;

namespace concord {
namespace consensus {

uint64_t ReplicaStateSyncImp::execute(log4cplus::Logger &logger,
                                      BlockchainDBAdapter &bcDBAdapter,
                                      ILocalKeyValueStorageReadOnly &kvs,
                                      BlockId lastReachableBlockId,
                                      uint64_t lastExecutedSeqNum) {
  ConcordMetadataStorage roKvs(kvs);
  BlockId blockId = lastReachableBlockId;
  uint64_t blockSeqNum = 0;
  uint64_t removedBlocksNum = 0;
  Key key = roKvs.BlockMetadataKey();
  do {
    blockSeqNum = roKvs.GetBlockMetadata(key);
    LOG4CPLUS_DEBUG(
        logger, "Block Metadata key = " << key << ", blockId = " << blockId
                                        << ", blockSeqNum = " << blockSeqNum);
    if (blockSeqNum <= lastExecutedSeqNum) {
      LOG4CPLUS_INFO(logger, "Replica state is in sync; removedBlocksNum is "
                                 << removedBlocksNum);
      return removedBlocksNum;
    }
    // SBFT State Metadata is not in sync with SBFT State.
    // Remove blocks which sequence number is greater than lastExecutedSeqNum.
    bcDBAdapter.deleteBlockAndItsKeys(blockId--);
    ++removedBlocksNum;
  } while (blockId);
  return removedBlocksNum;
}

}  // namespace consensus
}  // namespace concord
