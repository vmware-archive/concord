// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Ethereum Hashing.

#ifndef UTILS_CONCORD_ETH_HASH_HPP
#define UTILS_CONCORD_ETH_HASH_HPP

#include <log4cplus/loggingmacros.h>
#include "evmjit.h"

namespace concord {
namespace utils {
namespace eth_hash {

evmc_uint256be keccak_hash(const std::vector<uint8_t> &data);
evmc_uint256be keccak_hash(const uint8_t *data, size_t size);

}  // namespace eth_hash
}  // namespace utils
}  // namespace concord

#endif  // UTILS_CONCORD_ETH_HASH_HPP
