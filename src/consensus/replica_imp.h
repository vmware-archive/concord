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
#include "consensus/hash_defs.h"
#include "consensus/replica_state_sync.h"
#include "storage/blockchain_db_adapter.h"
#include "storage/blockchain_interfaces.h"
#include "storage/in_memory_db_client.h"
#include "storage/rocksdb_metadata_storage.h"

namespace concord {
namespace consensus {

class ReplicaInitException : public std::exception {
 public:
  explicit ReplicaInitException(const std::string &what) : msg(what){};

  virtual const char *what() const noexcept override { return msg.c_str(); }

 private:
  std::string msg;
};

class ReplicaImp : public concord::storage::IReplica,
                   public concord::storage::ILocalKeyValueStorageReadOnly,
                   public concord::storage::IBlocksAppender {
 public:
  // concord::storage::IReplica methods
  virtual Status start() override;
  virtual Status stop() override;

  virtual RepStatus getReplicaStatus() const override;

  virtual const concord::storage::ILocalKeyValueStorageReadOnly &
  getReadOnlyStorage() override;

  virtual Status addBlockToIdleReplica(
      const concord::storage::SetOfKeyValuePairs &updates) override;

  virtual void set_command_handler(
      concord::storage::ICommandsHandler *handler) override;

  // concord::storage::ILocalKeyValueStorageReadOnly methods
  virtual Status get(Sliver key, Sliver &outValue) const override;

  virtual Status get(concord::storage::BlockId readVersion, Sliver key,
                     Sliver &outValue,
                     concord::storage::BlockId &outBlock) const override;

  virtual concord::storage::BlockId getLastBlock() const override;

  virtual Status getBlockData(
      concord::storage::BlockId blockId,
      concord::storage::SetOfKeyValuePairs &outBlockData) const override;

  virtual Status mayHaveConflictBetween(Sliver key,
                                        concord::storage::BlockId fromBlock,
                                        concord::storage::BlockId toBlock,
                                        bool &outRes) const override;

  virtual concord::storage::ILocalKeyValueStorageReadOnlyIterator *
  getSnapIterator() const override;

  virtual Status freeSnapIterator(
      concord::storage::ILocalKeyValueStorageReadOnlyIterator *iter)
      const override;

  virtual void monitor() const override;

  // concord::storage::IBlocksAppender
  virtual Status addBlock(const concord::storage::SetOfKeyValuePairs &updates,
                          concord::storage::BlockId &outBlockId) override;

  bool isRunning() override {
    return (m_currentRepStatus == RepStatus::Running);
  }

  ReplicaImp(concord::storage::CommConfig &commConfig,
             concord::storage::ReplicaConsensusConfig &config,
             concord::storage::BlockchainDBAdapter *dbAdapter,
             ReplicaStateSync &replicaStateSync);
  ~ReplicaImp() override;

 protected:
  // METHODS

  Status addBlockInternal(const concord::storage::SetOfKeyValuePairs &updates,
                          concord::storage::BlockId &outBlockId);
  Status getInternal(concord::storage::BlockId readVersion, Sliver key,
                     Sliver &outValue,
                     concord::storage::BlockId &outBlock) const;
  void insertBlockInternal(concord::storage::BlockId blockId, Sliver block);
  Sliver getBlockInternal(concord::storage::BlockId blockId) const;
  concord::storage::BlockchainDBAdapter *getBcDbAdapter() const {
    return m_bcDbAdapter;
  }

 private:
  void createReplicaAndSyncState();

  // INTERNAL TYPES

  // represents <key,blockId>
  class KeyIDPair {
   public:
    const Sliver key;
    const concord::storage::BlockId blockId;

    KeyIDPair(Sliver s, concord::storage::BlockId i) : key(s), blockId(i) {}

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
      : public concord::storage::ILocalKeyValueStorageReadOnly {
   private:
    const ReplicaImp *rep;

   public:
    StorageWrapperForIdleMode(const ReplicaImp *r);

    virtual Status get(Sliver key, Sliver &outValue) const override;

    virtual Status get(concord::storage::BlockId readVersion, Sliver key,
                       Sliver &outValue,
                       concord::storage::BlockId &outBlock) const override;
    virtual concord::storage::BlockId getLastBlock() const override;

    virtual Status getBlockData(
        concord::storage::BlockId blockId,
        concord::storage::SetOfKeyValuePairs &outBlockData) const override;

    virtual Status mayHaveConflictBetween(Sliver key,
                                          concord::storage::BlockId fromBlock,
                                          concord::storage::BlockId toBlock,
                                          bool &outRes) const override;

    virtual concord::storage::ILocalKeyValueStorageReadOnlyIterator *
    getSnapIterator() const override;

    virtual Status freeSnapIterator(
        concord::storage::ILocalKeyValueStorageReadOnlyIterator *iter)
        const override;
    virtual void monitor() const override;
  };

  class StorageIterator
      : public concord::storage::ILocalKeyValueStorageReadOnlyIterator {
   private:
    log4cplus::Logger logger;
    const ReplicaImp *rep;
    concord::storage::BlockId readVersion;
    concord::storage::KeyValuePair m_current;
    concord::storage::BlockId m_currentBlock;
    bool m_isEnd;
    concord::storage::IDBClient::IDBClientIterator *m_iter;

   public:
    StorageIterator(const ReplicaImp *r);
    virtual ~StorageIterator() {
      // allocated by calls to rep::...::getIterator
      delete m_iter;
    }

    virtual void setReadVersion(concord::storage::BlockId _readVersion) {
      readVersion = _readVersion;
    }

    virtual concord::storage::KeyValuePair first(
        concord::storage::BlockId readVersion,
        concord::storage::BlockId &actualVersion, bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concord::storage::KeyValuePair first() override {
      concord::storage::BlockId block = m_currentBlock;
      concord::storage::BlockId dummy;
      bool dummy2;
      return first(block, dummy, dummy2);
    }

    // Assumes lexicographical ordering of the keys, seek the first element
    // k >= key
    virtual concord::storage::KeyValuePair seekAtLeast(
        concord::storage::BlockId readVersion, concord::storage::Key key,
        concord::storage::BlockId &actualVersion, bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concord::storage::KeyValuePair seekAtLeast(
        concord::storage::Key key) override {
      concord::storage::BlockId block = m_currentBlock;
      concord::storage::BlockId dummy;
      bool dummy2;
      return seekAtLeast(block, key, dummy, dummy2);
    }

    // Proceed to next element and return it
    virtual concord::storage::KeyValuePair next(
        concord::storage::BlockId readVersion, concord::storage::Key key,
        concord::storage::BlockId &actualVersion, bool &isEnd) override;

    // TODO(SG): Not implemented originally!
    virtual concord::storage::KeyValuePair next() override {
      concord::storage::BlockId block = m_currentBlock;
      concord::storage::BlockId dummy;
      bool dummy2;
      return next(block, getCurrent().first, dummy, dummy2);
    }

    // Return current element without moving
    virtual concord::storage::KeyValuePair getCurrent() override;

    virtual bool isEnd() override;
    virtual Status freeInternalIterator();
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
    std::atomic<concord::storage::BlockId> m_lastReachableBlock{0};

    friend class ReplicaImp;
  };

  // DATA

 private:
  log4cplus::Logger logger;
  RepStatus m_currentRepStatus;
  StorageWrapperForIdleMode m_InternalStorageWrapperForIdleMode;

  // storage - TODO(GG): add support for leveldb/rocksdb
  concord::storage::BlockchainDBAdapter *m_bcDbAdapter = nullptr;
  concord::storage::BlockId m_lastBlock = 0;
  bftEngine::ICommunication *m_ptrComm = nullptr;
  bftEngine::ReplicaConfig m_replicaConfig;
  bftEngine::Replica *m_replicaPtr = nullptr;
  concord::storage::ICommandsHandler *m_cmdHandler = nullptr;
  bftEngine::IStateTransfer *m_stateTransfer = nullptr;
  BlockchainAppState *m_appState = nullptr;
  concord::storage::RocksDBMetadataStorage *m_metadataStorage = nullptr;
  ReplicaStateSync &m_replicaStateSync;

  // static methods
  static Sliver createBlockFromUpdates(
      const concord::storage::SetOfKeyValuePairs &updates,
      concord::storage::SetOfKeyValuePairs &outUpdatesInNewBlock,
      bftEngine::SimpleBlockchainStateTransfer::StateTransferDigest
          &parentDigest);
  static concord::storage::SetOfKeyValuePairs fetchBlockData(Sliver block);
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_REPLICA_IMP_H_
