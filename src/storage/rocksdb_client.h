// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file RocksDBClient.h
 *
 *  @brief Header file containing the RocksDBClientIterator and RocksDBClient
 *  class definitions.
 *
 *  Objects of RocksDBClientIterator contain an iterator for database along with
 *  a pointer to the client object.
 *
 *  Objects of RocksDBClient signify connections with RocksDB database. They
 *  contain variables for storing the database directory path, connection object
 *  and comparator.
 *
 */

#ifndef CONCORD_STORAGE_ROCKSDB_CLIENT_H_
#define CONCORD_STORAGE_ROCKSDB_CLIENT_H_

#ifdef USE_ROCKSDB
#include <log4cplus/loggingmacros.h>
#include "rocksdb/db.h"
#include "storage/blockchain_db_types.h"
#include "storage/database_interface.h"

namespace concord {
namespace storage {

class RocksDBClient;

class RocksDBClientIterator
    : public concord::storage::IDBClient::IDBClientIterator {
  friend class RocksDBClient;

 public:
  RocksDBClientIterator(const RocksDBClient *_parentClient);
  ~RocksDBClientIterator() { delete m_iter; }

  // Inherited via IDBClientIterator
  concord::storage::KeyValuePair first() override;
  concord::storage::KeyValuePair seekAtLeast(
      concord::consensus::Sliver _searchKey) override;
  concord::storage::KeyValuePair previous() override;
  concord::storage::KeyValuePair next() override;
  concord::storage::KeyValuePair getCurrent() override;
  bool isEnd() override;
  concord::consensus::Status getStatus() override;

 private:
  log4cplus::Logger logger;

  rocksdb::Iterator *m_iter;

  // Reference to the RocksDBClient
  const RocksDBClient *m_parentClient;

  concord::consensus::Status m_status;
};

class RocksDBClient : public concord::storage::IDBClient {
 public:
  RocksDBClient(std::string _dbPath, rocksdb::Comparator *_comparator)
      : logger(log4cplus::Logger::getInstance("com.concord.vmware.kvb")),
        m_dbPath(_dbPath),
        m_comparator(_comparator) {}

  concord::consensus::Status init(bool readOnly = false) override;
  concord::consensus::Status get(
      concord::consensus::Sliver _key,
      OUT concord::consensus::Sliver &_outValue) const override;
  concord::consensus::Status get(concord::consensus::Sliver _key,
                                 OUT char *&buf, uint32_t bufSize,
                                 OUT uint32_t &_realSize) const override;
  IDBClientIterator *getIterator() const override;
  concord::consensus::Status freeIterator(
      IDBClientIterator *_iter) const override;
  concord::consensus::Status put(concord::consensus::Sliver _key,
                                 concord::consensus::Sliver _value) override;
  concord::consensus::Status del(concord::consensus::Sliver _key) override;
  concord::consensus::Status multiGet(
      const concord::storage::KeysVector &_keysVec,
      OUT concord::storage::ValuesVector &_valuesVec) override;
  concord::consensus::Status multiPut(
      const concord::storage::SetOfKeyValuePairs &_keyValueMap) override;
  concord::consensus::Status multiDel(
      const concord::storage::KeysVector &_keysVec) override;
  rocksdb::Iterator *getNewRocksDbIterator() const;
  void monitor() const override;
  bool isNew() override;

 private:
  concord::consensus::Status launchBatchJob(
      rocksdb::WriteBatch &_batchJob,
      const concord::storage::KeysVector &_keysVec);
  std::ostringstream collectKeysForPrint(
      const concord::storage::KeysVector &_keysVec);
  concord::consensus::Status get(concord::consensus::Sliver _key,
                                 OUT std::string &_value) const;

 private:
  log4cplus::Logger logger;

  // Database path on directory (used for connection).
  std::string m_dbPath;

  // Database object (created on connection).
  std::unique_ptr<rocksdb::DB> m_dbInstance;

  // Comparator object.
  rocksdb::Comparator *m_comparator;
};

rocksdb::Slice toRocksdbSlice(concord::consensus::Sliver _s);
concord::consensus::Sliver fromRocksdbSlice(rocksdb::Slice _s);
concord::consensus::Sliver copyRocksdbSlice(rocksdb::Slice _s);

}  // namespace storage
}  // namespace concord

#endif  // USE_ROCKSDB
#endif  // CONCORD_STORAGE_ROCKSDB_CLIENT_H_
