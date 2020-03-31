# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set(concord_time_sources
  time_contract.cpp time_contract.hpp
  time_exception.hpp
  time_pusher.cpp time_pusher.hpp
  time_reading.cpp time_reading.hpp
  time_verification.cpp time_verification.hpp)

add_library(concord_time ${concord_time_sources})

target_link_libraries(concord_time PUBLIC concord_consensus proto concordbft_storage kvbc util)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

# time_signing needs to include/link the RSASigner/RSAVerifier classes from
# Concord-BFT, at the time of this writing declared in
# bftengine/src/bftengine/Crypto.hpp in Concord-BFT.
target_link_libraries(concord_time PUBLIC corebft)
target_include_directories(concord_time PUBLIC ${bftengine_SOURCE_DIR})

