// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Wrapper used by concord::consensus::ConcordCommandsHandler to store BFT
// metadata (sequence number).

#include "concord_metadata_storage.h"
#include <log4cplus/loggingmacros.h>
#include "kv_types.hpp"

namespace concord {
namespace storage {

Sliver ConcordMetadataStorage::SerializeBlockMetadata(
    uint64_t bft_sequence_num) {
  block_metadata_.Clear();
  block_metadata_.set_version(kBlockMetadataVersion);
  block_metadata_.set_bft_sequence_num(bft_sequence_num);

  // It would be nice to re-use a buffer that is allocated only once, when this
  // class is instantiated. But, to many things rely on being able to hold a
  // pointer to the buffer we return, and they would all see the modification we
  // would make later.
  size_t serialized_size = block_metadata_.ByteSize();
  uint8_t* raw_buffer = new uint8_t[serialized_size];
  Sliver block_metadata_buffer(raw_buffer, serialized_size);
  block_metadata_.SerializeToArray(block_metadata_buffer.data(),
                                   block_metadata_buffer.length());
  return block_metadata_buffer;
}

uint64_t ConcordMetadataStorage::GetBlockMetadata(Sliver& key) {
  Sliver outValue;
  Status status = storage_.get(key, outValue);
  uint64_t sequenceNum = 0;
  if (status.isOK() && outValue.length() > 0) {
    com::vmware::concord::kvb::BlockMetadata blockMetadata;
    if (blockMetadata.ParseFromArray(outValue.data(), outValue.length())) {
      if (blockMetadata.version() == kBlockMetadataVersion) {
        sequenceNum = blockMetadata.bft_sequence_num();
      } else {
        LOG4CPLUS_ERROR(logger_, "Unknown block metadata version :"
                                     << blockMetadata.version());
        throw ConcordStorageException("Unknown block metadata version");
      }
    } else {
      LOG4CPLUS_ERROR(logger_, "Unable to decode block metadata" << outValue);
      throw ConcordStorageException("Corrupted block metadata");
    }
  } else {
    LOG4CPLUS_WARN(logger_, "Unable to get block or has zero-length; status = "
                                << status
                                << ", outValue.length = " << outValue.length());
  }
  LOG4CPLUS_DEBUG(logger_,
                  "key = " << key << ", sequenceNum = " << sequenceNum);
  return sequenceNum;
}

}  // namespace storage
}  // namespace concord
