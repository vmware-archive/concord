// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Acceptor for connections from the API/UI servers.

#include "api_acceptor.hpp"

#include <boost/bind.hpp>
#include <boost/thread.hpp>

using boost::asio::io_service;
using boost::asio::ip::tcp;
using boost::system::error_code;

using concord::common::StatusAggregator;
using concord::consensus::KVBClientPool;
using namespace boost::asio;

namespace concord {
namespace api {

ApiAcceptor::ApiAcceptor(
    io_service &io_service, tcp::endpoint endpoint, KVBClientPool &clientPool,
    StatusAggregator &sag, uint64_t gasLimit, uint64_t chainID, bool ethEnabled,
    const concord::config::ConcordConfiguration &nodeConfig)
    : acceptor_(io_service, endpoint),
      clientPool_(clientPool),
      logger_(log4cplus::Logger::getInstance("com.vmware.concord.ApiAcceptor")),
      sag_(sag),
      gasLimit_(gasLimit),
      chainID_(chainID),
      ethEnabled_(ethEnabled),
      nodeConfig_(nodeConfig) {
  // set SO_REUSEADDR option on this socket so that if listener thread fails
  // we can still bind again to this socket
  acceptor_.set_option(ip::tcp::acceptor::reuse_address(true));
  start_accept();
}

void ApiAcceptor::start_accept() {
  LOG4CPLUS_TRACE(logger_, "start_accept enter");

  ApiConnection::pointer new_connection = ApiConnection::create(
      acceptor_.get_io_service(), connManager_, clientPool_, sag_, gasLimit_,
      chainID_, ethEnabled_, nodeConfig_);

  acceptor_.async_accept(
      new_connection->socket(),
      boost::bind(&ApiAcceptor::handle_accept, this, new_connection,
                  boost::asio::placeholders::error));
  LOG4CPLUS_TRACE(logger_, "start_accept exit");
}

void ApiAcceptor::handle_accept(ApiConnection::pointer new_connection,
                                const boost::system::error_code &error) {
  LOG4CPLUS_TRACE(logger_, "handle_accept enter, thread id: "
                               << boost::this_thread::get_id());
  if (!error) {
    connManager_.start_connection(new_connection);
  } else if (error == error::operation_aborted) {
    // Exit here in case operation aborted - shutdown is ongoing.
    LOG4CPLUS_ERROR(logger_, error.message());
    return;
  }

  LOG4CPLUS_DEBUG(logger_, "handle_accept before start_accept");
  start_accept();
  LOG4CPLUS_TRACE(logger_, "handle_accept exit");
}

}  // namespace api
}  // namespace concord
