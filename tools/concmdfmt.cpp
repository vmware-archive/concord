// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Formatting utilities for command line tools.

#include "concmdfmt.hpp"
#include <assert.h>
#include <ostream>
#include "concmdex.hpp"

char hexval(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  } else if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  } else {
    throw ConcCmdException("non-hex character");
  }
}

/**
 * Convert a 0x-hex string to plain bytes. 0x prefix is optional.
 *   "0x1234" -> {18, 52}
 */
void dehex0x(const std::string &str, std::string &bin /* out */) {
  if (str.size() % 2 != 0) {
    throw ConcCmdException("nibble missing in string");
  }

  // allow people to include "0x" prefix, or not
  size_t adjust = (str[0] == '0' && str[1] == 'x') ? 2 : 0;

  size_t binsize = (str.size() - adjust) / 2;

  if (binsize > 0) {
    bin.resize(binsize);
    for (size_t i = 0; i < binsize; i++) {
      bin[i] =
          (hexval(str[i * 2 + adjust]) << 4) | hexval(str[i * 2 + adjust + 1]);
    }
  } else {
    bin.assign("");
  }
}

char hexchar(char c) {
  assert(c < 16);
  return "0123456789abcdef"[(uint8_t)c];
}

/**
 * Convert a vector of bytes into a 0x-hex string.
 *   {18, 52} -> "0x1234"
 */
void hex0x(const std::string &in, std::string &out /* out */) {
  out.assign("0x");
  for (auto s = in.begin(); s < in.end(); s++) {
    out.push_back(hexchar(((*s) >> 4) & 0xf));
    out.push_back(hexchar((*s) & 0xf));
  }
}
