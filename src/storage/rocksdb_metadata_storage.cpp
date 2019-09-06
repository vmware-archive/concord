// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
// This product is licensed to you under the Apache 2.0 license (the
// "License").  You may not use this product except in compliance with the
// Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#include "rocksdb_metadata_storage.h"

#include <exception>
#include "consensus/hash_defs.h"
#include "storage/blockchain_db_adapter.h"

using namespace std;

using concord::consensus::Status;

namespace concord {
namespace storage {

void RocksDBMetadataStorage::verifyOperation(uint32_t objectId,
                                             uint32_t dataLen,
                                             const char *buffer,
                                             bool writeOperation) const {
  auto elem = objectIdToSizeMap_.find(objectId);
  bool found = (elem != objectIdToSizeMap_.end());
  if (!dataLen || !buffer || !found || !objectId) {
    LOG4CPLUS_ERROR(logger_, WRONG_PARAMETER);
    throw runtime_error(WRONG_PARAMETER);
  }
  if (writeOperation && (dataLen > elem->second)) {
    ostringstream error;
    error << "Metadata object objectId " << objectId
          << " size is too big: given " << dataLen << ", allowed "
          << elem->second << endl;
    LOG4CPLUS_ERROR(logger_, error.str());
    throw runtime_error(error.str());
  }
}

bool RocksDBMetadataStorage::isNewStorage() {
  uint32_t outActualObjectSize;
  read(objectsNumParamId_, sizeof(objectsNum_), (char *)&objectsNum_,
       outActualObjectSize);
  return (outActualObjectSize == 0);
}

bool RocksDBMetadataStorage::initMaxSizeOfObjects(
    ObjectDesc *metadataObjectsArray, uint32_t metadataObjectsArrayLength) {
  for (uint32_t i = objectsNumParamId_ + 1; i < metadataObjectsArrayLength;
       ++i) {
    objectIdToSizeMap_[i] = metadataObjectsArray[i].maxSize;
    LOG4CPLUS_DEBUG(
        logger_, "initMaxSizeOfObjects i="
                     << i << " object data: id=" << metadataObjectsArray[i].id
                     << ", maxSize=" << metadataObjectsArray[i].maxSize);
  }
  // Metadata object with id=1 is used to indicate storage initialization state
  // (number of specified metadata objects).
  bool isNew = isNewStorage();
  if (isNew) {
    objectsNum_ = metadataObjectsArrayLength;
    atomicWrite(objectsNumParamId_, (char *)&objectsNum_, sizeof(objectsNum_));
  }
  LOG4CPLUS_DEBUG(logger_, "initMaxSizeOfObjects objectsNum_=" << objectsNum_);
  return isNew;
}

void RocksDBMetadataStorage::read(uint32_t objectId, uint32_t bufferSize,
                                  char *outBufferForObject,
                                  uint32_t &outActualObjectSize) {
  verifyOperation(objectId, bufferSize, outBufferForObject, false);
  lock_guard<mutex> lock(ioMutex_);
  Status status =
      dbClient_->get(KeyManipulator::generateMetadataKey(objectId),
                     outBufferForObject, bufferSize, outActualObjectSize);
  if (status.isNotFound()) {
    memset(outBufferForObject, 0, bufferSize);
    outActualObjectSize = 0;
    return;
  }
  if (!status.isOK()) {
    throw runtime_error("RocksDB get operation failed");
  }
}

void RocksDBMetadataStorage::atomicWrite(uint32_t objectId, char *data,
                                         uint32_t dataLength) {
  verifyOperation(objectId, dataLength, data, true);
  auto *dataCopy = new uint8_t[dataLength];
  memcpy(dataCopy, data, dataLength);
  lock_guard<mutex> lock(ioMutex_);
  Status status = dbClient_->put(KeyManipulator::generateMetadataKey(objectId),
                                 Sliver(dataCopy, dataLength));
  if (!status.isOK()) {
    throw runtime_error("RocksDB put operation failed");
  }
}

void RocksDBMetadataStorage::beginAtomicWriteOnlyTransaction() {
  LOG4CPLUS_DEBUG(logger_, "Begin atomic transaction");
  lock_guard<mutex> lock(ioMutex_);
  if (transaction_) {
    LOG4CPLUS_INFO(logger_, "Transaction has been opened before; ignoring");
    return;
  }
  transaction_ = new SetOfKeyValuePairs;
}

void RocksDBMetadataStorage::writeInTransaction(uint32_t objectId, char *data,
                                                uint32_t dataLength) {
  LOG4CPLUS_DEBUG(logger_,
                  "objectId=" << objectId << ", dataLength=" << dataLength);
  verifyOperation(objectId, dataLength, data, true);
  auto *dataCopy = new uint8_t[dataLength];
  memcpy(dataCopy, data, dataLength);
  lock_guard<mutex> lock(ioMutex_);
  if (!transaction_) {
    LOG4CPLUS_ERROR(logger_, WRONG_FLOW);
    throw runtime_error(WRONG_FLOW);
  }
  transaction_->insert(
      KeyValuePair(KeyManipulator::generateMetadataKey(objectId),
                   Sliver(dataCopy, dataLength)));
}

void RocksDBMetadataStorage::commitAtomicWriteOnlyTransaction() {
  LOG4CPLUS_DEBUG(logger_, "Commit atomic transaction");
  lock_guard<mutex> lock(ioMutex_);
  if (!transaction_) {
    LOG4CPLUS_ERROR(logger_, WRONG_FLOW);
    throw runtime_error(WRONG_FLOW);
  }
  Status status = dbClient_->multiPut(*transaction_);
  if (!status.isOK()) {
    throw runtime_error("RocksDB multiPut operation failed");
  }
  delete transaction_;
  transaction_ = nullptr;
}

Status RocksDBMetadataStorage::multiDel(const ObjectIdsVector &objectIds) {
  size_t objectsNumber = objectIds.size();
  assert(objectsNum_ >= objectsNumber);
  LOG4CPLUS_DEBUG(logger_, "Going to perform multiple delete");
  KeysVector keysVec;
  for (size_t objectId = 0; objectId < objectsNumber; objectId++) {
    Key key = KeyManipulator::generateMetadataKey(objectId);
    keysVec.push_back(key);
    LOG4CPLUS_INFO(logger_,
                   "Deleted object id=" << objectId << ", key=" << key);
  }
  return dbClient_->multiDel(keysVec);
}

}  // namespace storage
}  // namespace concord
