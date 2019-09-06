// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "basicRandomTestsRunner.hpp"
#include <assert.h>
#include <chrono>

#ifndef _WIN32
#include <unistd.h>
#endif

using std::chrono::seconds;

using concord::storage::IClient;

namespace BasicRandomTests {

BasicRandomTestsRunner::BasicRandomTestsRunner(concordlogger::Logger &logger,
                                               IClient &client,
                                               size_t numOfOperations)
    : logger_(logger), client_(client), numOfOperations_(numOfOperations) {
  testsBuilder_ = new TestsBuilder(logger_, client);
}

void BasicRandomTestsRunner::sleep(int ops) {
#ifndef _WIN32
  if (ops % 100 == 0) usleep(100 * 1000);
#endif
}

void BasicRandomTestsRunner::run() {
  assert(!client_.isRunning());

  testsBuilder_->createRandomTest(numOfOperations_, 1111);
  client_.start();

  RequestsList requests = testsBuilder_->getRequests();
  RepliesList expectedReplies = testsBuilder_->getReplies();
  assert(requests.size() == expectedReplies.size());

  int ops = 0;
  while (!requests.empty()) {
    sleep(ops);
    SimpleRequest *request = requests.front();
    SimpleReply *expectedReply = expectedReplies.front();
    requests.pop_front();
    expectedReplies.pop_front();

    bool readOnly = (request->type != COND_WRITE);
    size_t requestSize = TestsBuilder::sizeOfRequest(request);
    size_t expectedReplySize = TestsBuilder::sizeOfReply(expectedReply);
    uint32_t actualReplySize = 0;
    char reply[expectedReplySize];

    client_.invokeCommandSynch((char *)request, requestSize, readOnly,
                               seconds(5), expectedReplySize, reply,
                               &actualReplySize);

    if (isReplyCorrect(request->type, expectedReply, reply, expectedReplySize,
                       actualReplySize))
      ops++;
  }
  sleep(1);
  LOG_INFO(logger_,
           "\n*** Test completed. " << ops << " messages have been handled.");
  client_.stop();
}

bool BasicRandomTestsRunner::isReplyCorrect(RequestType requestType,
                                            const SimpleReply *expectedReply,
                                            const char *reply,
                                            size_t expectedReplySize,
                                            uint32_t actualReplySize) {
  if (actualReplySize != expectedReplySize) {
    LOG_ERROR(logger_, "*** Test failed: actual reply size != expected");
    assert(0);
  }
  std::stringstream error;
  switch (requestType) {
    case COND_WRITE:
      if (((SimpleReply_ConditionalWrite *)expectedReply)
              ->isEquiv(*(SimpleReply_ConditionalWrite *)reply, error))
        return true;
      break;
    case READ:
      if (((SimpleReply_Read *)expectedReply)
              ->isEquiv(*(SimpleReply_Read *)reply, error))
        return true;
      break;
    case GET_LAST_BLOCK:
      if (((SimpleReply_GetLastBlock *)expectedReply)
              ->isEquiv(*(SimpleReply_GetLastBlock *)reply, error))
        return true;
      break;
    default:;
  }

  LOG_ERROR(logger_, "*** Test failed: actual reply != expected; error: "
                         << error.str());
  assert(0);
}

}  // namespace BasicRandomTests
