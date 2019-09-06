// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef API_CONNECTION_MANAGER_HPP
#define API_CONNECTION_MANAGER_HPP

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <set>
#include "api_connection.hpp"

namespace concord {
namespace api {

class ConnectionManager {
 public:
  void start_connection(ApiConnection::pointer pConn);

  void close_connection(ApiConnection::pointer pConn);

 private:
  /* Socket being handled. */
  std::set<ApiConnection::pointer> connections_;
  /* Mutex used to protect updates to connections_ set */
  boost::mutex mutex_;
};

}  // namespace api
}  // namespace concord

#endif  // API_CONNECTION_MANAGER_HPP
