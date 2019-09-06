// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "connection_manager.hpp"
#include <log4cplus/loggingmacros.h>

namespace concord {
namespace api {

/* statoc logger per class */
static log4cplus::Logger logger_(
    log4cplus::Logger::getInstance("com.vmware.concord.ConnectionManager"));

void ConnectionManager::start_connection(ApiConnection::pointer pConn) {
  LOG4CPLUS_TRACE(logger_, "start_connection enter");

  boost::unique_lock<boost::mutex> lock(mutex_);
  connections_.insert(pConn);
  lock.unlock();

  pConn->start_async();
  LOG4CPLUS_INFO(logger_, "new connection added, live connections: "
                              << connections_.size());
  LOG4CPLUS_TRACE(logger_, "start_connection exit");
}

void ConnectionManager::close_connection(ApiConnection::pointer pConn) {
  LOG4CPLUS_TRACE(logger_, "close_connection enter");

  boost::unique_lock<boost::mutex> lock(mutex_);
  connections_.erase(pConn);
  lock.unlock();

  LOG4CPLUS_INFO(logger_, "connection closed and removed, live connections: "
                              << connections_.size());
  LOG4CPLUS_TRACE(logger_, "close_connection exit");
}

}  // namespace api
}  // namespace concord
