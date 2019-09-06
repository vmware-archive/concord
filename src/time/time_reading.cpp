// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "time_reading.hpp"

#include <google/protobuf/util/time_util.h>
#include <log4cplus/loggingmacros.h>
#include <chrono>

#include "config/configuration_manager.hpp"

using concord::config::ConcordConfiguration;
using std::chrono::system_clock;

namespace concord {
namespace time {

// Return true if the time service is enabled, or false if it is disabled.
bool IsTimeServiceEnabled(const ConcordConfiguration &config) {
  return config.getValue<bool>("FEATURE_time_service");
}

// Read milliseconds since the UNIX Epoch, according to the system clock.
//
// Eventually this should take a Config object, and use it to decide how to read
// the time.
google::protobuf::Timestamp ReadTime() {
  system_clock::time_point now = system_clock::now();
  system_clock::duration since_epoch = now.time_since_epoch();
  return google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch)
          .count());
}

}  // namespace time
}  // namespace concord
