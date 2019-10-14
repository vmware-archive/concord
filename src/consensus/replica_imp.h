// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KV Blockchain replica definition.

#ifndef CONCORD_CONSENSUS_REPLICA_IMP_H_
#define CONCORD_CONSENSUS_REPLICA_IMP_H_

#include <functional>
#include <map>
#include <string>

#include <log4cplus/loggingmacros.h>
#include "CommFactory.hpp"
#include "ICommunication.hpp"
#include "Replica.hpp"
#include "ReplicaConfig.hpp"
#include "StatusInfo.h"
#include "blockchain/db_adapter.h"
#include "blockchain/db_interfaces.h"
#include "communication.h"
#include "consensus/replica_state_sync.h"
#include "hash_defs.h"
#include "memorydb/client.h"
#include "replica_interface.h"
#include "sliver.hpp"
#include "status.hpp"
#include "storage/db_metadata_storage.h"

namespace concord {
namespace consensus {

class ReplicaInitException : public std::exception {
 public:
  explicit ReplicaInitException(const std::string &what) : msg(what){};

  virtual const char *what() const noexcept override { return msg.c_str(); }

 private:
  std::string msg;
};

class ReplicaImp
    : public IReplica,
      public concord::storage::blockchain::ILocalKeyValueStorageReadOnly,
      public concord::storage::blockchain::IBlocksAppender {
 public:
  // concord::storage::IReplica methods
  virtual concordUtils::Status start() override;
  virtual concordUtils::Status stop() override;

  virtual RepStatus getReplicaStatus() const override;

  virtual const concord::storage::blockchain::ILocalKeyValueStorageReadOnly &
  getReadOnlyStorage() override;

  virtual concordUtils::Status addBlockToIdleReplica(
      const concord::storage::SetOfKeyValuePairs &updates) override;

  virtual void set_command_handler(ICommandsHandler *handler) override;

  // concord::storage::blockchain::ILocalKeyValueStorageReadOnly methods
  virtual concordUtils::Status get(
      const concordUtils::Key &key,
      concordUtils::Value &outValue) const override;

  virtual concordUtils::Status get(
      concordUtils::BlockId readVersion, const concordUtils::Sliver &key,
      concordUtils::Sliver &outValue,
      concordUtils::BlockId &outBlock) const override;

  virtual concordUtils::BlockId getLastBlock() const override;

  virtual concordUtils::Status getBlockData(
      concordUtils::BlockId blockId,
      concord::storage::SetOfKeyValuePairs &outBlockData) const override;

  virtual concordUtils::Status mayHaveConflictBetween(
      const concordUtils::Sliver &key, concordUtils::BlockId fromBlock,
      concordUtils::BlockId toBlock, bool &outRes) const override;

  virtual concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator *
  getSnapIterator() const override;

  virtual concordUtils::Status freeSnapIterator(
      concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator *iter)
      const override;

  virtual void monitor() const override;

  // concord::storage::IBlocksAppender
  virtual concordUtils::Status addBlock(
      const concord::storage::SetOfKeyValuePairs &updates,
      concordUtils::BlockId &outBlockId) override;

  bool isRunning() override {
    return (m_currentRepStatus == RepStatus::Running);
  }

  ReplicaImp(CommConfig &commConfig, ReplicaConsensusConfig &config,
             concord::storage::blockchain::DBAdapter *dbAdapter,
             ReplicaStateSync &replicaStateSync);
  ~ReplicaImp() override;

 protected:
  // METHODS

  concordUtils::Status addBlockInternal(
      const concord::storage::SetOfKeyValuePairs &updates,
      concordUtils::BlockId &outBlockId);
  concordUtils::Status getInternal(concordUtils::BlockId readVersion,
                                   concordUtils::Sliver key,
                                   concordUtils::Sliver &outValue,
                                   concordUtils::BlockId &outBlock) const;
  void insertBlockInternal(concordUtils::BlockId blockId,
                           concordUtils::Sliver block);
  concordUtils::Sliver getBlockInternal(concordUtils::BlockId blockId) const;
  concord::storage::blockchain::DBAdapter *getBcDbAdapter() const {
    return m_bcDbAdapter;
  }

 private:
  void createReplicaAndSyncState();

  // INTERNAL TYPES

  // represents <key,blockId>
  class KeyIDPair {
   public:
    const concordUtils::Sliver key;
    const concordUtils::BlockId blockId;

    KeyIDPair(concordUtils::Sliver s, concordUtils::BlockId i)
        : key(s), blockId(i) {}

    bool operator<(const KeyIDPair &k) const {
      int c = this->key.compare(k.key);
      if (c == 0) {
        return this->blockId > k.blockId;
      } else {
        return c < 0;
      }
    }

    bool operator==(const KeyIDPair &k) const {
      if (this->blockId != k.blockId) {
        return false;
      }
      return (this->key.compare(k.key) == 0);
    }
  };

  // TODO(GG): do we want synchronization here ?
  class StorageWrapperForIdleMode
      : public concord::storage::blockchain::ILocalKeyValueStorageReadOnly {
   private:
    const ReplicaImp *rep;

   public:
    StorageWrapperForIdleMode(const ReplicaImp *r);

    virtual concordUtils::Status get(
        const concordUtils::Key &key,
        concordUtils::Value &outValue) const override;

    virtual concordUtils::Status get(
        concordUtils::BlockId readVersion, const concordUtils::Sliver &key,
        concordUtils::Sliver &outValue,
        concordUtils::BlockId &outBlock) const override;
    virtual concordUtils::BlockId getLastBlock() const override;

    virtual concordUtils::Status getBlockData(
        concordUtils::BlockId blockId,
        concord::storage::SetOfKeyValuePairs &outBlockData) const override;

    virtual concordUtils::Status mayHaveConflictBetween(
        const concordUtils::Sliver &key, concordUtils::BlockId fromBlock,
        concordUtils::BlockId toBlock, bool &outRes) const override;

    virtual concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator
        *
        getSnapIterator() const override;

    virtual concordUtils::Status freeSnapIterator(
        concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator
            *iter) const override;
    virtual void monitor() const override;
  };

  class StorageIterator : public concord::storage::blockchain::
                              ILocalKeyValueStorageReadOnlyIterator {
   private:
    log4cplus::Logger logger;
    const ReplicaImp *rep;
    concordUtils::BlockId readVersion;
    concordUtils::KeyValuePair m_current;
    concordUtils::BlockId m_currentBlock;
    bool m_isEnd;
    concord::storage::IDBClient::IDBClientIterator *m_iter;

   public:
    StorageIterator(const ReplicaImp *r);
    virtual ~StorageIterator() {
      // allocated by calls to rep::...::getIterator
      delete m_iter;
    }

    virtual void setReadVersion(concordUtils::BlockId _readVersion) {
      readVersion = _readVersion;
    }

    virtual concordUtils::KeyValuePair first(
        concordUtils::BlockId readVersion, concordUtils::BlockId &actualVersion,
        bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concordUtils::KeyValuePair first() override {
      concordUtils::BlockId block = m_currentBlock;
      concordUtils::BlockId dummy;
      bool dummy2;
      return first(block, dummy, dummy2);
    }

    // Assumes lexicographical ordering of the keys, seek the first element
    // k >= key
    virtual concordUtils::KeyValuePair seekAtLeast(
        concordUtils::BlockId readVersion, const concordUtils::Key &key,
        concordUtils::BlockId &actualVersion, bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concordUtils::KeyValuePair seekAtLeast(
        const concordUtils::Key &key) override {
      concordUtils::BlockId block = m_currentBlock;
      concordUtils::BlockId dummy;
      bool dummy2;
      return seekAtLeast(block, key, dummy, dummy2);
    }

    // Proceed to next element and return it
    virtual concordUtils::KeyValuePair next(
        concordUtils::BlockId readVersion, const concordUtils::Key &key,
        concordUtils::BlockId &actualVersion, bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concordUtils::KeyValuePair next() override {
      concordUtils::BlockId block = m_currentBlock;
      concordUtils::BlockId dummy;
      bool dummy2;
      return next(block, getCurrent().first, dummy, dummy2);
    }

    // Return current element without moving
    virtual concordUtils::KeyValuePair getCurrent() override;

    virtual bool isEnd() override;
    virtual concordUtils::Status freeInternalIterator();
  };

  class BlockchainAppState
      : public bftEngine::SimpleBlockchainStateTransfer::IAppState {
   public:
    BlockchainAppState(ReplicaImp *const parent);

    virtual bool hasBlock(uint64_t blockId) override;
    virtual bool getBlock(uint64_t blockId, char *outBlock,
                          uint32_t *outBlockSize) override;
    virtual bool getPrevDigestFromBlock(
        uint64_t blockId,
        bftEngine::SimpleBlockchainStateTransfer::StateTransferDigest
            *outPrevBlockDigest) override;
    virtual bool putBlock(uint64_t blockId, char *block,
                          uint32_t blockSize) override;
    virtual uint64_t getLastReachableBlockNum() override;
    virtual uint64_t getLastBlockNum() override;

   private:
    ReplicaImp *const m_ptrReplicaImpl = nullptr;
    log4cplus::Logger m_logger;

    // from IAppState. represents maximal block number n such that all
    // blocks 1 <= i <= n exist
    std::atomic<concordUtils::BlockId> m_lastReachableBlock{0};

    friend class ReplicaImp;
  };

  // DATA

 private:
  log4cplus::Logger logger;
  RepStatus m_currentRepStatus;
  StorageWrapperForIdleMode m_InternalStorageWrapperForIdleMode;

  concord::storage::blockchain::DBAdapter *m_bcDbAdapter = nullptr;
  concordUtils::BlockId m_lastBlock = 0;
  bftEngine::ICommunication *m_ptrComm = nullptr;
  bftEngine::ReplicaConfig m_replicaConfig;
  bftEngine::Replica *m_replicaPtr = nullptr;
  ICommandsHandler *m_cmdHandler = nullptr;
  bftEngine::IStateTransfer *m_stateTransfer = nullptr;
  BlockchainAppState *m_appState = nullptr;
  concord::storage::DBMetadataStorage *m_metadataStorage = nullptr;
  ReplicaStateSync &m_replicaStateSync;

  // static methods
  static concordUtils::Sliver createBlockFromUpdates(
      const concord::storage::SetOfKeyValuePairs &updates,
      concord::storage::SetOfKeyValuePairs &outUpdatesInNewBlock,
      bftEngine::SimpleBlockchainStateTransfer::StateTransferDigest
          &parentDigest);
  static concord::storage::SetOfKeyValuePairs fetchBlockData(
      concordUtils::Sliver block);
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_REPLICA_IMP_H_
