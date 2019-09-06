// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Logging utilities

#ifndef COMMON_CONCORD_LOG_HPP
#define COMMON_CONCORD_LOG_HPP

// TODO: We may want "release" versions of these macros that print fewer bytes.

#include <ostream>
#include <vector>
#include "evm.h"

namespace concord {
namespace common {

struct HexPrintVector {
  std::vector<uint8_t> const& vec;
};

struct HexPrintBytes {
  const char* bytes;
  const uint32_t size;
};

std::ostream& operator<<(std::ostream& s, HexPrintVector v);
std::ostream& operator<<(std::ostream& s, HexPrintBytes p);
std::ostream& operator<<(std::ostream& s, const evm_uint256be& u);
std::ostream& operator<<(std::ostream& s, const evm_address& u);
std::ostream& operator<<(std::ostream& s, struct evm_message msg);
std::ostream& operator<<(std::ostream& s, evm_call_kind kind);

}  // namespace common
}  // namespace concord

#endif  // COMMON_CONCORD_LOG_HPP
