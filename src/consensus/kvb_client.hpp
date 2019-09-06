// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Layer between api_connection and IClient

#ifndef CONCORD_CONSENSUS_KVB_CLIENT_HPP_
#define CONCORD_CONSENSUS_KVB_CLIENT_HPP_

#include <log4cplus/loggingmacros.h>
#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <vector>

#include "concord.pb.h"
#include "consensus/client_imp.h"
#include "storage/blockchain_interfaces.h"
#include "time/time_pusher.hpp"

namespace concord {

// This breaks the circular dependency between TimePusher and
// KVBClient/KVBClientPool
namespace time {
class TimePusher;
}  // namespace time

namespace consensus {

class KVBClient {
 private:
  concord::storage::IClient *client_;
  std::chrono::milliseconds timeout_;
  std::shared_ptr<concord::time::TimePusher> timePusher_;
  log4cplus::Logger logger_;
  static constexpr size_t OUT_BUFFER_SIZE = 512000;
  char m_outBuffer[OUT_BUFFER_SIZE];

 public:
  KVBClient(concord::storage::IClient *client,
            std::chrono::milliseconds timeout,
            std::shared_ptr<concord::time::TimePusher> timePusher)
      : client_(client),
        timeout_(timeout),
        timePusher_(timePusher),
        logger_(log4cplus::Logger::getInstance("com.vmware.concord")) {}

  ~KVBClient() {
    client_->stop();
    releaseClient(client_);
  }

  bool send_request_sync(com::vmware::concord::ConcordRequest &req,
                         bool isReadOnly,
                         com::vmware::concord::ConcordResponse &resp);
};

class KVBClientPool {
 private:
  log4cplus::Logger logger_;
  boost::lockfree::queue<KVBClient *> clients_;
  std::shared_ptr<concord::time::TimePusher> time_pusher_;

 public:
  // Constructor for KVBClientPool. clients should be a vector of pointers
  // to the pointers this KVBClientPool is to manage, and time_pusher should
  // be a shared_ptr to the TimePusher shared by these clients; this shared
  // pointer should point to the same thing as the shared TimePusher pointer
  // each client under management here was constucted with; we do not
  // currently define what the behavior will be if this precondition is not
  // met. This thing that this KVBClientPool and the KVBClients in manages
  // point to should be a valid TimePusher if the time service is enabled
  // and should be a null pointer if the time service is disabled.
  KVBClientPool(std::vector<KVBClient *> &clients,
                std::shared_ptr<concord::time::TimePusher> time_pusher);
  ~KVBClientPool();

  bool send_request_sync(com::vmware::concord::ConcordRequest &req,
                         bool isReadOnly,
                         com::vmware::concord::ConcordResponse &resp);

  // Reconfigure the time period for the TimePusher (if any) managed by this
  // KVBClientPool and shared by its KVBClients. Calls to this function will be
  // ignored if this KVBClientPool was created without a TimePusher.
  void SetTimePusherPeriod(const google::protobuf::Duration &period);

  // Check whether this KVBClientPool was constructed with a TimePusher. It is
  // expected that this should be effectively equivalent to checking whether the
  // time service is enabled and the Concord node this is running on is a time
  // source if the preconditions of the KVBClientPool constructor were met when
  // this instance was constructed.
  bool HasTimePusher();
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_KVB_CLIENT_HPP_
