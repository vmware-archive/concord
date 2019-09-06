// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Ethereum Signature verification.

#ifndef UTILS_CONCORD_ETH_SIGN_HPP
#define UTILS_CONCORD_ETH_SIGN_HPP

#include <log4cplus/loggingmacros.h>
#include <secp256k1.h>

#include "evm.h"

namespace concord {
namespace utils {

class EthSign {
 public:
  EthSign();
  ~EthSign();

  std::vector<uint8_t> sign(const evm_uint256be hash,
                            const evm_uint256be key) const;

  evm_address ecrecover(const evm_uint256be hash, const uint8_t version,
                        const evm_uint256be r, const evm_uint256be s) const;

 private:
  log4cplus::Logger logger;
  secp256k1_context *ctx;
};

}  // namespace utils
}  // namespace concord

#endif  // UTILS_CONCORD_ETH_SIGN_HPP
