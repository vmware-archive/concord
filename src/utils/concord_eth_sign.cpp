// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Ethereum Signature verification.

#include "concord_eth_sign.hpp"

#include <log4cplus/loggingmacros.h>
#include <secp256k1_recovery.h>
#include <iostream>

#include "concord_eth_hash.hpp"

namespace concord {
namespace utils {

// TODO: sort out build structure, to pull this from concord_types
const evm_address zero_address{{0}};

EthSign::EthSign()
    : logger(log4cplus::Logger::getInstance("com.vmware.concord.eth_sign")) {
  ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN |
                                 SECP256K1_CONTEXT_VERIFY);
}

EthSign::~EthSign() { secp256k1_context_destroy(ctx); }

/**
 * Sign hash with private key.
 */
std::vector<uint8_t> EthSign::sign(const evm_uint256be hash,
                                   const evm_uint256be key) const {
  std::vector<uint8_t> serializedSignature;
  secp256k1_ecdsa_recoverable_signature signature;

  // nullptrs == use default nonce generator
  if (secp256k1_ecdsa_sign_recoverable(ctx, &signature, hash.bytes, key.bytes,
                                       nullptr, nullptr)) {
    unsigned char signatureTemp[64];
    int version;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(
        ctx, signatureTemp, &version, &signature);

    // TODO: need to compare to a large number statically defined in
    // cpp-ethereum; is it just an overflow check?
    uint8_t uv = (uint8_t)version;
    serializedSignature.push_back(uv);
    std::copy(signatureTemp, signatureTemp + 64,
              std::back_inserter(serializedSignature));
  }
  return serializedSignature;
}

/**
 * Recover the "from" address from a transaction signature.
 */
evm_address EthSign::ecrecover(const evm_uint256be hash, const uint8_t version,
                               const evm_uint256be r,
                               const evm_uint256be s) const {
  // parse_compact supports 0-3, but Ethereum is documented as only using 0 and
  // 1
  if (version > 1) {
    return zero_address;
  }

  std::vector<uint8_t> signature;
  std::copy(r.bytes, r.bytes + sizeof(evm_uint256be),
            std::back_inserter(signature));
  std::copy(s.bytes, s.bytes + sizeof(evm_uint256be),
            std::back_inserter(signature));

  secp256k1_ecdsa_recoverable_signature ecsig;
  if (!secp256k1_ecdsa_recoverable_signature_parse_compact(
          ctx, &ecsig, (unsigned char*)&signature[0], version)) {
    return zero_address;
  }

  secp256k1_pubkey ecpubkey;
  if (!secp256k1_ecdsa_recover(ctx, &ecpubkey, &ecsig, hash.bytes)) {
    return zero_address;
  }

  size_t pubkeysize = 65;
  unsigned char pubkey[65];
  secp256k1_ec_pubkey_serialize(ctx, pubkey, &pubkeysize, &ecpubkey,
                                SECP256K1_EC_UNCOMPRESSED);

  assert(pubkey[0] == 4);
  assert(pubkeysize == 65);
  assert(pubkeysize > 1);
  // skip the version byte at [0]
  evm_uint256be pubkeyhash =
      eth_hash::keccak_hash((uint8_t*)(pubkey + 1), pubkeysize - 1);

  evm_address address;
  std::copy(pubkeyhash.bytes + (sizeof(evm_uint256be) - sizeof(evm_address)),
            pubkeyhash.bytes + sizeof(evm_uint256be), address.bytes);

  return address;
}

/**
 * Verify a transaction's signature.
 */
// bool com::vmware::concord::EthSign::ecverify(const EthTransaction &tx) const
// {
//    evm_address recoveredAddr = ecrecover(tx.hash(), tx.sig_v, tx.sig_r,
//    tx.sig_s); return recoveredAddr != zero_address && recoveredAddr ==
//    tx.from;
// }

}  // namespace utils
}  // namespace concord
