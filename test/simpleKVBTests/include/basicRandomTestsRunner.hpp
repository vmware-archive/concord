// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef BASIC_RANDOM_TESTS_HPP
#define BASIC_RANDOM_TESTS_HPP

#include "Logger.hpp"
#include "simpleKVBTestsBuilder.hpp"
#include "storage/blockchain_interfaces.h"

namespace BasicRandomTests {

class BasicRandomTestsRunner {
 public:
  BasicRandomTestsRunner(concordlogger::Logger &logger,
                         concord::storage::IClient &client,
                         size_t numOfOperations);
  ~BasicRandomTestsRunner() { delete testsBuilder_; }
  void run();

 private:
  static void sleep(int ops);
  bool isReplyCorrect(RequestType requestType, const SimpleReply *expectedReply,
                      const char *reply, size_t expectedReplySize,
                      uint32_t actualReplySize);

 private:
  concordlogger::Logger &logger_;
  concord::storage::IClient &client_;
  const size_t numOfOperations_;
  TestsBuilder *testsBuilder_;
};

}  // namespace BasicRandomTests

#endif
