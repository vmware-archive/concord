// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Acceptor for connections from the API/UI servers.

#ifndef API_API_ACCEPTOR_HPP
#define API_API_ACCEPTOR_HPP

#include <log4cplus/loggingmacros.h>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include "api/api_connection.hpp"
#include "api/connection_manager.hpp"
#include "common/status_aggregator.hpp"
#include "consensus/kvb_client.hpp"

namespace concord {
namespace api {

class ApiAcceptor {
 public:
  ApiAcceptor(boost::asio::io_service &io_service,
              boost::asio::ip::tcp::endpoint endpoint,
              concord::consensus::KVBClientPool &clientPool,
              concord::common::StatusAggregator &sag, uint64_t gasLimit,
              uint64_t chainID, bool ethEnabled);

 private:
  boost::asio::ip::tcp::acceptor acceptor_;
  concord::consensus::KVBClientPool &clientPool_;
  log4cplus::Logger logger_;
  ConnectionManager connManager_;
  concord::common::StatusAggregator sag_;
  uint64_t gasLimit_;
  uint64_t chainID_;
  bool ethEnabled_;

  void start_accept();

  void handle_accept(ApiConnection::pointer new_connection,
                     const boost::system::error_code &error);
};

}  // namespace api
}  // namespace concord

#endif  // API_API_ACCEPTOR_HPP
