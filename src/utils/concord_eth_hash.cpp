// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Ethereum Hashing.

#include "concord_eth_hash.hpp"

#include <keccak.h>
#include <log4cplus/loggingmacros.h>

#include "evmjit.h"

namespace concord {
namespace utils {
namespace eth_hash {

evmc_uint256be keccak_hash(const std::vector<uint8_t> &data) {
  return keccak_hash(&data[0], data.size());
}

evmc_uint256be keccak_hash(const uint8_t *data, size_t size) {
  static_assert(sizeof(evmc_uint256be) == CryptoPP::Keccak_256::DIGESTSIZE,
                "hash is not the same size as uint256");

  CryptoPP::Keccak_256 keccak;
  evmc_uint256be hash;
  keccak.CalculateDigest(hash.bytes, data, size);
  return hash;
}

}  // namespace eth_hash
}  // namespace utils
}  // namespace concord
