// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Time pusher is a thread to make sure that this Concord node publishes its
// time at least once within some configured period.
//
// The implementation wakes up once/period, and checks if any transactions were
// sent while this thread was sleeping, and only sends a transaction if no
// others were.

#ifndef TIME_TIME_PUSHER_HPP
#define TIME_TIME_PUSHER_HPP

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <log4cplus/loggingmacros.h>
#include <mutex>
#include <thread>

#include "concord.pb.h"
#include "config/configuration_manager.hpp"
#include "consensus/kvb_client.hpp"
#include "time/time_signing.hpp"

namespace concord {

// This breaks the circular dependency between TimePusher and
// KVBClient/KVBClientPool
namespace consensus {
class KVBClientPool;
}  // namespace consensus

namespace time {

class TimePusher {
 public:
  // Constructor for TimePusher. Arguments:
  //   - config: ConcordConfiguration for this Concord cluster.
  //   - nodeConfig: node-specific configuration for the node for which this
  //   time pusher will submit updates. Note that the time source ID and
  //   associated private key will be read from this configuration. An
  //   std::invalid_argument may be thrown if an appropriate source ID and
  //   private key cannot be founde in nodeConfig or either of the
  //   ConcordConfigurations passed to the constructor otherwise do not meet the
  //   expectations of the time service.
  //   - clientPool: KVBClientPool through which this TimePusher will publish
  //   its updates.
  explicit TimePusher(const concord::config::ConcordConfiguration &config,
                      const concord::config::ConcordConfiguration &nodeConfig);

  void Start(concord::consensus::KVBClientPool *clientPool);
  void Stop();

  // Set the maximum period with which this TimePusher should publish time
  // updates (it will proactively publish time updates in order to meet this
  // period if AddTimeToCommand is not used frequently enough). Note this
  // function should be thread safe if called from multiple threads. If this
  // TimePusher has a running pusher thread, changing its period to a
  // non-positive one will stop the pusher thread. Changing the period to a
  // positive one will (re)start the pusher thread if period non-positivity is
  // the only reason the thread was not running.
  void SetPeriod(const google::protobuf::Duration &period);

  void AddTimeToCommand(com::vmware::concord::ConcordRequest &command);

 private:
  void AddTimeToCommand(com::vmware::concord::ConcordRequest &command,
                        google::protobuf::Timestamp time);

  // Internal implementations of Start and Stop which do not change the
  // user/consumer of the TimePusher's intent for it to be started or stopped
  // (this intent is recorded in the run_requested_ private field). Both these
  // helper functions expect the threadMutex_ is held while they are called as a
  // precondition.
  void DoStart(concord::consensus::KVBClientPool *clientPool);
  void DoStop();

 private:
  log4cplus::Logger logger_;
  concord::consensus::KVBClientPool *clientPool_;
  bool run_requested_;
  bool stop_;
  google::protobuf::Timestamp lastPublishTime_;

  google::protobuf::Duration period_;
  std::string timeSourceId_;
  std::unique_ptr<concord::time::TimeSigner> signer_;

  std::thread pusherThread_;
  std::mutex threadMutex_;

  void ThreadFunction();
};

}  // namespace time
}  // namespace concord

#endif  // TIME_TIME_PUSHER_HPP
