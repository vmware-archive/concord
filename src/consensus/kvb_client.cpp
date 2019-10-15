// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Layer between api_connection and concord::storage::IClient
//
// This is the end of the client side of Concord. Commands sent from here will
// end up at KVBCommandsHandler.

#include "kvb_client.hpp"

#include <boost/thread.hpp>

using com::vmware::concord::ConcordRequest;
using com::vmware::concord::ConcordResponse;
using com::vmware::concord::ErrorResponse;
using google::protobuf::Duration;

using concord::time::TimePusher;
using std::chrono::steady_clock;

namespace concord {
namespace consensus {

/**
 * Send a request to the replicas. Returns true if the response contains
 * something to forward (either a response message or an appropriate error
 * message). Returns false if the response is empty (for example, if parsing
 * failed).
 */
bool KVBClient::send_request_sync(ConcordRequest &req, bool isReadOnly,
                                  ConcordResponse &resp) {
  if (!isReadOnly && timePusher_) {
    timePusher_->AddTimeToCommand(req);
  }

  std::string command;
  req.SerializeToString(&command);
  memset(m_outBuffer, 0, OUT_BUFFER_SIZE);

  uint32_t actualReplySize = 0;
  timing_bft_.Start();
  concordUtils::Status status = client_->invokeCommandSynch(
      command.c_str(), command.size(), isReadOnly, timeout_, OUT_BUFFER_SIZE,
      m_outBuffer, &actualReplySize);
  timing_bft_.End();

  log_timing();

  if (status.isOK() && actualReplySize) {
    return resp.ParseFromArray(m_outBuffer, actualReplySize);
  } else {
    LOG4CPLUS_ERROR(logger_, "Error invoking "
                                 << (isReadOnly ? "read-only" : "read-write")
                                 << " command. Status: " << status
                                 << " Reply size: " << actualReplySize);
    ErrorResponse *err = resp.add_error_response();
    err->set_description("Internal concord Error");
    return true;
  }
}

void KVBClient::log_timing() {
  if (timing_enabled_ &&
      steady_clock::now() - timing_log_last_ > timing_log_period_) {
    LOG_INFO(logger_, metrics_.ToJson());
    timing_log_last_ = steady_clock::now();

    timing_bft_.Reset();
  }
}

KVBClientPool::KVBClientPool(std::vector<KVBClient *> &clients,
                             shared_ptr<TimePusher> time_pusher)
    : logger_(
          log4cplus::Logger::getInstance("com.vmware.concord.KVBClientPool")),
      time_pusher_(time_pusher),
      client_count_{clients.size()},
      clients_(),
      clients_mutex_(),
      clients_condition_(),
      wait_queue_(),
      shutdown_{false} {
  for (auto it = clients.begin(); it < clients.end(); it++) {
    clients_.push(*it);
  }
}

KVBClientPool::~KVBClientPool() {
  std::unique_lock<std::mutex> clients_lock(clients_mutex_);
  // stop new requests
  shutdown_ = true;

  while (client_count_ > 0) {
    // TODO: timeout
    clients_condition_.wait(clients_lock,
                            [this] { return !this->clients_.empty(); });

    LOG4CPLUS_DEBUG(logger_, "Stopping and deleting client");
    KVBClient *client = clients_.front();
    clients_.pop();
    delete client;
    client_count_--;
  }
  LOG4CPLUS_INFO(logger_, "Client cleanup complete");
}

bool KVBClientPool::send_request_sync(ConcordRequest &req, bool isReadOnly,
                                      ConcordResponse &resp) {
  KVBClient *client;
  {
    std::unique_lock<std::mutex> clients_lock(clients_mutex_);

    // Avoid starvation by forcing waiters to be unblocked in the order they
    // started waiting.
    std::thread::id my_thread_id = std::this_thread::get_id();
    wait_queue_.push(my_thread_id);

    // TODO: timeout
    clients_condition_.wait(clients_lock, [this, my_thread_id] {
      // Only continue if either the node is shutting down, or if it's this
      // thread's turn.
      return this->shutdown_ ||
             (my_thread_id == wait_queue_.front() && !clients_.empty());
    });

    if (shutdown_) {
      // TODO: To make things super clean, we should find and remove ourselves
      // from wait_queue_ as well, but if we're shutting down, we don't really
      // care about that tracking.
      ErrorResponse *err = resp.add_error_response();
      err->set_description("Node is shutting down.");
      return true;
    }

    wait_queue_.pop();

    client = clients_.front();
    clients_.pop();

    if (!clients_.empty()) {
      // We have to re-notify here, because it's possible that multiple notify
      // calls happened before the head waiter woke up. In that case, the next
      // waiter after this one may have woken, found that it was not next, and
      // gone back to waiting. If there's a client for it, it needs to be woken
      // again to grab it now.
      clients_condition_.notify_all();
    }
  }  // scope unlocks mutex

  bool result = client->send_request_sync(req, isReadOnly, resp);

  {
    std::unique_lock<std::mutex> clients_lock(clients_mutex_);
    clients_.push(client);

    // Wake all waiters, to be sure that the next one in the wait queue can grab
    // a client.
    clients_condition_.notify_all();
  }  // scope unlocks mutex

  return result;
}

void KVBClientPool::SetTimePusherPeriod(const Duration &period) {
  if (time_pusher_) {
    time_pusher_->SetPeriod(period);
  } else {
    LOG4CPLUS_WARN(logger_,
                   "Received request to reconfigure time pusher period to "
                   "client pool with no time pusher.");
  }
}

bool KVBClientPool::HasTimePusher() { return (bool)time_pusher_; }

}  // namespace consensus
}  // namespace concord
