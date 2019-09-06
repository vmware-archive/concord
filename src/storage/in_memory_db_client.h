// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Objects of InMemoryDBClientIterator contain an iterator for the in memory
// object store (implemented as a map) along with a pointer to the map.
//
// Objects of InMemoryDBClient are implementations of an in memory database
// (implemented as a map).
//
// The map contains key value pairs of the type KeyValuePair. Keys and values
// are of type Sliver.

#ifndef CONCORD_STORAGE_IN_MEMORY_DB_CLIENT_H_
#define CONCORD_STORAGE_IN_MEMORY_DB_CLIENT_H_

#include <log4cplus/loggingmacros.h>
#include <map>
#include "storage/database_interface.h"

namespace concord {
namespace storage {

class InMemoryDBClient;

typedef std::map<concord::consensus::Sliver, concord::consensus::Sliver,
                 IDBClient::KeyComparator>
    TKVStore;

class InMemoryDBClientIterator : public IDBClient::IDBClientIterator {
  friend class InMemoryDBClient;

 public:
  InMemoryDBClientIterator(InMemoryDBClient *_parentClient)
      : logger(log4cplus::Logger::getInstance("com.vmware.concord.kvb")),
        m_parentClient(_parentClient) {}
  virtual ~InMemoryDBClientIterator() {}

  // Inherited via IDBClientIterator
  virtual KeyValuePair first() override;
  virtual KeyValuePair seekAtLeast(Sliver _searchKey) override;
  virtual KeyValuePair previous() override;
  virtual KeyValuePair next() override;
  virtual KeyValuePair getCurrent() override;
  virtual bool isEnd() override;
  virtual concord::consensus::Status getStatus() override;

 private:
  log4cplus::Logger logger;

  // Pointer to the InMemoryDBClient.
  InMemoryDBClient *m_parentClient;

  // Current iterator inside the map.
  TKVStore::const_iterator m_current;
};

// In-memory IO operations below are not thread-safe.
// get/put/del/multiGet/multiPut/multiDel operations are not synchronized and
// not guarded by locks. The caller is expected to use those APIs via a
// single thread.
class InMemoryDBClient : public IDBClient {
 public:
  InMemoryDBClient(KeyComparator comp) { setComparator(comp); }

  virtual Status init(bool readOnly) override;
  virtual Status get(Sliver _key, OUT Sliver &_outValue) const override;
  Status get(Sliver _key, OUT char *&buf, uint32_t bufSize,
             OUT uint32_t &_size) const override;
  virtual IDBClientIterator *getIterator() const override;
  virtual concord::consensus::Status freeIterator(
      IDBClientIterator *_iter) const override;
  virtual concord::consensus::Status put(Sliver _key, Sliver _value) override;
  virtual concord::consensus::Status del(Sliver _key) override;
  concord::consensus::Status multiGet(const KeysVector &_keysVec,
                                      OUT ValuesVector &_valuesVec) override;
  concord::consensus::Status multiPut(
      const SetOfKeyValuePairs &_keyValueMap) override;
  concord::consensus::Status multiDel(const KeysVector &_keysVec) override;
  virtual void monitor() const override{};
  bool isNew() override { return true; }

  TKVStore &getMap() { return map; }
  void setComparator(KeyComparator comp) { map = TKVStore(comp); }

 private:
  // map that stores the in memory database.
  TKVStore map;
};

}  // namespace storage
}  // namespace concord

#endif  // CONCORD_STORAGE_IN_MEMORY_DB_CLIENT_H_
