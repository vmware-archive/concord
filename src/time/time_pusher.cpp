// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "time_pusher.hpp"

#include <google/protobuf/util/time_util.h>
#include <log4cplus/loggingmacros.h>
#include <chrono>
#include <mutex>
#include <thread>

#include "concord.pb.h"
#include "config/configuration_manager.hpp"
#include "consensus/kvb_client.hpp"
#include "time/time_exception.hpp"
#include "time/time_reading.hpp"

using com::vmware::concord::ConcordRequest;
using com::vmware::concord::ConcordResponse;
using com::vmware::concord::TimeRequest;
using com::vmware::concord::TimeSample;
using concord::consensus::KVBClientPool;
using concord::time::TimePusher;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

TimePusher::TimePusher(const concord::config::ConcordConfiguration &config,
                       const concord::config::ConcordConfiguration &nodeConfig)
    : logger_(log4cplus::Logger::getInstance("concord.time.pusher")),
      stop_(false),
      lastPublishTime_(TimeUtil::GetEpoch()),
      signer_(std::unique_ptr<TimeSigner>{}) {
  if (!concord::time::IsTimeServiceEnabled(config)) {
    throw TimeException(
        "Time service is not enabled. TimePusher should not be created.");
  }

  if (nodeConfig.hasValue<int32_t>("time_pusher_period_ms")) {
    period_ = TimeUtil::MillisecondsToDuration(
        nodeConfig.getValue<int32_t>("time_pusher_period_ms"));
  } else {
    period_ = TimeUtil::MillisecondsToDuration(0);
  }

  if (nodeConfig.hasValue<std::string>("time_source_id")) {
    timeSourceId_ = nodeConfig.getValue<std::string>("time_source_id");
  } else {
    timeSourceId_ = "";
  }

  if (config.hasValue<string>("time_verification")) {
    if (config.getValue<string>("time_verification") == "rsa-time-signing") {
      signer_.reset(new RSATimeSigner(nodeConfig));
    } else if ((config.getValue<string>("time_verification") !=
                "bft-client-proxy-id") &&
               (config.getValue<string>("time_verification") != "none")) {
      throw invalid_argument(
          "Cannot construct TimePusher: Unrecognized selection for "
          "time_verification in configuration: \"" +
          config.getValue<string>("time_verification") + "\".");
    }
  }
}

void TimePusher::Start(KVBClientPool *clientPool) {
  std::lock_guard<std::mutex> lock(threadMutex_);
  run_requested_ = true;
  DoStart(clientPool);
}

void TimePusher::Stop() {
  std::lock_guard<std::mutex> lock(threadMutex_);
  run_requested_ = false;
  DoStop();
}

void TimePusher::SetPeriod(const Duration &period) {
  // Optimization to avoid unnecessarily acquiring the mutex and stopping and
  // restarting the pusher thread in the event this request doesn't actually
  // change the current period.
  if (period == period_) {
    return;
  }

  std::lock_guard<std::mutex> lock(threadMutex_);
  if ((TimeUtil::DurationToMilliseconds(period) <= 0) &&
      pusherThread_.joinable()) {
    DoStop();
  }
  period_ = period;
  if (run_requested_ && (TimeUtil::DurationToMilliseconds(period) > 0) &&
      (!pusherThread_.joinable())) {
    DoStart(clientPool_);
  }
}

void TimePusher::AddTimeToCommand(ConcordRequest &command) {
  AddTimeToCommand(command, ReadTime());
}

void TimePusher::AddTimeToCommand(ConcordRequest &command, Timestamp time) {
  TimeRequest *tr = command.mutable_time_request();

  // Only add a sample if there isn't one, to allow tests to specify samples for
  // their requests.
  if (!tr->has_sample()) {
    TimeSample *ts = tr->mutable_sample();
    ts->set_source(timeSourceId_);
    Timestamp *t = new Timestamp(time);
    ts->set_allocated_time(t);
    if (signer_) {
      vector<uint8_t> signature = signer_->Sign(time);
      ts->set_signature(signature.data(), signature.size());
    }
  }

  lastPublishTime_ = time;
}

void TimePusher::DoStart(KVBClientPool *clientPool) {
  // TimePusher's implementation is buggy if it attempts to start itself when
  // its user/consumer hasn't set it to do so.
  assert(run_requested_);

  if ((clientPool_ != clientPool) && (pusherThread_.joinable())) {
    DoStop();
  }

  clientPool_ = clientPool;
  if (!clientPool_) {
    LOG4CPLUS_ERROR(logger_,
                    "Not starting thread: no clientPool to push with.");
    return;
  }

  if (timeSourceId_.empty()) {
    LOG4CPLUS_INFO(logger_,
                   "Not starting thread: no time_source_id configured.");
    return;
  }

  if (TimeUtil::DurationToMilliseconds(period_) <= 0) {
    LOG4CPLUS_INFO(logger_, "TimePusher thread not running: period is "
                                << period_ << " (less than or equal to zero).");
    return;
  }

  if (pusherThread_.joinable()) {
    LOG4CPLUS_INFO(logger_, "Ignoring duplicate start request.");
    return;
  }

  pusherThread_ = std::thread(&TimePusher::ThreadFunction, this);
}

void TimePusher::DoStop() {
  if (!pusherThread_.joinable()) {
    LOG4CPLUS_INFO(logger_, "Ignoring stop request - nothing to stop");
    return;
  }

  stop_ = true;
  pusherThread_.join();

  // allows the thread to be restarted, if we like
  stop_ = false;
}

void TimePusher::ThreadFunction() {
  LOG4CPLUS_INFO(logger_, "Thread started with period " << period_ << ".");
  ConcordRequest req;
  ConcordResponse resp;

  while (!stop_) {
    // Sleeping for a static amount of time, instead of taking into account how
    // recently the last publish time was, means we might wait up to
    // 2*periodMilliseconds_ before publishing, but it also prevents silly 1ms
    // sleeps.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(TimeUtil::DurationToMilliseconds(period_)));

    Timestamp time = ReadTime();
    if (time < lastPublishTime_ + period_) {
      // Time was published by a transaction recently - no need to publish again
      // right now.
      continue;
    }

    try {
      AddTimeToCommand(req, time);
      clientPool_->send_request_sync(req, false /* not read-only */, resp);
      req.Clear();
      resp.Clear();
    } catch (...) {
      // We don't want this thread to die for any reason other than being shut
      // down, because we don't have anything monitoring to restart it if it
      // does. So we'll swallow all exceptions and just yell into the log about
      // any problems and wait for an admin to notice.
      LOG4CPLUS_ERROR(logger_, "Unable to send time update");
    }
  }
}
