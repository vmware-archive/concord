// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Concord Ethereum VM management.

#include "evm_init_params.hpp"

#include <iostream>
#include "common/concord_log.hpp"
#include "utils/concord_utils.hpp"

using log4cplus::Logger;
using json = nlohmann::json;
using boost::multiprecision::uint256_t;
using boost::multiprecision::uint512_t;

using concord::common::HexPrintVector;
using concord::utils::dehex;
using concord::utils::from_uint256_t;

namespace concord {
namespace ethereum {

EVMInitParams::EVMInitParams()
    : logger(Logger::getInstance("com.vmware.concord.evm_init_params")) {}

EVMInitParams::EVMInitParams(const std::string genesis_file_path)
    : logger(Logger::getInstance("com.vmware.concord.evm_init_params")) {
  json genesis_block = parse_genesis_block(genesis_file_path);

  if (genesis_block.find("config") != genesis_block.end()) {
    json config = genesis_block["config"];
    if (config.find("chainId") != config.end()) chainID = config["chainId"];
  }
  LOG4CPLUS_INFO(logger, "Connecting to Chain " << chainID);

  if (genesis_block.find("alloc") != genesis_block.end()) {
    json alloc = genesis_block["alloc"];
    for (json::iterator it = alloc.begin(); it != alloc.end(); it++) {
      std::vector<uint8_t> address_v = dehex(it.key());
      if (address_v.size() != sizeof(evmc_address)) {
        LOG4CPLUS_ERROR(
            logger, "Invalid account address: " << HexPrintVector{address_v});
        throw EVMInitParamException("Invalid 'alloc' section in genesis file");
      }
      evmc_address address;
      std::copy(address_v.begin(), address_v.end(), address.bytes);

      std::string balance_str = it.value()["balance"];
      uint256_t balance{balance_str};

      // Make sure the balance is within the 256bit boundary
      uint512_t balance_512{balance_str};
      if (balance != balance_512) {
        throw EVMInitParamException("Invalid 'balance' in genesis file");
      }
      initial_accounts[address] = from_uint256_t(&balance);
      LOG4CPLUS_TRACE(logger,
                      "New initial account added with balance: " << balance);
    }
  }
  LOG4CPLUS_INFO(logger, initial_accounts.size() << " initial accounts added.");

  if (genesis_block.find("timestamp") != genesis_block.end()) {
    std::string time_str = genesis_block["timestamp"];
    timestamp = parse_number("timestamp", time_str);
  }

  if (genesis_block.find("gasLimit") != genesis_block.end()) {
    std::string gas_str = genesis_block["gasLimit"];
    gasLimit = parse_number("gasLimit", gas_str);
  }
}

/**
 * Reads the genesis block json from file @genesis_file_path.
 * This json is parsed and converted into nlohmann::json and returned
 */
json EVMInitParams::parse_genesis_block(std::string genesis_file_path) {
  std::ifstream genesis_stream(genesis_file_path);
  json genesis_block;
  if (genesis_stream.good()) {
    genesis_stream >> genesis_block;
  } else {
    LOG4CPLUS_ERROR(logger, "Error reading genesis file at "
                                << genesis_file_path << " Exiting.");
    throw EVMInitParamException("Unable to read genesis file at: " +
                                genesis_file_path);
  }
  return genesis_block;
}

uint64_t EVMInitParams::parse_number(std::string label, std::string val_str) {
  // Values can have odd nibble counts - pad & retry
  if (val_str.size() % 2 != 0) {
    std::string even_val_str = "0";
    if (val_str.size() >= 2 && val_str[0] == '0' && val_str[1] == 'x') {
      even_val_str += val_str.substr(2);
    } else {
      even_val_str += val_str;
    }
    return parse_number(label, even_val_str);
  }

  std::vector<uint8_t> val_v = dehex(val_str);

  if (val_v.size() > sizeof(uint64_t)) {
    throw EVMInitParamException(label + " value is too large");
  }

  uint64_t val = 0;
  for (auto b = val_v.begin(); b != val_v.end(); b++) {
    val <<= 8;
    val |= *b;
  }
  return val;
}

const std::map<evmc_address, evmc_uint256be>
    &EVMInitParams::get_initial_accounts() const {
  return initial_accounts;
}

uint64_t EVMInitParams::get_chainID() const { return chainID; }

uint64_t EVMInitParams::get_timestamp() const { return timestamp; }

uint64_t EVMInitParams::get_gas_limit() const { return gasLimit; }

}  // namespace ethereum
}  // namespace concord
