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
  Status status = client_->invokeCommandSynch(
      command.c_str(), command.size(), isReadOnly, timeout_, OUT_BUFFER_SIZE,
      m_outBuffer, &actualReplySize);

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

KVBClientPool::KVBClientPool(std::vector<KVBClient *> &clients,
                             shared_ptr<TimePusher> time_pusher)
    : logger_(
          log4cplus::Logger::getInstance("com.vmware.concord.KVBClientPool")),
      clients_(clients.size()),
      time_pusher_(time_pusher) {
  for (auto it = clients.begin(); it < clients.end(); it++) {
    clients_.push(*it);
  }
}

KVBClientPool::~KVBClientPool() {
  while (true) {
    KVBClient *client;
    if (!clients_.pop(client)) {
      LOG4CPLUS_INFO(logger_, "Client cleanup complete");
      break;
    }

    LOG4CPLUS_DEBUG(logger_, "Stopping and deleting client");
    delete client;
  }
}

bool KVBClientPool::send_request_sync(ConcordRequest &req, bool isReadOnly,
                                      ConcordResponse &resp) {
  while (true) {
    KVBClient *client;
    if (!clients_.pop(client)) {
      boost::this_thread::yield();
      continue;
    }

    bool result = client->send_request_sync(req, isReadOnly, resp);
    clients_.push(client);
    return result;
  }
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
