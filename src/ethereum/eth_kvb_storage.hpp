// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Wrapper around KVB to provide EVM execution storage context. This class
// defines the mapping of EVM object to KVB address. It also records updates to
// be used in minting a block when a transaction finishes.

#ifndef ETHEREUM_ETH_KVB_STORAGE_HPP
#define ETHEREUM_ETH_KVB_STORAGE_HPP

#include <log4cplus/loggingmacros.h>
#include <vector>

#include "blockchain/db_interfaces.h"
#include "blockchain/db_types.h"
#include "common/concord_types.hpp"
#include "evmjit.h"
#include "hash_defs.h"
#include "sliver.hpp"
#include "status.hpp"

namespace concord {
namespace ethereum {

class EthKvbStorage {
 private:
  const concord::storage::blockchain::ILocalKeyValueStorageReadOnly &roStorage_;
  concord::storage::blockchain::IBlocksAppender *blockAppender_;
  concord::storage::SetOfKeyValuePairs updates;
  std::vector<concord::common::EthTransaction> pending_transactions;
  log4cplus::Logger logger;

  /* Value of "type" byte, at the start of each key. */
  const uint8_t TYPE_BLOCK = 0x01;
  const uint8_t TYPE_TRANSACTION = 0x02;
  const uint8_t TYPE_BALANCE = 0x03;
  const uint8_t TYPE_CODE = 0x04;
  const uint8_t TYPE_STORAGE = 0x05;
  const uint8_t TYPE_NONCE = 0x06;

  // 0x20 used by concord::time::TimeContract
  // 0x21 used by concord::storage::ConcordMetadataStorage

  concordUtils::Sliver kvb_key(uint8_t type, const uint8_t *bytes,
                               size_t length) const;

  concordUtils::Sliver block_key(const concord::common::EthBlock &blk) const;
  concordUtils::Sliver block_key(const evmc_uint256be &hash) const;
  concordUtils::Sliver transaction_key(
      const concord::common::EthTransaction &tx) const;
  concordUtils::Sliver transaction_key(const evmc_uint256be &hash) const;
  concordUtils::Sliver balance_key(const evmc_address &addr) const;
  concordUtils::Sliver nonce_key(const evmc_address &addr) const;
  concordUtils::Sliver code_key(const evmc_address &addr) const;
  concordUtils::Sliver storage_key(const evmc_address &addr,
                                   const evmc_uint256be &location) const;
  concordUtils::Status get(const concordUtils::Sliver &key,
                           concordUtils::Sliver &out);
  concordUtils::Status get(const concordUtils::BlockId readVersion,
                           const concordUtils::Sliver &key,
                           concordUtils::Sliver &value,
                           concordUtils::BlockId &outBlock);
  void put(const concordUtils::Sliver &key, const concordUtils::Sliver &value);

  uint64_t next_block_number();
  void add_block(concord::common::EthBlock &blk);

 public:
  // read-only mode
  EthKvbStorage(
      const concord::storage::blockchain::ILocalKeyValueStorageReadOnly
          &roStorage);

  // read-write mode
  EthKvbStorage(
      const concord::storage::blockchain::ILocalKeyValueStorageReadOnly
          &roStorage,
      concord::storage::blockchain::IBlocksAppender *blockAppender);

  ~EthKvbStorage();

  bool is_read_only();
  const concord::storage::blockchain::ILocalKeyValueStorageReadOnly &
  getReadOnlyStorage();

  uint64_t current_block_number();
  concord::common::EthBlock get_block(const evmc_uint256be &hash);
  concord::common::EthBlock get_block(uint64_t number);
  concord::common::EthTransaction get_transaction(const evmc_uint256be &hash);
  evmc_uint256be get_balance(const evmc_address &addr);
  evmc_uint256be get_balance(const evmc_address &addr, uint64_t &block_number);
  uint64_t get_nonce(const evmc_address &addr);
  uint64_t get_nonce(const evmc_address &addr, uint64_t &block_number);
  bool account_exists(const evmc_address &addr);
  bool get_code(const evmc_address &addr, std::vector<uint8_t> &out,
                evmc_uint256be &hash);
  bool get_code(const evmc_address &addr, std::vector<uint8_t> &out,
                evmc_uint256be &hash, uint64_t &block_number);
  evmc_uint256be get_storage(const evmc_address &addr,
                             const evmc_uint256be &location);
  evmc_uint256be get_storage(const evmc_address &addr,
                             const evmc_uint256be &location,
                             uint64_t &block_number);

  concordUtils::Status write_block(uint64_t timestamp, uint64_t gas_limit);
  void reset();
  void add_transaction(concord::common::EthTransaction &tx);
  void set_balance(const evmc_address &addr, evmc_uint256be balance);
  void set_nonce(const evmc_address &addr, uint64_t nonce);
  void set_code(const evmc_address &addr, const uint8_t *code,
                size_t code_size);
  void set_storage(const evmc_address &addr, const evmc_uint256be &location,
                   const evmc_uint256be &data);
};

}  // namespace ethereum
}  // namespace concord

#endif  // ETHEREUM_ETH_KVB_STORAGE_HPP
