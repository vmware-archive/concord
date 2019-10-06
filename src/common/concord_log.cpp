// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Logging utilities

#include "concord_log.hpp"

#include <ios>

#include "consensus/hex_tools.h"
#include "evmjit.h"

namespace concord {
namespace common {

// Print a vector of bytes as its 0x<hex> representation.
std::ostream& operator<<(std::ostream& s, const HexPrintVector v) {
  return hexPrint(s, &v.vec[0], v.vec.size());
};

// Print a char* of bytes as its 0x<hex> representation.
std::ostream& operator<<(std::ostream& s, const HexPrintBytes p) {
  return hexPrint(s, reinterpret_cast<const uint8_t*>(p.bytes), p.size);
};

// Print an evmc_address as its 0x<hex> representation.
std::ostream& operator<<(std::ostream& s, const evmc_address& a) {
  return hexPrint(s, a.bytes, sizeof(evmc_address));
};

// Print an evmc_uint256be as its 0x<hex> representation.
std::ostream& operator<<(std::ostream& s, const evmc_uint256be& u) {
  return hexPrint(s, u.bytes, sizeof(evmc_uint256be));
};

std::ostream& operator<<(std::ostream& s, evmc_call_kind kind) {
  switch (kind) {
    case EVMC_CALL:
      s << "EVMC_CALL";
      break;
    case EVMC_DELEGATECALL:
      s << "EVMC_DELEGATECALL";
      break;
    case EVMC_CALLCODE:
      s << "EVMC_CALLCODE";
      break;
    case EVMC_CREATE:
      s << "EVMC_CREATE";
      break;
  }
  return s;
}

std::ostream& operator<<(std::ostream& s, struct evmc_message msg) {
  s << "\nMessage: {\ndestination: " << msg.destination
    << "\nsender: " << msg.sender << "\nether: " << msg.value
    << "\ncall_kind: " << msg.kind << "\ndepth: " << msg.depth
    << "\ngas: " << msg.gas << "\ninput size: " << msg.input_size;
  s << "\n}\n";
  return s;
};

}  // namespace common
}  // namespace concord
