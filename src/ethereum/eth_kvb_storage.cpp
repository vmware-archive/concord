// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Wrapper around KVB to provide EVM execution storage context. This class
// defines the mapping of EVM object to KVB address. It also records updates to
// be used in minting a block when a transaction finishes.
//
// Initializing a EthKvbStorage object without an IBlocksAppender causes it to
// operate in read-only mode. A ReadOnlyModeException will be thrown if any of
// the set/add/write functions are called on a EthKvbStorage object in read-only
// mode.
//
// To add a block, first call the set/add functions to prepare data for the
// block. When all data has been prepared, call `write_block`. After calling
// `write_block`, the staging area is cleared, and more objects can be prepared
// for a new block, if desired.
//
// After calling set/add/write, a copy of the data has been made, which is
// managed by this object. The original value passed to the set/add/write
// function can be safely destroyed or modified.
//
// KVBlockchain writes a block as a set of key-value pairs. We use the first
// byte of a key to signify the type of the value (see the `TYPE_*` constants in
// `kvb_storage.hpp`. Values are mostly Protocol Buffer encodings, defined in
// `concord_storage.proto`, with the exception being contract data (not code).
// All protobuf messages include a "version" field, so we can handle upgrades
// to storage at a later date.
//
// Storage layouts:
//
// * Block
//   - Key: kKvbKeyEthBlock+[block hash (32 bytes)]
//   - Value: com::vmware::concord::kvb::Block protobuf
//   - Notes: Do not confuse this with the KVB block. This is Ethereum-level
//            block information.
//
// * Transaction
//   - Key: kKvbKeyEthTransaction+[transaction hash (32 bytes)]
//   - Value: com::vmware::concord::kvb::Transaction protobuf
//
// * Account or Contract Balance
//   - Key: kKvbKeyEthBalance+[account/contract address (20 bytes)]
//   - Value: com::vmware::concord::kvb::Balance protobuf
//   - Notes: Yes, it seems a little overkill to wrap a number in a protobuf
//            encoding, but this saves hassle with endian encoding.
//
// * Contract Code
//   - Key: kKvbKeyEthCode+[contract address (20 bytes)]
//   - Value: com::vmware::concord::kvb::Code protobuf
//
// * Contract Data
//   - Key: kKvbKeyEthStorage+[contract address (20 bytes)]+[location (32
//   bytes)]
//   - Value: 32 bytes directly copied from an evm_uint256be
//   - Notes: aka "storage"
//
// * Account Nonce
//   - Key: kKvbKeyEthNonce+[account address (20 bytes)]
//   - Value: com::vmware::concord::kvb::Nonce protobuf
//   - Notes: As with balance, using protobuf solves encoding issues.

#include "eth_kvb_storage.hpp"

#include <cstring>
#include <vector>

#include "common/concord_exception.hpp"
#include "concord_storage.pb.h"
#include "evmc/evmc.h"
#include "storage/kvb_key_types.h"
#include "utils/concord_eth_hash.hpp"

using concord::common::BlockNotFoundException;
using concord::common::EthBlock;
using concord::common::EthTransaction;
using concord::common::EVMException;
using concord::common::ReadOnlyModeException;
using concord::common::TransactionNotFoundException;
using concord::common::zero_hash;
using concord::common::operator<<;
using concord::storage::blockchain::IBlocksAppender;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnly;
using concordUtils::BlockId;
using concordUtils::SetOfKeyValuePairs;
using concordUtils::Sliver;
using concordUtils::Status;

using concord::storage::kKvbKeyEthBalance;
using concord::storage::kKvbKeyEthBlock;
using concord::storage::kKvbKeyEthCode;
using concord::storage::kKvbKeyEthNonce;
using concord::storage::kKvbKeyEthStorage;
using concord::storage::kKvbKeyEthTransaction;

namespace concord {
namespace ethereum {

////////////////////////////////////////
// GENERAL

/* Current storage versions. */
const int64_t balance_storage_version = 1;
const int64_t nonce_storage_version = 1;
const int64_t code_storage_version = 1;

// read-only mode
EthKvbStorage::EthKvbStorage(const ILocalKeyValueStorageReadOnly &roStorage)
    : roStorage_(roStorage),
      blockAppender_(nullptr),
      logger(log4cplus::Logger::getInstance("com.vmware.concord.kvb")) {}

// read-write mode
EthKvbStorage::EthKvbStorage(const ILocalKeyValueStorageReadOnly &roStorage,
                             IBlocksAppender *blockAppender)
    : roStorage_(roStorage),
      blockAppender_(blockAppender),
      logger(log4cplus::Logger::getInstance("com.vmware.concord.kvb")) {}

EthKvbStorage::~EthKvbStorage() {
  // Any Slivers in updates will release their memory automatically.

  // We don't own the blockAppender we're pointing to, so leave it alone.
}

bool EthKvbStorage::is_read_only() {
  // if we don't have a blockAppender, we are read-only
  auto res = blockAppender_ == nullptr;
  return res;
}

/**
 * Allow access to read-only storage object, to enabled downgrades to read-only
 * EthKvbStorage when convenient.
 */
const ILocalKeyValueStorageReadOnly &EthKvbStorage::getReadOnlyStorage() {
  return roStorage_;
}

////////////////////////////////////////
// ADDRESSING

/**
 * Constructs a key: one byte of `type`, concatenated with `length` bytes of
 * `bytes`.
 */
Sliver EthKvbStorage::kvb_key(uint8_t type, const uint8_t *bytes,
                              size_t length) const {
  char *key = new char[1 + length];
  key[0] = type;
  std::copy(bytes, bytes + length, key + 1);
  return Sliver(key, length + 1);
}

/**
 * Convenience functions for constructing a key for each object type.
 */
Sliver EthKvbStorage::block_key(const EthBlock &blk) const {
  return kvb_key(kKvbKeyEthBlock, blk.get_hash().bytes, sizeof(evmc_uint256be));
}

Sliver EthKvbStorage::block_key(const evmc_uint256be &hash) const {
  return kvb_key(kKvbKeyEthBlock, hash.bytes, sizeof(hash));
}

Sliver EthKvbStorage::transaction_key(const EthTransaction &tx) const {
  return kvb_key(kKvbKeyEthTransaction, tx.hash().bytes,
                 sizeof(evmc_uint256be));
}

Sliver EthKvbStorage::transaction_key(const evmc_uint256be &hash) const {
  return kvb_key(kKvbKeyEthTransaction, hash.bytes, sizeof(hash));
}

Sliver EthKvbStorage::balance_key(const evmc_address &addr) const {
  return kvb_key(kKvbKeyEthBalance, addr.bytes, sizeof(addr));
}

Sliver EthKvbStorage::nonce_key(const evmc_address &addr) const {
  return kvb_key(kKvbKeyEthNonce, addr.bytes, sizeof(addr));
}

Sliver EthKvbStorage::code_key(const evmc_address &addr) const {
  return kvb_key(kKvbKeyEthCode, addr.bytes, sizeof(addr));
}

Sliver EthKvbStorage::storage_key(const evmc_address &addr,
                                  const evmc_uint256be &location) const {
  uint8_t combined[sizeof(addr) + sizeof(location)];
  std::copy(addr.bytes, addr.bytes + sizeof(addr), combined);
  std::copy(location.bytes, location.bytes + sizeof(location),
            combined + sizeof(addr));
  return kvb_key(kKvbKeyEthStorage, combined, sizeof(addr) + sizeof(location));
}

////////////////////////////////////////
// WRITING

/**
 * Add a key-value pair to be stored in the block. Throws ReadOnlyModeException
 * if this object is in read-only mode.
 */
void EthKvbStorage::put(const Sliver &key, const Sliver &value) {
  if (!blockAppender_) {
    throw ReadOnlyModeException();
  }

  updates[key] = value;
}

/**
 * Add a block to the database, containing all of the key-value pairs that have
 * been prepared. A ReadOnlyModeException will be thrown if this object is in
 * read-only mode.
 */
Status EthKvbStorage::write_block(uint64_t timestamp, uint64_t gas_limit) {
  if (!blockAppender_) {
    throw ReadOnlyModeException();
  }

  // Prepare the block metadata
  EthBlock blk;
  blk.number = next_block_number();

  if (blk.number == 0) {
    blk.parent_hash = zero_hash;
  } else {
    EthBlock parent = get_block(blk.number - 1);
    blk.parent_hash = parent.hash;
  }

  blk.timestamp = timestamp;
  blk.gas_limit = gas_limit;

  // We need hash of all transactions for calculating hash of a block
  // but we also need block hash inside transaction structure (not required
  // to calculate hash of that transaction). So first use transaction hash to
  // get block hash and then populate that block hash & block number inside
  // all transactions in that block
  for (auto tx : pending_transactions) {
    blk.transactions.push_back(tx.hash());
  }
  blk.hash = blk.get_hash();

  blk.gas_used = 0;
  for (auto tx : pending_transactions) {
    tx.block_hash = blk.hash;
    tx.block_number = blk.number;
    Sliver txaddr = transaction_key(tx);
    uint8_t *txser;
    size_t txser_length = tx.serialize(&txser);
    blk.gas_used += tx.gas_used;

    put(txaddr, Sliver(reinterpret_cast<char *>(txser), txser_length));
  }
  pending_transactions.clear();

  // Create serialized versions of the objects and store them in a staging area.
  add_block(blk);

  // Actually write the block
  BlockId outBlockId;
  Status status = blockAppender_->addBlock(updates, outBlockId);
  if (status.isOK()) {
    LOG4CPLUS_INFO(logger, "Appended block number " << outBlockId);
  } else {
    LOG4CPLUS_ERROR(logger, "Failed to append block");
  }

  // Prepare to stage another block
  reset();
  return status;
}

/**
 * Drop all pending updates.
 */
void EthKvbStorage::reset() {
  // Slivers release their memory automatically.
  updates.clear();
}

/**
 * Preparation functions for each value type in a block. These creates
 * serialized versions of the objects and store them in a staging area.
 */
void EthKvbStorage::add_block(EthBlock &blk) {
  Sliver blkaddr = block_key(blk);
  uint8_t *blkser;
  size_t blkser_length = blk.serialize(&blkser);

  put(blkaddr, Sliver(reinterpret_cast<char *>(blkser), blkser_length));
}

void EthKvbStorage::add_transaction(EthTransaction &tx) {
  // Like other add_* methods we don't serialized the transaction here. The
  // reason is that block hash and block number is not known at this point
  // hence we can not serialize the transaction properly. Instead we put the
  // transaction in `pending_tansactions` vector for now and then in
  // write_block properly fill block_number & block_hash inside each
  // transaction
  pending_transactions.push_back(tx);
}

void EthKvbStorage::set_balance(const evmc_address &addr,
                                evmc_uint256be balance) {
  com::vmware::concord::kvb::Balance proto;
  proto.set_version(balance_storage_version);
  proto.set_balance(balance.bytes, sizeof(evmc_uint256be));
  size_t sersize = proto.ByteSize();
  char *ser = new char[sersize];
  proto.SerializeToArray(ser, sersize);

  put(balance_key(addr), Sliver(ser, sersize));
}

void EthKvbStorage::set_nonce(const evmc_address &addr, uint64_t nonce) {
  com::vmware::concord::kvb::Nonce proto;
  proto.set_version(nonce_storage_version);
  proto.set_nonce(nonce);
  size_t sersize = proto.ByteSize();
  char *ser = new char[sersize];
  proto.SerializeToArray(ser, sersize);

  put(nonce_key(addr), Sliver(ser, sersize));
}

void EthKvbStorage::set_code(const evmc_address &addr, const uint8_t *code,
                             size_t code_size) {
  com::vmware::concord::kvb::Code proto;
  proto.set_version(code_storage_version);
  proto.set_code(code, code_size);
  evmc_uint256be hash = concord::utils::eth_hash::keccak_hash(code, code_size);
  proto.set_hash(hash.bytes, sizeof(hash));

  size_t sersize = proto.ByteSize();
  char *ser = new char[sersize];
  proto.SerializeToArray(ser, sersize);

  put(code_key(addr), Sliver(ser, sersize));
}

void EthKvbStorage::set_storage(const evmc_address &addr,
                                const evmc_uint256be &location,
                                const evmc_uint256be &data) {
  char *str = new char[sizeof(data)];
  std::copy(data.bytes, data.bytes + sizeof(data), str);
  put(storage_key(addr, location), Sliver(str, sizeof(data)));
}

////////////////////////////////////////
// READING

/**
 * Get the number of the block that will be added when write_block is called.
 */
uint64_t EthKvbStorage::next_block_number() {
  // Ethereum block number is 1+KVB block number. So, the most recent KVB block
  // number is actually the next Ethereum block number.
  return roStorage_.getLastBlock();
}

/**
 * Get the number of the most recent block that was added.
 */
uint64_t EthKvbStorage::current_block_number() {
  // Ethereum block number is 1+KVB block number. So, the most recent Ethereum
  // block is one less than the most recent KVB block.
  return roStorage_.getLastBlock() - 1;
}

/**
 * Get a value from storage. The staging area is searched first, so that it can
 * be used as a sort of current execution environment. If the key is not found
 * in the staging area, its value in the most recent block in which it was
 * written will be returned.
 */
Status EthKvbStorage::get(const Sliver &key, Sliver &value) {
  uint64_t block_number = current_block_number();
  BlockId out;
  return get(block_number, key, value, out);
}

/**
 * Get a value from storage. The staging area is searched first, so that it can
 * be used as a sort of current execution environment. If the key is not found
 * in the staging area, its value in the most recent block in which it was
 * written will be returned.
 * @param readVersion BlockId object signifying the read version with which a
 *                    lookup needs to be done.
 * @param key Sliver object of the key.
 * @param value Sliver object where the value of the lookup result is stored.
 * @param outBlock BlockId object where the read version of the result is stored
 * @return
 */
Status EthKvbStorage::get(const BlockId readVersion, const Sliver &key,
                          Sliver &value, BlockId &outBlock) {
  // TODO(BWF): this search will be very inefficient for a large set of changes
  for (auto &u : updates) {
    if (u.first == key) {
      value = u.second;
      return Status::OK();
    }
  }
  // "1+" == KVBlockchain starts at block 1, but Ethereum starts at 0
  return roStorage_.get(readVersion + 1, key, value, outBlock);
}

/**
 * Fetch functions for each value type.
 */
EthBlock EthKvbStorage::get_block(uint64_t number) {
  SetOfKeyValuePairs outBlockData;

  // "1+" == KVBlockchain starts at block 1, but Ethereum starts at 0
  Status status = roStorage_.getBlockData(1 + number, outBlockData);

  LOG4CPLUS_DEBUG(logger, "Getting block number "
                              << number << " status: " << status
                              << " value.size: " << outBlockData.size());
  if (status.isOK()) {
    for (auto kvp : outBlockData) {
      if (kvp.first.data()[0] == kKvbKeyEthBlock) {
        return EthBlock::deserialize(kvp.second);
      }
    }
  }
  throw BlockNotFoundException();
}

EthBlock EthKvbStorage::get_block(const evmc_uint256be &hash) {
  Sliver kvbkey = block_key(hash);
  Sliver value;
  Status status = get(kvbkey, value);

  LOG4CPLUS_DEBUG(logger, "Getting block "
                              << hash << " status: " << status << " key: "
                              << kvbkey << " value.length: " << value.length());

  if (status.isOK() && value.length() > 0) {
    // TODO: we may store less for block, by using this part to get the number,
    // then get_block(number) to rebuild the transaction list from KV pairs
    return EthBlock::deserialize(value);
  }

  throw BlockNotFoundException();
}

EthTransaction EthKvbStorage::get_transaction(const evmc_uint256be &hash) {
  Sliver kvbkey = transaction_key(hash);
  Sliver value;
  Status status = get(kvbkey, value);

  LOG4CPLUS_DEBUG(logger, "Getting transaction "
                              << hash << " status: " << status << " key: "
                              << kvbkey << " value.length: " << value.length());

  if (status.isOK() && value.length() > 0) {
    // TODO: lookup block hash and number as well
    return EthTransaction::deserialize(value);
  }

  throw TransactionNotFoundException();
}

evmc_uint256be EthKvbStorage::get_balance(const evmc_address &addr) {
  uint64_t block_number = current_block_number();
  return get_balance(addr, block_number);
}

evmc_uint256be EthKvbStorage::get_balance(const evmc_address &addr,
                                          uint64_t &block_number) {
  Sliver kvbkey = balance_key(addr);
  Sliver value;
  BlockId outBlock;
  Status status = get(block_number, kvbkey, value, outBlock);

  LOG4CPLUS_DEBUG(logger, "Getting balance "
                              << addr
                              << " lookup block starting at: " << block_number
                              << " status: " << status << " key: " << kvbkey
                              << " value.length: " << value.length()
                              << " out block at: " << outBlock);

  evmc_uint256be out;
  if (status.isOK() && value.length() > 0) {
    com::vmware::concord::kvb::Balance balance;
    if (balance.ParseFromArray(value.data(), value.length())) {
      if (balance.version() == balance_storage_version) {
        std::copy(balance.balance().begin(), balance.balance().end(),
                  out.bytes);
        return out;
      } else {
        throw EVMException("Unknown balance storage version");
      }
    } else {
      LOG4CPLUS_ERROR(logger, "Unable to decode balance for addr " << addr);
      throw EVMException("Corrupt balance storage");
    }
  }

  // untouched accounts have a balance of 0
  return evmc_uint256be{0};
}

uint64_t EthKvbStorage::get_nonce(const evmc_address &addr) {
  uint64_t block_number = current_block_number();
  return get_nonce(addr, block_number);
}

uint64_t EthKvbStorage::get_nonce(const evmc_address &addr,
                                  uint64_t &block_number) {
  Sliver kvbkey = nonce_key(addr);
  Sliver value;
  BlockId outBlock;
  Status status = get(block_number, kvbkey, value, outBlock);

  LOG4CPLUS_DEBUG(logger, "Getting nonce "
                              << addr
                              << " lookup block starting at: " << block_number
                              << " status: " << status << " key: " << kvbkey
                              << " value.length: " << value.length()
                              << " out block at: " << outBlock);

  if (status.isOK() && value.length() > 0) {
    com::vmware::concord::kvb::Nonce nonce;
    if (nonce.ParseFromArray(value.data(), value.length())) {
      if (nonce.version() == nonce_storage_version) {
        return nonce.nonce();
      } else {
        throw EVMException("Unknown nonce storage version");
      }
    }
  }

  // untouched accounts have a nonce of 0
  return 0;
}

bool EthKvbStorage::account_exists(const evmc_address &addr) {
  Sliver kvbkey = balance_key(addr);
  Sliver value;
  Status status = get(kvbkey, value);

  LOG4CPLUS_DEBUG(logger, "Getting balance "
                              << addr << " status: " << status << " key: "
                              << kvbkey << " value.length: " << value.length());

  if (status.isOK() && value.length() > 0) {
    // if there was a balance recorded, the account exists
    return true;
  }

  return false;
}

/**
 * Code and hash will be copied to `out`, if found, and `true` will be
 * returned. If no code is found, `false` is returned.
 */
bool EthKvbStorage::get_code(const evmc_address &addr,
                             std::vector<uint8_t> &out, evmc_uint256be &hash) {
  uint64_t block_number = current_block_number();
  return get_code(addr, out, hash, block_number);
}

/**
 * Code and hash will be copied to `out`, if found, and `true` will be
 * returned. If no code is found, `false` is returned.
 * @param addr
 * @param out
 * @param hash
 * @param block_number the starting block number for lookup,
 *                     'default block parameters'
 * @return
 */
bool EthKvbStorage::get_code(const evmc_address &addr,
                             std::vector<uint8_t> &out, evmc_uint256be &hash,
                             uint64_t &block_number) {
  Sliver kvbkey = code_key(addr);
  Sliver value;
  BlockId outBlock;
  Status status = get(block_number, kvbkey, value, outBlock);

  LOG4CPLUS_DEBUG(logger, "Getting code "
                              << addr
                              << " lookup block starting at: " << block_number
                              << " status: " << status << " key: " << kvbkey
                              << " value.length: " << value.length()
                              << " out block at: " << outBlock);

  if (status.isOK() && value.length() > 0) {
    com::vmware::concord::kvb::Code code;
    if (code.ParseFromArray(value.data(), value.length())) {
      if (code.version() == code_storage_version) {
        std::copy(code.code().begin(), code.code().end(),
                  std::back_inserter(out));
        std::copy(code.hash().begin(), code.hash().end(), hash.bytes);
        return true;
      } else {
        LOG4CPLUS_ERROR(logger,
                        "Unknown code storage version" << code.version());
        throw EVMException("Unknown code storage version");
      }
    } else {
      LOG4CPLUS_ERROR(logger,
                      "Unable to decode storage for contract at " << addr);
      throw EVMException("Corrupt code storage");
    }
  }

  return false;
}

evmc_uint256be EthKvbStorage::get_storage(const evmc_address &addr,
                                          const evmc_uint256be &location) {
  uint64_t block_number = current_block_number();
  return get_storage(addr, location, block_number);
}

evmc_uint256be EthKvbStorage::get_storage(const evmc_address &addr,
                                          const evmc_uint256be &location,
                                          uint64_t &block_number) {
  Sliver kvbkey = storage_key(addr, location);
  Sliver value;
  BlockId outBlock;
  Status status = get(block_number, kvbkey, value, outBlock);

  // (IG): when running ST tests, logs are full of this line. Changed to Debug
  // level
  LOG4CPLUS_DEBUG(logger, "Getting storage "
                              << addr << " at " << location
                              << " lookup block starting at: " << block_number
                              << " status: " << status << " key: " << kvbkey
                              << " value.length: " << value.length()
                              << " out block at: " << outBlock);

  evmc_uint256be out;
  if (status.isOK() && value.length() > 0) {
    if (value.length() == sizeof(evmc_uint256be)) {
      std::copy(value.data(), value.data() + value.length(), out.bytes);
    } else {
      LOG4CPLUS_ERROR(logger, "Contract " << addr << " storage " << location
                                          << " only had " << value.length()
                                          << " bytes.");
      throw EVMException("Corrupt contract storage");
    }
  } else {
    std::memset(out.bytes, 0, sizeof(out));
  }
  return out;
}

}  // namespace ethereum
}  // namespace concord
