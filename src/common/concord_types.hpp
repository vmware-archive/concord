// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Common types passed among concord components.

#ifndef COMMON_CONCORD_TYPES_HPP
#define COMMON_CONCORD_TYPES_HPP

#include "concord_log.hpp"
#include "concord_types.hpp"
#include "consensus/sliver.hpp"

namespace concord {
namespace common {

const evm_address zero_address{{0}};
const evm_uint256be zero_hash{{0}};

const int64_t tx_storage_version = 1;
const int64_t blk_storage_version = 1;

typedef struct EthLog {
  evm_address address;
  std::vector<evm_uint256be> topics;
  std::vector<uint8_t> data;
} EthLog;

typedef struct EthTransaction {
  uint64_t nonce;
  evm_uint256be block_hash;
  uint64_t block_number;
  evm_address from;
  evm_address to;
  evm_address contract_address;
  std::vector<uint8_t> input;
  evm_status_code status;
  evm_uint256be value;
  uint64_t gas_price;
  uint64_t gas_limit;
  uint64_t gas_used;
  std::vector<EthLog> logs;
  evm_uint256be sig_r;
  evm_uint256be sig_s;
  uint64_t sig_v;

  std::vector<uint8_t> rlp() const;
  evm_uint256be hash() const;
  size_t serialize(uint8_t **out);
  static struct EthTransaction deserialize(concord::consensus::Sliver &input);
} EthTransaction;

typedef struct EthBlock {
  uint64_t number;
  uint64_t timestamp;
  evm_uint256be hash;
  evm_uint256be parent_hash;
  uint64_t gas_limit;
  uint64_t gas_used;
  std::vector<evm_uint256be> transactions;

  evm_uint256be get_hash() const;
  size_t serialize(uint8_t **out);
  static struct EthBlock deserialize(concord::consensus::Sliver &input);
} EthBlock;

}  // namespace common
}  // namespace concord

// Byte-wise comparators for evm_uint256be and evm_address. This allows us to
// use these types as keys in a std::map. Must be in the global namespace.
bool operator<(const evm_uint256be &a, const evm_uint256be &b);
bool operator!=(const evm_uint256be &a, const evm_uint256be &b);
bool operator==(const evm_uint256be &a, const evm_uint256be &b);
bool operator<(const evm_address &a, const evm_address &b);
bool operator!=(const evm_address &a, const evm_address &b);
bool operator==(const evm_address &a, const evm_address &b);

#endif  // COMMON_CONCORD_TYPES_HPP
