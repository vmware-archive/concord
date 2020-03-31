// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
/**
 * Test ReplicaStateSyncImp class.
 */

#define USE_ROCKSDB 1

#include <log4cplus/configurator.h>
#include <log4cplus/hierarchy.h>
#include <log4cplus/loggingmacros.h>
#include "blockchain/block.h"
#include "blockchain/db_adapter.h"
#include "gtest/gtest.h"
#include "replica_state_sync_imp.hpp"

#include "rocksdb/client.h"
#include "rocksdb/key_comparator.h"
#include "storage/concord_block_metadata.h"

#include <memory>

using namespace std;
using namespace log4cplus;

using concord::kvbc::ReplicaStateSyncImp;
using concord::storage::ConcordBlockMetadata;
using BlockEntry = concord::storage::blockchain::block::detail::Entry;
using BlockHeader = concord::storage::blockchain::block::detail::Header;
using concord::storage::blockchain::DBAdapter;
using concord::storage::blockchain::DBKeyComparator;
using concord::storage::blockchain::DBKeyManipulator;
using concord::storage::blockchain::IBlocksAppender;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnly;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator;
using concord::storage::rocksdb::Client;
using concord::storage::rocksdb::KeyComparator;
using concordUtils::BlockId;
using concordUtils::Key;
using concordUtils::SetOfKeyValuePairs;
using concordUtils::Sliver;
using concordUtils::Status;
using concordUtils::Value;

namespace {

std::shared_ptr<Client> dbClient;
DBAdapter *bcDBAdapter = nullptr;
Logger *logger = nullptr;
Value emptyValue;
const BlockId lastBlockId = 2;
const uint64_t lastSeqNum = 50;
const BlockId singleBlockId = 999;
const BlockId prevBlockId = lastBlockId - 1;
const BlockId prevPrevBlockId = lastBlockId - 2;
BlockId blockIdToBeRead = 0;

class MockILocalKeyValueStorageReadOnly : public ILocalKeyValueStorageReadOnly {
 public:
  Status get(const Key &key, Value &outValue) const override;
  Status get(BlockId readVersion, const Sliver &key, Sliver &outValue,
             BlockId &outBlock) const override {
    return Status::OK();
  }
  BlockId getLastBlock() const override { return lastBlockId; }
  Status getBlockData(BlockId blockId,
                      SetOfKeyValuePairs &outBlockData) const override {
    return Status::OK();
  }
  Status mayHaveConflictBetween(const Sliver &key, BlockId fromBlock,
                                BlockId toBlock, bool &outRes) const override {
    return Status::OK();
  }
  ILocalKeyValueStorageReadOnlyIterator *getSnapIterator() const override {
    return nullptr;
  }
  Status freeSnapIterator(
      ILocalKeyValueStorageReadOnlyIterator *iter) const override {
    return Status::OK();
  }
  void monitor() const override { ; }
};

class MockIBlocksAppender : public IBlocksAppender {
 public:
  Status addBlock(const SetOfKeyValuePairs &updates,
                  BlockId &outBlockId) override {
    return Status::OK();
  }
};

void fillBufAndAdvance(char *&buffer, const void *data, const size_t dataSize) {
  memcpy(buffer, data, dataSize);
  buffer += dataSize;
}

Sliver setUpBlockContent(Key key, Value blockValue) {
  BlockHeader blockHeader = {0};
  blockHeader.numberOfElements = 1;

  BlockEntry entry = {0};
  size_t sizeOfMetadata = sizeof(blockHeader) + sizeof(entry);
  entry.keySize = key.length();
  entry.valSize = blockValue.length();
  entry.keyOffset = sizeOfMetadata;
  entry.valOffset = sizeOfMetadata + key.length();

  size_t sizeOfBuf = sizeOfMetadata + key.length() + blockValue.length();
  char *buf = new char[sizeOfBuf];
  char *ptr = buf;
  fillBufAndAdvance(ptr, &blockHeader, sizeof(blockHeader));
  fillBufAndAdvance(ptr, &entry, sizeof(entry));
  fillBufAndAdvance(ptr, key.data(), key.length());
  fillBufAndAdvance(ptr, blockValue.data(), blockValue.length());

  return Sliver(buf, sizeOfBuf);
}

MockILocalKeyValueStorageReadOnly keyValueStorageMock;
MockIBlocksAppender blocksAppenderMock;
ReplicaStateSyncImp replicaStateSync(
    new ConcordBlockMetadata(keyValueStorageMock));

ConcordBlockMetadata kvbStorage(keyValueStorageMock);

const Sliver blockMetadataInternalKey = kvbStorage.getKey();

const Key lastBlockFullKey =
    DBKeyManipulator::genDataDbKey(blockMetadataInternalKey, lastBlockId);
const Value lastBlockValue = kvbStorage.serialize(lastSeqNum + 2);

const Key prevBlockFullKey =
    DBKeyManipulator::genDataDbKey(blockMetadataInternalKey, prevBlockId);
const Value prevBlockValue = kvbStorage.serialize(lastSeqNum + 1);

const Key prevPrevBlockFullKey =
    DBKeyManipulator::genDataDbKey(blockMetadataInternalKey, prevPrevBlockId);
const Value prevPrevBlockValue = kvbStorage.serialize(lastSeqNum);

const Key singleBlockValueFullKey =
    DBKeyManipulator::genDataDbKey(blockMetadataInternalKey, singleBlockId);
const Value singleBlockValue = kvbStorage.serialize(lastSeqNum);

Status MockILocalKeyValueStorageReadOnly::get(const Key &key,
                                              Value &outValue) const {
  switch (blockIdToBeRead) {
    case singleBlockId:
      outValue = singleBlockValue;
      break;
    case lastBlockId:
      outValue = lastBlockValue;
      blockIdToBeRead = prevBlockId;
      break;
    case prevBlockId:
      outValue = prevBlockValue;
      blockIdToBeRead = prevPrevBlockId;
      break;
    case prevPrevBlockId:
      outValue = prevPrevBlockValue;
      break;
    default:
      return Status::GeneralError("Block ID is out of range.");
  }
  return Status::OK();
}

TEST(replicaStateSync_test, state_in_sync) {
  blockIdToBeRead = singleBlockId;

  uint64_t removedBlocks = replicaStateSync.execute(*logger, *bcDBAdapter,
                                                    singleBlockId, lastSeqNum);
  ASSERT_EQ(removedBlocks, 0);
}

TEST(replicaStateSync_test, block_removed) {
  dbClient->put(prevPrevBlockFullKey, prevPrevBlockValue);
  dbClient->put(prevBlockFullKey, prevBlockValue);
  dbClient->put(lastBlockFullKey, lastBlockValue);

  Sliver prevPrevBlockDbKey = DBKeyManipulator::genBlockDbKey(prevPrevBlockId);
  Sliver prevBlockDbKey = DBKeyManipulator::genBlockDbKey(prevBlockId);
  Sliver lastBlockDbKey = DBKeyManipulator::genBlockDbKey(lastBlockId);

  dbClient->put(prevPrevBlockDbKey,
                setUpBlockContent(prevPrevBlockFullKey, prevPrevBlockValue));
  dbClient->put(prevBlockDbKey,
                setUpBlockContent(prevBlockFullKey, prevBlockValue));
  dbClient->put(lastBlockDbKey,
                setUpBlockContent(lastBlockFullKey, lastBlockValue));

  blockIdToBeRead = lastBlockId;
  uint64_t removedBlocks =
      replicaStateSync.execute(*logger, *bcDBAdapter, lastBlockId, lastSeqNum);

  ASSERT_EQ(removedBlocks, 2);
}

}  // end namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger = new Logger(Logger::getInstance("com.vmware.test"));
  initialize();
  Hierarchy &hierarchy = Logger::getDefaultHierarchy();
  hierarchy.disableDebug();
  BasicConfigurator config(hierarchy, false);
  config.configure();
  const string dbPath = "./replicaStateSync_test";
  dbClient = std::make_shared<Client>(dbPath,
                                      new KeyComparator(new DBKeyComparator()));
  bcDBAdapter = new DBAdapter(dbClient);

  int res = RUN_ALL_TESTS();

  delete bcDBAdapter;
  return res;
}
