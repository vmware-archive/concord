// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// concord common Utilities.

#ifndef UTILS_CONCORD_UTILS_HPP
#define UTILS_CONCORD_UTILS_HPP

#include <evmc/evmc.h>
#include <log4cplus/loggingmacros.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include "json.hpp"

namespace concord {
namespace utils {

std::vector<uint8_t> dehex(const std::string &str);

void to_evmc_uint256be(uint64_t val, evmc_uint256be *ret);

uint64_t from_evmc_uint256be(const evmc_uint256be *val);

evmc_uint256be from_uint256_t(const boost::multiprecision::uint256_t *val);

boost::multiprecision::uint256_t to_uint256_t(const evmc_uint256be *val);

int64_t get_epoch_millis();

std::ostream &hexPrint(std::ostream &s, const uint8_t *data, size_t size);

}  // namespace utils
}  // namespace concord

#endif  // UTILS_CONCORD_UTILS_HPP
