// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Concord Ethereum VM management.

#ifndef ETHEREUM_CONCORD_EVM_HPP
#define ETHEREUM_CONCORD_EVM_HPP

#include <log4cplus/loggingmacros.h>
#include <map>
#include <memory>
#include <vector>
#include "common/concord_types.hpp"
#include "evm_init_params.hpp"
#include "evmjit.h"
#include "utils/concord_utils.hpp"

namespace concord {
namespace ethereum {

// forward declaration for callbacks.
class EVM;

/**
 * This extern block of conc_* functions are callbacks that the EVM uses to
 * interact with our state-keeping layer.
 */
extern "C" {
/**
 * Our wrapper around EVM's wrapper, where we can add pointers to the modules
 * we're using to keep state.
 */
typedef struct concord_context {
  /** evmctx must be first, so we can cast to our wrapper */
  struct evmc_context evmctx;
  class EVM* conc_object;
  class EthKvbStorage* kvbStorage;
  std::vector<::concord::common::EthLog>* evmLogs;
  log4cplus::Logger* logger;
  uint64_t timestamp;

  // Stash to answer ORIGIN opcode. This starts with the same value as
  // evmc_message.sender, but sender changes as contracts call other contracts,
  // while origin always points to the same address.
  struct evmc_address origin;

  // Which contract we're actually using for storage. This is usually the
  // contract being called, but may be the contract doing the calling during
  // CALLCODE and DELEGATECALL.
  struct evmc_address storage_contract;
} concord_context;

EVM* conc_object(const struct evmc_context* evmctx);
const concord_context* conc_context(const struct evmc_context* evmctx);

int conc_account_exists(struct evmc_context* evmctx,
                        const struct evmc_address* address);
void conc_get_storage(struct evmc_uint256be* result,
                      struct evmc_context* evmctx,
                      const struct evmc_address* address,
                      const struct evmc_uint256be* key);
void conc_set_storage(struct evmc_context* evmctx,
                      const struct evmc_address* address,
                      const struct evmc_uint256be* key,
                      const struct evmc_uint256be* value);
void conc_get_balance(struct evmc_uint256be* result,
                      struct evmc_context* evmctx,
                      const struct evmc_address* address);
size_t conc_get_code_size(struct evmc_context* evmctx,
                          const struct evmc_address* address);
size_t conc_copy_code(struct evmc_context* evmctx,
                      const struct evmc_address* address, size_t code_offset,
                      uint8_t* buffer_data, size_t buffer_size);
void conc_selfdestruct(struct evmc_context* evmctx,
                       const struct evmc_address* address,
                       const struct evmc_address* beneficiary);
void conc_emit_log(struct evmc_context* evmctx,
                   const struct evmc_address* address, const uint8_t* data,
                   size_t data_size, const struct evmc_uint256be topics[],
                   size_t topics_count);
void conc_call(struct evmc_result* result, struct evmc_context* evmctx,
               const struct evmc_message* msg);
void conc_get_block_hash(struct evmc_uint256be* result,
                         struct evmc_context* evmctx, int64_t number);
void conc_get_tx_context(struct evmc_tx_context* result,
                         struct evmc_context* evmctx);

/*
 * Function dispatch table for EVM. Specified by EEI.
 */
const static struct evmc_context_fn_table concord_fn_table = {
    conc_account_exists, conc_get_storage,   conc_set_storage,
    conc_get_balance,    conc_get_code_size, conc_copy_code,
    conc_selfdestruct,   conc_call,          conc_get_tx_context,
    conc_get_block_hash, conc_emit_log};
}

class EVM {
 public:
  explicit EVM(EVMInitParams params);
  ~EVM();

  /* Concord API */
  void transfer_fund(evmc_message& message, EthKvbStorage& kvbStorage,
                     evmc_result& result);
  evmc_result run(evmc_message& message, uint64_t timestamp,
                  EthKvbStorage& kvbStorage,
                  std::vector<::concord::common::EthLog>& evmLogs,
                  const evmc_address& origin,
                  const evmc_address& storage_contract);
  evmc_result create(evmc_address& contract_address, evmc_message& message,
                     uint64_t timestamp, EthKvbStorage& kvbStorage,
                     std::vector<::concord::common::EthLog>& evmLogs,
                     const evmc_address& origin);
  evmc_address contract_destination(evmc_address& sender, uint64_t nonce) const;

 private:
  evmc_instance* evminst;
  log4cplus::Logger logger;

  // chain to which we are connected
  uint64_t chainId;

  evmc_result execute(evmc_message& message, uint64_t timestamp,
                      EthKvbStorage& kvbStorage,
                      std::vector<::concord::common::EthLog>& evmLogs,
                      const std::vector<uint8_t>& code,
                      const evmc_address& origin,
                      const evmc_address& storage_contract);
};

}  // namespace ethereum
}  // namespace concord

#endif  // ETHEREUM_CONCORD_EVM_HPP
