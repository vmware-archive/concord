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

#include "common/concord_types.hpp"
#include "consensus/hash_defs.h"
#include "consensus/sliver.hpp"
#include "evm.h"
#include "storage/blockchain_db_types.h"
#include "storage/blockchain_interfaces.h"

namespace concord {
namespace ethereum {

class EthKvbStorage {
 private:
  const concord::storage::ILocalKeyValueStorageReadOnly &roStorage_;
  concord::storage::IBlocksAppender *blockAppender_;
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

  // 0x10 - 0x1F reserved

  // 0x20 used by concord::time::TimeContract
  // 0x21 used by concord::storage::ConcordMetadataStorage

  concord::consensus::Sliver kvb_key(uint8_t type, const uint8_t *bytes,
                                     size_t length) const;

  concord::consensus::Sliver block_key(
      const concord::common::EthBlock &blk) const;
  concord::consensus::Sliver block_key(const evm_uint256be &hash) const;
  concord::consensus::Sliver transaction_key(
      const concord::common::EthTransaction &tx) const;
  concord::consensus::Sliver transaction_key(const evm_uint256be &hash) const;
  concord::consensus::Sliver balance_key(const evm_address &addr) const;
  concord::consensus::Sliver nonce_key(const evm_address &addr) const;
  concord::consensus::Sliver code_key(const evm_address &addr) const;
  concord::consensus::Sliver storage_key(const evm_address &addr,
                                         const evm_uint256be &location) const;
  concord::consensus::Status get(const concord::consensus::Sliver &key,
                                 concord::consensus::Sliver &out);
  concord::consensus::Status get(const concord::storage::BlockId readVersion,
                                 const concord::consensus::Sliver &key,
                                 concord::consensus::Sliver &value,
                                 concord::storage::BlockId &outBlock);
  void put(const concord::consensus::Sliver &key,
           const concord::consensus::Sliver &value);

  uint64_t next_block_number();
  void add_block(concord::common::EthBlock &blk);

 public:
  // read-only mode
  EthKvbStorage(
      const concord::storage::ILocalKeyValueStorageReadOnly &roStorage);

  // read-write mode
  EthKvbStorage(
      const concord::storage::ILocalKeyValueStorageReadOnly &roStorage,
      concord::storage::IBlocksAppender *blockAppender);

  ~EthKvbStorage();

  bool is_read_only();
  const concord::storage::ILocalKeyValueStorageReadOnly &getReadOnlyStorage();

  uint64_t current_block_number();
  concord::common::EthBlock get_block(const evm_uint256be &hash);
  concord::common::EthBlock get_block(uint64_t number);
  concord::common::EthTransaction get_transaction(const evm_uint256be &hash);
  evm_uint256be get_balance(const evm_address &addr);
  evm_uint256be get_balance(const evm_address &addr, uint64_t &block_number);
  uint64_t get_nonce(const evm_address &addr);
  uint64_t get_nonce(const evm_address &addr, uint64_t &block_number);
  bool account_exists(const evm_address &addr);
  bool get_code(const evm_address &addr, std::vector<uint8_t> &out,
                evm_uint256be &hash);
  bool get_code(const evm_address &addr, std::vector<uint8_t> &out,
                evm_uint256be &hash, uint64_t &block_number);
  evm_uint256be get_storage(const evm_address &addr,
                            const evm_uint256be &location);
  evm_uint256be get_storage(const evm_address &addr,
                            const evm_uint256be &location,
                            uint64_t &block_number);

  concord::consensus::Status write_block(uint64_t timestamp,
                                         uint64_t gas_limit);
  void reset();
  void add_transaction(concord::common::EthTransaction &tx);
  void set_balance(const evm_address &addr, evm_uint256be balance);
  void set_nonce(const evm_address &addr, uint64_t nonce);
  void set_code(const evm_address &addr, const uint8_t *code, size_t code_size);
  void set_storage(const evm_address &addr, const evm_uint256be &location,
                   const evm_uint256be &data);
};

}  // namespace ethereum
}  // namespace concord

#endif  // ETHEREUM_ETH_KVB_STORAGE_HPP
