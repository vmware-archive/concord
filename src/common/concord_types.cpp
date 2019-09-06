// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Common types passed among Concord components.

#include "concord_types.hpp"

#include <keccak.h>
#include <log4cplus/loggingmacros.h>
#include <string.h>

#include "common/concord_exception.hpp"
#include "common/concord_log.hpp"
#include "concord_storage.pb.h"
#include "consensus/hex_tools.h"
#include "consensus/sliver.hpp"
#include "utils/concord_eth_hash.hpp"
#include "utils/rlp.hpp"

using com::vmware::concord::kvb::Block;
using com::vmware::concord::kvb::Log;
using com::vmware::concord::kvb::Transaction;
using concord::utils::RLPBuilder;

// Byte-wise comparator for evm_uint256be. This allows us to use this type as a
// key in a std::map. Must be in the global namespace.
bool operator<(const evm_uint256be &a, const evm_uint256be &b) {
  for (size_t i = 0; i < sizeof(evm_uint256be); ++i) {
    if (a.bytes[i] < b.bytes[i]) {
      return true;
    } else if (a.bytes[i] > b.bytes[i]) {
      return false;
    }
  }

  return false;
}

bool operator!=(const evm_uint256be &a, const evm_uint256be &b) {
  return !(a == b);
}

bool operator==(const evm_uint256be &a, const evm_uint256be &b) {
  return memcmp(a.bytes, b.bytes, sizeof(evm_uint256be)) == 0;
}

// Byte-wise comparator for evm_address. This allows us to use this type as a
// key in a std::map. Must be in the global namespace.
bool operator<(const evm_address &a, const evm_address &b) {
  for (size_t i = 0; i < sizeof(evm_address); ++i) {
    if (a.bytes[i] < b.bytes[i]) {
      return true;
    } else if (a.bytes[i] > b.bytes[i]) {
      return false;
    }
  }

  return false;
}

bool operator!=(const evm_address &a, const evm_address &b) {
  return !(a == b);
}

bool operator==(const evm_address &a, const evm_address &b) {
  return memcmp(a.bytes, b.bytes, sizeof(evm_address)) == 0;
}

namespace concord {
namespace common {

std::vector<uint8_t> EthTransaction::rlp() const {
  RLPBuilder rlpb;
  rlpb.start_list();

  // RLP building is done in reverse order - build flips it for us
  rlpb.add(this->sig_s);
  rlpb.add(this->sig_r);
  rlpb.add(this->sig_v);
  rlpb.add(this->input);

  // `value` is a QUANTITY and hence doesn't allow leading zeroes.
  // Note: This is a big endian type which means that value.bytes[0] is the MSB
  size_t i = 0;
  for (; i < sizeof(evm_uint256be); ++i) {
    if (this->value.bytes[i] != 0x00) {
      break;
    }
  }
  rlpb.add(&this->value.bytes[i], sizeof(evm_uint256be) - i);

  if (this->to == zero_address) {
    // when deploying a contract, the 'to' addresss is empty, hence to insert
    // an empty vector.
    std::vector<uint8_t> empty_address;
    rlpb.add(empty_address);
  } else {
    rlpb.add(this->to);
  }

  rlpb.add(this->gas_limit);
  rlpb.add(this->gas_price);
  rlpb.add(this->nonce);

  return rlpb.build();
}

/**
 * Compute the hash which will be used to reference the transaction.
 */
evm_uint256be EthTransaction::hash() const {
  return concord::utils::eth_hash::keccak_hash(this->rlp());
}

size_t EthTransaction::serialize(uint8_t **serialized) {
  Transaction out;

  out.set_version(tx_storage_version);
  out.set_block_number(this->block_number);
  out.set_nonce(this->nonce);
  out.set_block_hash(this->block_hash.bytes, sizeof(this->block_hash));
  out.set_from(this->from.bytes, sizeof(this->from));

  if (this->to != zero_address) {
    out.set_to(this->to.bytes, sizeof(this->to));
  }

  if (this->contract_address != zero_address) {
    out.set_contract_address(this->contract_address.bytes,
                             sizeof(this->contract_address));
  }

  if (this->input.size() > 0) {
    out.set_input(std::string(this->input.begin(), this->input.end()));
  }

  out.set_status(this->status);
  out.set_value(this->value.bytes, sizeof(evm_uint256be));
  out.set_gas_price(this->gas_price);
  out.set_gas_limit(this->gas_limit);
  out.set_gas_used(this->gas_used);
  out.set_sig_v(this->sig_v);
  out.set_sig_r(this->sig_r.bytes, sizeof(this->sig_r));
  out.set_sig_s(this->sig_s.bytes, sizeof(this->sig_s));

  for (EthLog &log : this->logs) {
    Log *outlog = out.add_log();
    outlog->set_address(log.address.bytes, sizeof(evm_address));
    for (evm_uint256be topic : log.topics) {
      outlog->add_topic(topic.bytes, sizeof(evm_uint256be));
    }
    if (log.data.size() > 0) {
      outlog->set_data(std::string(log.data.begin(), log.data.end()));
    }
  }

  size_t size = out.ByteSize();

  *serialized = new uint8_t[size];
  out.SerializeToArray(*serialized, size);
  return size;
}

struct EthTransaction EthTransaction::deserialize(
    concord::consensus::Sliver &input) {
  Transaction intx;
  intx.ParseFromArray(input.data(), input.length());

  if (intx.version() == tx_storage_version) {
    EthTransaction outtx;

    outtx.block_number = intx.block_number();
    outtx.nonce = intx.nonce();
    std::copy(intx.block_hash().begin(), intx.block_hash().end(),
              outtx.block_hash.bytes);

    if (intx.from().size() != sizeof(outtx.from)) {
      LOG4CPLUS_ERROR(log4cplus::Logger::getInstance("com.vmware.concord"),
                      "Invalid address length " << intx.from().size());
      throw EVMException("Invalid from address length");
    }
    std::copy(intx.from().begin(), intx.from().end(), outtx.from.bytes);

    if (intx.has_to()) {
      // TODO(BWF): check length
      std::copy(intx.to().begin(), intx.to().end(), outtx.to.bytes);
    } else {
      outtx.to = zero_address;
    }

    if (intx.has_contract_address()) {
      // TODO(BWF): check length
      std::copy(intx.contract_address().begin(), intx.contract_address().end(),
                outtx.contract_address.bytes);
    } else {
      outtx.contract_address = zero_address;
    }

    if (intx.has_input()) {
      std::copy(intx.input().begin(), intx.input().end(),
                std::back_inserter(outtx.input));
    }

    outtx.status = static_cast<evm_status_code>(intx.status());
    std::copy(intx.value().begin(), intx.value().end(), outtx.value.bytes);

    if (intx.has_gas_price()) {
      outtx.gas_price = intx.gas_price();
    } else {
      outtx.gas_price = 0;
    }

    if (intx.has_gas_limit()) {
      outtx.gas_limit = intx.gas_limit();
    } else {
      outtx.gas_limit = 0;
    }

    if (intx.has_gas_used()) {
      outtx.gas_used = intx.gas_used();
    } else {
      outtx.gas_used = 0;
    }

    if (intx.has_sig_r()) {
      std::copy(intx.sig_r().begin(), intx.sig_r().end(), outtx.sig_r.bytes);
    } else {
      outtx.sig_r = zero_hash;
    }

    if (intx.has_sig_s()) {
      std::copy(intx.sig_s().begin(), intx.sig_s().end(), outtx.sig_s.bytes);
    } else {
      outtx.sig_s = zero_hash;
    }

    if (intx.has_sig_v()) {
      outtx.sig_v = intx.sig_v();
    } else {
      outtx.sig_v = 0;
    }

    for (int i = 0; i < intx.log_size(); i++) {
      Log inlog = intx.log(i);

      evm_address addr;
      std::copy(inlog.address().begin(), inlog.address().end(), addr.bytes);

      std::vector<evm_uint256be> topics;
      for (int j = 0; j < inlog.topic_size(); j++) {
        evm_uint256be topic;
        std::copy(inlog.topic(j).begin(), inlog.topic(j).end(), topic.bytes);
        topics.push_back(topic);
      }

      std::vector<uint8_t> data;
      if (inlog.has_data()) {
        std::copy(inlog.data().begin(), inlog.data().end(),
                  std::back_inserter(data));
      }

      EthLog outlog{addr, topics, data};
      outtx.logs.push_back(outlog);
    }

    return outtx;
  } else {
    LOG4CPLUS_ERROR(log4cplus::Logger::getInstance("com.vmware.concord"),
                    "Unknown transaction storage version " << intx.version());
    throw EVMException("Unkown transaction storage version");
  }
}

/**
 * Compute the hash which will be used to reference the transaction.
 */
evm_uint256be EthBlock::get_hash() const {
  /*
   * WARNING: This is not the same as Ethereum's block hash right now,
   * but is instead an approximation, in order to provide something to fill API
   * holes. For now, the plan is:
   *
   * RLP([number, parent_hash, [txhash1, txhash2, ...]])
   */

  RLPBuilder rlpb;
  rlpb.start_list();

  rlpb.start_list();
  for (auto txh = this->transactions.rbegin(); txh != this->transactions.rend();
       ++txh) {
    rlpb.add(*txh);
  }
  rlpb.end_list();

  rlpb.add(this->parent_hash);
  rlpb.add(this->number);
  std::vector<uint8_t> rlp = rlpb.build();

  // hash it
  return concord::utils::eth_hash::keccak_hash(rlp);
}

size_t EthBlock::serialize(uint8_t **serialized) {
  Block out;

  out.set_version(blk_storage_version);
  out.set_number(this->number);
  out.set_hash(this->hash.bytes, sizeof(this->hash));
  out.set_parent_hash(this->parent_hash.bytes, sizeof(this->parent_hash));

  for (auto t : this->transactions) {
    out.add_transaction(t.bytes, sizeof(evm_uint256be));
  }

  out.set_timestamp(this->timestamp);
  out.set_gas_limit(this->gas_limit);
  out.set_gas_used(this->gas_used);

  size_t size = out.ByteSize();

  *serialized = new uint8_t[size];
  out.SerializeToArray(*serialized, size);
  return size;
}

struct EthBlock EthBlock::deserialize(concord::consensus::Sliver &input) {
  Block inblk;
  inblk.ParseFromArray(input.data(), input.length());

  if (inblk.version() == blk_storage_version) {
    EthBlock outblk;

    outblk.number = inblk.number();
    std::copy(inblk.hash().begin(), inblk.hash().end(), outblk.hash.bytes);
    std::copy(inblk.parent_hash().begin(), inblk.parent_hash().end(),
              outblk.parent_hash.bytes);

    for (int i = 0; i < inblk.transaction_size(); i++) {
      std::string txhashstr = inblk.transaction(i);
      if (txhashstr.size() != sizeof(evm_uint256be)) {
        LOG4CPLUS_ERROR(log4cplus::Logger::getInstance("com.vmware.concord"),
                        "Invalid hash length " << txhashstr.size());
        throw EVMException("Invalid transaction hash length");
      }
      evm_uint256be txhash;
      std::copy(txhashstr.begin(), txhashstr.end(), txhash.bytes);
      outblk.transactions.push_back(txhash);
    }

    if (inblk.has_timestamp()) {
      outblk.timestamp = inblk.timestamp();
    } else {
      outblk.timestamp = 0;
    }

    assert(inblk.has_gas_limit());
    outblk.gas_limit = inblk.gas_limit();

    assert(inblk.has_gas_used());
    outblk.gas_used = inblk.gas_used();

    return outblk;
  } else {
    LOG4CPLUS_ERROR(log4cplus::Logger::getInstance("com.vmware.concord"),
                    "Unknown block storage version " << inblk.version());
    throw EVMException("Unkown block storage version");
  }
}

}  // namespace common
}  // namespace concord
