// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Concord Ethereum VM management.

#include "concord_evm.hpp"

#include <log4cplus/loggingmacros.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "common/concord_exception.hpp"
#include "common/concord_log.hpp"
#include "common/concord_types.hpp"
#include "consensus/hash_defs.h"
#include "consensus/hex_tools.h"
#include "ethereum/eth_kvb_storage.hpp"
#include "utils/concord_eth_hash.hpp"
#include "utils/concord_utils.hpp"
#include "utils/rlp.hpp"

#ifdef USE_HERA
#include "hera.h"
#else
#include "evmjit.h"
#endif

using boost::multiprecision::uint256_t;
using log4cplus::Logger;

using concord::common::EthBlock;
using concord::common::EthLog;
using concord::common::EVMException;
using concord::common::zero_address;
using concord::common::zero_hash;
using concord::common::operator<<;
using concord::utils::from_evmc_uint256be;
using concord::utils::from_uint256_t;
using concord::utils::RLPBuilder;
using concord::utils::to_uint256_t;

namespace concord {
namespace ethereum {

/**
 * Initialize the concord/evm context and start the evm instance.
 */
EVM::EVM(EVMInitParams params)
    : logger(Logger::getInstance("com.vmware.concord.evm")),
      chainId(params.get_chainID()) {
#ifdef USE_HERA
  evminst = hera_create();
#else
  evminst = evmjit_create();
#endif

  if (!evminst) {
    LOG4CPLUS_FATAL(logger, "Could not create EVM instance");
    throw EVMException("Could not create EVM instance");
  }
  LOG4CPLUS_INFO(logger, "EVM started");
}

/**
 * Shutdown the EVM instance and destroy the concord context.
 */
EVM::~EVM() {
  evminst->destroy(evminst);
  LOG4CPLUS_INFO(logger, "EVM stopped");
}

void EVM::transfer_fund(evmc_message& message, EthKvbStorage& kvbStorage,
                        evmc_result& result) {
  uint256_t transfer_val = to_uint256_t(&message.value);

  try {
    evmc_uint256be src_in = kvbStorage.get_balance(message.sender);
    evmc_uint256be dst_in = kvbStorage.get_balance(message.destination);

    uint256_t sender_balance = to_uint256_t(&src_in);
    uint256_t destination_balance = to_uint256_t(&dst_in);

    if (!kvbStorage.account_exists(message.sender)) {
      // Don't allow if source account does not exist.
      result.status_code = EVMC_FAILURE;
      LOG4CPLUS_DEBUG(logger, "Source account with address "
                                  << message.sender << ", does not exist.");
    } else if (sender_balance < transfer_val) {
      // Don't allow if source account has insufficient balance.
      result.status_code = EVMC_FAILURE;
      LOG4CPLUS_DEBUG(logger, "Account with address "
                                  << message.sender
                                  << ", does not have sufficient funds ("
                                  << sender_balance << ").");
    } else {
      destination_balance += transfer_val;
      sender_balance -= transfer_val;
      kvbStorage.set_balance(message.destination,
                             from_uint256_t(&destination_balance));
      kvbStorage.set_balance(message.sender, from_uint256_t(&sender_balance));
      result.status_code = EVMC_SUCCESS;
      LOG4CPLUS_DEBUG(logger, "Transferred  " << transfer_val << " units to: "
                                              << message.destination << " ("
                                              << destination_balance << ")"
                                              << " from: " << message.sender
                                              << " (" << sender_balance << ")");
    }
  } catch (...) {
    LOG4CPLUS_DEBUG(logger, "Failed to decode balances");
    result.status_code = EVMC_FAILURE;
  }
}

/**
 * Run a contract, or just transfer value if the destination is not a
 * contract. Calling a contract can either be done with 'call' method or with
 * 'sendTransaction'. Generally pure methods (methods which don't change any
 * state) are called via 'call' method and all others are called via
 * 'sendTransaction' method. The 'sendTransaction' way requires that the
 * transaction is recorded. However for 'call' way there is no transaction to
 * record, it is a simple read storage operation.
 */
evmc_result EVM::run(evmc_message& message, uint64_t timestamp,
                     EthKvbStorage& kvbStorage, std::vector<EthLog>& evmLogs,
                     const evmc_address& origin,
                     const evmc_address& storage_contract) {
  assert(message.kind != EVMC_CREATE);

  std::vector<uint8_t> code;
  evmc_uint256be hash{{0}};
  evmc_result result;
  result.output_data = nullptr;
  result.output_size = 0;
  result.create_address = {0};
  result.release = nullptr;
  if (kvbStorage.get_code(message.destination, code, hash)) {
    LOG4CPLUS_DEBUG(logger, "Loaded code from " << message.destination);
    message.code_hash = hash;

    try {
      result = execute(message, timestamp, kvbStorage, evmLogs, code, origin,
                       storage_contract);
      if (result.status_code == EVMC_SUCCESS) {
        uint64_t transfer_val = from_evmc_uint256be(&message.value);
        if (transfer_val > 0) {
          transfer_fund(message, kvbStorage, result);
        }
      }
    } catch (EVMException e) {
      LOG4CPLUS_ERROR(logger, "EVM execution exception: '"
                                  << e.what() << "'. "
                                  << "Contract: " << message.destination);
      result.status_code = EVMC_FAILURE;
    }
  } else if (message.input_size == 0) {
    LOG4CPLUS_DEBUG(logger, "No code found at " << message.destination);
    memset(&result, 0, sizeof(result));

    if (!kvbStorage.is_read_only()) {
      transfer_fund(message, kvbStorage, result);
    } else {
      LOG4CPLUS_DEBUG(logger, "Balance transfer attempted in read-only mode.");
      result.status_code = EVMC_FAILURE;
    }
  } else {
    LOG4CPLUS_DEBUG(logger, "Input data, but no code at "
                                << message.destination
                                << ", returning error code.");
    // attempted to run a contract that doesn't exist
    memset(&result, 0, sizeof(result));
    result.status_code = EVMC_FAILURE;
  }

  return result;
}

/**
 * Create a contract.
 */
evmc_result EVM::create(evmc_address& contract_address, evmc_message& message,
                        uint64_t timestamp, EthKvbStorage& kvbStorage,
                        std::vector<EthLog>& evmLogs,
                        const evmc_address& origin) {
  assert(message.kind == EVMC_CREATE);
  assert(message.input_size > 0);

  std::vector<uint8_t> code;
  evmc_uint256be hash{{0}};
  evmc_result result;
  if (!kvbStorage.get_code(contract_address, code, hash)) {
    LOG4CPLUS_DEBUG(logger, "Creating contract at " << contract_address);

    std::vector<uint8_t> create_code = std::vector<uint8_t>(
        message.input_data, message.input_data + message.input_size);
    message.destination = contract_address;

    // we need a hash for this, or evmjit will cache its compilation under
    // something random
    message.code_hash = concord::utils::eth_hash::keccak_hash(create_code);

    result = execute(message, timestamp, kvbStorage, evmLogs, create_code,
                     origin, contract_address);

    // TODO: check if the new contract is zero bytes in length;
    //       return error, not success in that case
    if (result.status_code == EVMC_SUCCESS) {
      LOG4CPLUS_DEBUG(logger, "Contract created at "
                                  << contract_address << " with "
                                  << result.output_size << "bytes of code.");
      kvbStorage.set_code(contract_address, result.output_data,
                          result.output_size);

      // users could transfer ether to the new created contract if the value
      // is specified.
      uint64_t transfer_val = from_evmc_uint256be(&message.value);
      if (transfer_val > 0) {
        transfer_fund(message, kvbStorage, result);
      }

      // There is a bug (either in evmjit or in our usage of evm) which
      // causes nested contract creation calls to give segmentation
      // fault. This bug also causes segmentation fault if we try to call
      // release method on result object in a normal contract creation call.
      // The reason is that `evmjit` stores a pointer to its internal data
      // inside evmc_result's optional data storage and when the evmc_result
      // objects scope ends we (or evm in case of nested call) call release
      // method to free memory from that pointer. However, this optional data
      // storage actually uses evmc_result structure's create_address field to
      // store that pointer, in case of nested contract creation we store the
      // address of created contract into this field and it over-writes the
      // already stored pointer. This leads to seg-fault when freeing that
      // memory.  To fix this temporarily we just call release on result
      // object ourselves. Ideally only the owner of result object should
      // call release method on it and the result object itself should not be
      // used once we call release on it but this works until we find a
      // proper way.
      if (result.release) {
        result.release(&result);
        result.release = nullptr;
      }

      result.create_address = contract_address;
    }
  } else {
    LOG4CPLUS_DEBUG(logger, "Existing code found at "
                                << message.destination
                                << ", returning error code.");
    // attempted to call a contract that doesn't exist
    memset(&result, 0, sizeof(result));
    result.status_code = EVMC_FAILURE;
  }

  // don't expose the address if it wasn't used
  if (result.status_code != EVMC_SUCCESS) {
    result.create_address = zero_address;
  }

  return result;
}

/**
 * Contract destination is the low 20 bytes of the SHA3 hash of the RLP encoding
 * of [sender_address, sender_nonce].
 */
evmc_address EVM::contract_destination(evmc_address& sender,
                                       uint64_t nonce) const {
  RLPBuilder rlpb;
  rlpb.start_list();

  // RLP building is done in reverse order - build flips it for us

  if (nonce == 0) {
    // "0" is encoded as "empty string" here, not "integer zero"
    std::vector<uint8_t> empty_nonce;
    rlpb.add(empty_nonce);
  } else {
    rlpb.add(nonce);
  }

  rlpb.add(sender);
  std::vector<uint8_t> rlp = rlpb.build();

  // hash it
  evmc_uint256be hash = concord::utils::eth_hash::keccak_hash(rlp);

  // the lower 20 bytes are the address
  evmc_address address;
  std::copy(hash.bytes + (sizeof(evmc_uint256be) - sizeof(evmc_address)),
            hash.bytes + sizeof(evmc_uint256be), address.bytes);
  return address;
}

evmc_result EVM::execute(evmc_message& message, uint64_t timestamp,
                         EthKvbStorage& kvbStorage,
                         std::vector<EthLog>& evmLogs,
                         const std::vector<uint8_t>& code,
                         const evmc_address& origin,
                         const evmc_address& storage_contract) {
  // wrap an evm context in an concord context
  concord_context concctx = {
      {&concord_fn_table}, this,   &kvbStorage,     &evmLogs, &logger,
      timestamp,           origin, storage_contract};

  return evminst->execute(evminst, &concctx.evmctx, EVMC_BYZANTIUM, &message,
                          &code[0], code.size());
}

extern "C" {
/**
 * The next several conc_* functions are callbacks that the EVM uses to interact
 * with our state-keeping layer.
 */

EVM* conc_object(const struct evmc_context* evmctx) {
  return reinterpret_cast<const concord_context*>(evmctx)->conc_object;
}

const concord_context* conc_context(const struct evmc_context* evmctx) {
  return reinterpret_cast<const concord_context*>(evmctx);
}

int conc_account_exists(struct evmc_context* evmctx,
                        const struct evmc_address* address) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::account_exists called, address: " << *address);

  if (conc_context(evmctx)->kvbStorage->account_exists(*address)) {
    return 1;
  }
  return 0;
}

void conc_get_storage(struct evmc_uint256be* result,
                      struct evmc_context* evmctx,
                      const struct evmc_address* address,
                      const struct evmc_uint256be* key) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::get_storage called, contract address: "
                      << *address << " storage contract: "
                      << conc_context(evmctx)->storage_contract
                      << " key: " << *key);

  *result = conc_context(evmctx)->kvbStorage->get_storage(
      conc_context(evmctx)->storage_contract, *key);
}

void conc_set_storage(struct evmc_context* evmctx,
                      const struct evmc_address* address,
                      const struct evmc_uint256be* key,
                      const struct evmc_uint256be* value) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::set_storage called, contract address: "
                      << *address << " storage contract: "
                      << conc_context(evmctx)->storage_contract
                      << " key: " << *key << " value: " << *value);

  conc_context(evmctx)->kvbStorage->set_storage(
      conc_context(evmctx)->storage_contract, *key, *value);
}

void conc_get_balance(struct evmc_uint256be* result,
                      struct evmc_context* evmctx,
                      const struct evmc_address* address) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::get_balance called, address: " << *address);

  evmc_uint256be balance =
      conc_context(evmctx)->kvbStorage->get_balance(*address);
  memcpy(result, &balance, sizeof(evmc_uint256be));
}

size_t conc_get_code_size(struct evmc_context* evmctx,
                          const struct evmc_address* address) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "conc_get_code_size called, address: " << *address);
  std::vector<uint8_t> code;
  evmc_uint256be hash;
  if (conc_context(evmctx)->kvbStorage->get_code(*address, code, hash)) {
    return code.size();
  }

  return 0;
}

size_t conc_copy_code(struct evmc_context* evmctx,
                      const struct evmc_address* address, size_t code_offset,
                      uint8_t* buffer_data, size_t buffer_size) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "conc_get_code called, address: " << *address);

  std::vector<uint8_t> stored_code;
  evmc_uint256be hash;
  if (conc_context(evmctx)->kvbStorage->get_code(*address, stored_code, hash)) {
    memcpy(buffer_data, &stored_code[0] + code_offset,
           buffer_size - code_offset);
    return stored_code.size();
  }
  return 0;
}

void conc_selfdestruct(struct evmc_context* evmctx,
                       const struct evmc_address* address,
                       const struct evmc_address* beneficiary) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "conc_selfdestruct called, address: "
                      << *address << " beneficiary: " << *beneficiary);

  // TODO: Actually self-destruct contract.
}

void conc_emit_log(struct evmc_context* evmctx,
                   const struct evmc_address* address, const uint8_t* data,
                   size_t data_size, const struct evmc_uint256be topics[],
                   size_t topics_count) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::emit_log called, address: "
                      << *address << " topics_count: " << topics_count
                      << " data_size: " << data_size);

  EthLog log;
  log.address = *address;
  for (size_t i = 0; i < topics_count; i++) {
    log.topics.push_back(topics[i]);
  }
  std::copy(data, data + data_size, std::back_inserter(log.data));

  conc_context(evmctx)->evmLogs->push_back(log);
}

void conc_call(struct evmc_result* result, struct evmc_context* evmctx,
               const struct evmc_message* msg) {
  // create copy of message struct since
  // call function needs non-const message object
  evmc_message call_msg = *msg;

  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::call called: " << call_msg);

  // our block-creation scheme will get confused if the EVM isn't
  // incrementing the depth for us
  assert(msg->depth > 0);

  // evmc_result object sent by evm is un-initialized, not initializing it
  // can cause segmentation errors
  memset(result, 0, sizeof(evmc_result));

  if (msg->kind == EVMC_CREATE) {
    EthKvbStorage* kvbStorage = conc_context(evmctx)->kvbStorage;

    uint64_t nonce = kvbStorage->get_nonce(call_msg.sender);
    kvbStorage->set_nonce(call_msg.sender, nonce + 1);

    evmc_address contract_address =
        conc_object(evmctx)->contract_destination(call_msg.sender, nonce);

    *result = conc_object(evmctx)->create(
        contract_address, call_msg, conc_context(evmctx)->timestamp,
        *kvbStorage, *(conc_context(evmctx)->evmLogs),
        conc_context(evmctx)->origin);
  } else {
    // CALLCODE and DELEGATECALL both cause the called contract's code to
    // operate on the calling contract's storage (i.e. do not change which
    // storage we were using before the op in that case)
    const evmc_address& storage_contract =
        (msg->kind == EVMC_CALLCODE || msg->kind == EVMC_DELEGATECALL)
            ? conc_context(evmctx)->storage_contract
            : msg->destination;

    *result = conc_object(evmctx)->run(
        call_msg, conc_context(evmctx)->timestamp,
        *(conc_context(evmctx)->kvbStorage), *(conc_context(evmctx)->evmLogs),
        conc_context(evmctx)->origin, storage_contract);
  }
}

void conc_get_block_hash(struct evmc_uint256be* result,
                         struct evmc_context* evmctx, int64_t number) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::get_block_hash called, block: " << number);

  try {
    if (number < 0 ||
        (uint64_t)number >
            conc_context(evmctx)->kvbStorage->current_block_number()) {
      // KVBlockchain internals assert that the value passed to get_block
      // is <= the latest block number
      *result = zero_hash;
    } else {
      EthBlock blk = conc_context(evmctx)->kvbStorage->get_block(number);
      *result = blk.hash;
    }
  } catch (...) {
    *result = zero_hash;
  }
}

void conc_get_tx_context(struct evmc_tx_context* result,
                         struct evmc_context* evmctx) {
  LOG4CPLUS_DEBUG(*(conc_context(evmctx)->logger),
                  "EVM::get_tx_context called");

  memset(result, 0, sizeof(*result));

  // TODO: fill in the rest of the context for the currently-executing block

  result->block_timestamp = conc_context(evmctx)->timestamp;
  memcpy(result->tx_origin.bytes, conc_context(evmctx)->origin.bytes,
         sizeof(result->tx_origin));
}
}

}  // namespace ethereum
}  // namespace concord
