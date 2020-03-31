// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Wrapper used by concord::consensus::ConcordCommandsHandler to store BFT
// metadata (sequence number).

#include "concord_block_metadata.h"

#include "kv_types.hpp"

namespace concord {
namespace storage {

Sliver ConcordBlockMetadata::serialize(uint64_t bft_sequence_num) const {
  com::vmware::concord::kvb::BlockMetadata block_metadata_;
  block_metadata_.set_version(kBlockMetadataVersion);
  block_metadata_.set_bft_sequence_num(bft_sequence_num);

  // It would be nice to re-use a buffer that is allocated only once, when this
  // class is instantiated. But, to many things rely on being able to hold a
  // pointer to the buffer we return, and they would all see the modification we
  // would make later.
  size_t serialized_size = block_metadata_.ByteSize();
  char* raw_buffer = new char[serialized_size];
  block_metadata_.SerializeToArray(raw_buffer, serialized_size);
  return Sliver(raw_buffer, serialized_size);
}

uint64_t ConcordBlockMetadata::getSequenceNum(const Sliver& key) const {
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
  LOG_DEBUG(logger_, "key = " << key << ", sequenceNum = " << sequenceNum);
  return sequenceNum;
}

}  // namespace storage
}  // namespace concord
