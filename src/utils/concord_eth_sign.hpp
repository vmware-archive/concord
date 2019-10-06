// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Ethereum Signature verification.

#ifndef UTILS_CONCORD_ETH_SIGN_HPP
#define UTILS_CONCORD_ETH_SIGN_HPP

#include <log4cplus/loggingmacros.h>
#include <secp256k1.h>

#include "evmjit.h"

namespace concord {
namespace utils {

class EthSign {
 public:
  EthSign();
  ~EthSign();

  std::vector<uint8_t> sign(const evmc_uint256be hash,
                            const evmc_uint256be key) const;

  evmc_address ecrecover(const evmc_uint256be hash, const uint8_t version,
                         const evmc_uint256be r, const evmc_uint256be s) const;

 private:
  log4cplus::Logger logger;
  secp256k1_context *ctx;
};

}  // namespace utils
}  // namespace concord

#endif  // UTILS_CONCORD_ETH_SIGN_HPP
