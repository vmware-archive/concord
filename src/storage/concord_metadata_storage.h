// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

//
// Wrapper used by concord::consensus::ConcordCommandsHandler to store BFT
// metadata (sequence number).

#ifndef CONSENSUS_CONCORD_METADATA_STORAGE_HPP_
#define CONSENSUS_CONCORD_METADATA_STORAGE_HPP_

#include <log4cplus/logger.h>
#include <exception>
#include <string>

#include "blockchain/db_interfaces.h"
#include "concord_storage.pb.h"

namespace concord {
namespace storage {

const int64_t kBlockMetadataVersion = 1;
const uint8_t kBlockMetadataKey = 0x21;

class ConcordStorageException : public std::exception {
 public:
  explicit ConcordStorageException(const std::string &what) : msg_(what) {}

  const char *what() const noexcept override { return msg_.c_str(); }

 private:
  std::string msg_;
};

class ConcordMetadataStorage {
 private:
  log4cplus::Logger logger_;
  const concord::storage::blockchain::ILocalKeyValueStorageReadOnly &storage_;
  const Sliver block_metadata_key_;
  com::vmware::concord::kvb::BlockMetadata block_metadata_;

 public:
  ConcordMetadataStorage(
      const concord::storage::blockchain::ILocalKeyValueStorageReadOnly
          &storage)
      : logger_(log4cplus::Logger::getInstance(
            "concord.consensus.ConcordMetadataStorage")),
        storage_(storage),
        block_metadata_key_(new uint8_t[1]{kBlockMetadataKey}, 1) {}

  Sliver BlockMetadataKey() const { return block_metadata_key_; }

  uint64_t GetBlockMetadata(Sliver &key);

  Sliver SerializeBlockMetadata(uint64_t bft_sequence_num);
};

}  // namespace storage
}  // namespace concord

#endif  // CONSENSUS_CONCORD_METADATA_STORAGE_HPP_
