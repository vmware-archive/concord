// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Wrapper used by concord::storage::ConcordCommandsHandler to store BFT
// metadata (sequence number).

#ifndef CONCORD_STORAGE_CONCORD_METADATA_STORAGE_HPP_
#define CONCORD_STORAGE_CONCORD_METADATA_STORAGE_HPP_

#include <exception>
#include <string>

#include "block_metadata.hpp"
#include "blockchain/db_interfaces.h"
#include "concord_storage.pb.h"

namespace concord {
namespace storage {
using concord::storage::blockchain::ILocalKeyValueStorageReadOnly;

class ConcordStorageException : public std::exception {
 public:
  explicit ConcordStorageException(const std::string &what) : msg_(what) {}

  const char *what() const noexcept override { return msg_.c_str(); }

 private:
  std::string msg_;
};

class ConcordBlockMetadata : public concord::kvbc::IBlockMetadata {
 public:
  ConcordBlockMetadata(const ILocalKeyValueStorageReadOnly &storage)
      : IBlockMetadata(storage) {
    logger_ = log4cplus::Logger::getInstance(
        "concord.storage.ConcordMetadataStorage");
  }
  virtual uint64_t getSequenceNum(const Sliver &key) const override;
  virtual Sliver serialize(uint64_t sequence_num) const override;

 protected:
  const int64_t kBlockMetadataVersion = 1;
};

}  // namespace storage
}  // namespace concord

#endif  // CONCORD_STORAGE_CONCORD_METADATA_STORAGE_HPP_
