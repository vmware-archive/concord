// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Handler for connections from the API/UI servers.
//
// We have one handler per connection. It is expected that connections
// from will be long-lived. We will multiplex client requests
// onto these connections.
//
// This version also handles one request at a time. The next request
// is read from the socket, the request is handled, and a response is
// sent to the socket. TODO: we probably want to toss requests at a
// threadpool of handlers (see SEDA system design).
//
// Requests are sent as two bytes encoding the length as an unsigned
// little-endian integer, followed by that number of bytes encoding an
// concordRequest protocol buffers message (see
// concord.proto). Responses are encoded the same way, though as
// concordResponse messages. TODO: do we need to support requests or
// responses more than 64k in length?
//
// Most of what this class does is pass requests from the external socket to
// KVBlockchain/SBFT via KVBClient, and then pass the results back to the
// socket. Some verification is done where possible, to avoid send_requesting
// commands through SBFT if they would just generate errors on the replica side.

#include "api_connection.hpp"

#include <boost/predef/detail/endian_compat.h>
#include <google/protobuf/util/time_util.h>
#include <boost/bind.hpp>
#include <iostream>
#include <limits>

#include "api/connection_manager.hpp"
#include "common/concord_log.hpp"
#include "consensus/kvb_client.hpp"
#include "evmjit.h"

using namespace boost::asio;
using namespace std;

// Protobuf interface
using namespace com::vmware::concord;

using boost::asio::buffer;
using boost::asio::io_service;
using boost::asio::mutable_buffer;
using boost::asio::read;
using boost::asio::transfer_at_least;
using boost::asio::write;
using boost::asio::ip::tcp;
using boost::system::error_code;

using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;

using concord::common::StatusAggregator;
using concord::consensus::KVBClientPool;
using concord::utils::from_evmc_uint256be;

namespace concord {
namespace api {

ApiConnection::pointer ApiConnection::create(
    io_service &io_service, ConnectionManager &connManager,
    KVBClientPool &clientPool, StatusAggregator &sag, uint64_t gasLimit,
    uint64_t chainID, bool ethEnabled) {
  return pointer(new ApiConnection(io_service, connManager, clientPool, sag,
                                   gasLimit, chainID, ethEnabled));
}

tcp::socket &ApiConnection::socket() { return socket_; }

void ApiConnection::start_async() {
  LOG4CPLUS_TRACE(logger_, "start_async enter");
  remotePeer_ = socket_.remote_endpoint();
  LOG4CPLUS_INFO(logger_, "Connection to " << remotePeer_ << " opened by peer");

  read_async_header();

  LOG4CPLUS_TRACE(logger_, "start_async exit");
}

uint16_t ApiConnection::get_message_length(const char *buffer) {
  uint16_t msgLen =
      *(static_cast<const uint16_t *>(static_cast<const void *>(buffer)));
#ifndef BOOST_LITTLE_ENDIAN
  // swap byte order for big endian and pdp endian
  msgLen = (msglen << 8) | (msglen >> 8);
#endif
  return msgLen;
}

bool ApiConnection::check_async_error(const boost::system::error_code &ec) {
  bool res = false;
  if (boost::asio::error::eof == ec) {
    LOG4CPLUS_INFO(logger_, "connection closed by peer");
    res = true;
  } else if (boost::asio::error::operation_aborted == ec) {
    LOG4CPLUS_ERROR(logger_, ec.message());
    res = true;
  }
  return res;
}

void ApiConnection::on_read_async_header_completed(
    const boost::system::error_code &ec, const size_t bytesRead) {
  LOG4CPLUS_TRACE(logger_, "on_read_async_header_completed enter");
  auto err = check_async_error(ec);
  if (err) {
    LOG4CPLUS_DEBUG(logger_,
                    "on_read_async_header_completed, ec: " << ec.message());
    close();
    return;
  }

  auto msgLen = get_message_length(inMsgBuffer_);
  if (msgLen == 0) {
    LOG4CPLUS_FATAL(logger_, "on_read_async_header_completed, msgLen=0");
    return;
  }

  LOG4CPLUS_DEBUG(logger_,
                  "on_read_async_header_completed, msgLen: " << msgLen);

  read_async_message(MSG_LENGTH_BYTES, msgLen);

  LOG4CPLUS_TRACE(logger_, "on_read_async_header_completed exit");
}

void ApiConnection::read_async_header() {
  LOG4CPLUS_TRACE(logger_, "read_async_header enter");

  // clean all previous data
  memset(inMsgBuffer_, 0, BUFFER_LENGTH);
  concordRequest_.Clear();
  concordResponse_.Clear();

  // async operation will finish when either expectedBytes are read
  // or error occured
  async_read(socket_, boost::asio::buffer(inMsgBuffer_, MSG_LENGTH_BYTES),
             boost::bind(&ApiConnection::on_read_async_header_completed,
                         shared_from_this(), boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));

  LOG4CPLUS_TRACE(logger_, "read_async exit");
}

void ApiConnection::read_async_message(uint16_t offset,
                                       uint16_t expectedBytes) {
  LOG4CPLUS_TRACE(logger_, "read_async_message enter");
  LOG4CPLUS_DEBUG(logger_,
                  "offset: " << offset << ", expectedBytes: " << expectedBytes);

  // async operation will finish when either expectedBytes are read
  // or error occured
  async_read(socket_, boost::asio::buffer(inMsgBuffer_ + offset, expectedBytes),
             boost::bind(&ApiConnection::on_read_async_message_completed,
                         shared_from_this(), boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));

  LOG4CPLUS_TRACE(logger_, "read_async_message exit");
}

// this is the handler to async_read, it will be called only if the
// supplied data buffer for read is full OR error occured
void ApiConnection::on_read_async_message_completed(
    const boost::system::error_code &ec, const size_t bytesRead) {
  LOG4CPLUS_TRACE(logger_, "on_read_async_completed enter");

  auto err = check_async_error(ec);
  if (err) {
    LOG4CPLUS_DEBUG(logger_,
                    "on_read_async_message_completed, ec: " << ec.message());
    return;
  }

  LOG4CPLUS_DEBUG(logger_, "msg data read, msgLen: " << bytesRead);
  process_incoming();
  read_async_header();

  LOG4CPLUS_TRACE(logger_, "on_read_async_completed exit");
}

void ApiConnection::close() {
  // we should not close socket_ explicitly since we use shared_from_this,
  // so the current api_connetion object and its socket_ object should be
  // destroyed automatically. However, this should be profiled during
  // stress tests for memory leaks
  LOG4CPLUS_DEBUG(logger_, "closing connection");
  connManager_.close_connection(shared_from_this());
}

void ApiConnection::on_write_completed(const boost::system::error_code &ec) {
  if (!ec) {
    LOG4CPLUS_DEBUG(logger_, "sent completed");
  } else {
    LOG4CPLUS_ERROR(logger_, "sent failed with error: " << ec.message());
  }
}

/*
 * Start handling a connection. Read requests from the connection and
 * send back responses until the client disconnects.
 */
void ApiConnection::process_incoming() {
  std::string pb;
  LOG4CPLUS_TRACE(logger_, "process_incoming enter");

  // Parse the protobuf
  if (concordRequest_.ParseFromArray(inMsgBuffer_ + MSG_LENGTH_BYTES,
                                     get_message_length(inMsgBuffer_))) {
    LOG4CPLUS_DEBUG(logger_, "Parsed!");

    // handle the request
    dispatch();
  } else {
    // Parsing failed
    ErrorResponse *e = concordResponse_.add_error_response();
    e->mutable_description()->assign("Unable to parse request");
  }

  // marshal the protobuf
  concordResponse_.SerializeToString(&pb);
  uint16_t msgLen = pb.length();
#ifndef BOOST_LITTLE_ENDIAN
  msgLen = (msgLen << 8) || (msgLen >> 8);
#endif

  memset(outMsgBuffer_, 0, BUFFER_LENGTH);
  memcpy(outMsgBuffer_, &msgLen, MSG_LENGTH_BYTES);
  memcpy(outMsgBuffer_ + MSG_LENGTH_BYTES, pb.c_str(), msgLen);

  LOG4CPLUS_DEBUG(logger_, "sending back " << to_string(msgLen) << " bytes");
  boost::asio::async_write(
      socket_, boost::asio::buffer(outMsgBuffer_, msgLen + MSG_LENGTH_BYTES),
      boost::bind(&ApiConnection::on_write_completed, shared_from_this(),
                  boost::asio::placeholders::error));

  LOG4CPLUS_DEBUG(logger_, "responded!");
  LOG4CPLUS_TRACE(logger_, "process_incoming exit");
}

/*
 * Based on what requests are in the message, dispatch to the proper
 * handler.
 */
void ApiConnection::dispatch() {
  // HACK to enable management APIs only for non-Ethereum deployments
  if (!ethEnabled_) {
    if (concordRequest_.has_peer_request()) {
      handle_peer_request();
    }
    if (concordRequest_.has_reconfiguration_request()) {
      handle_reconfiguration_request();
    }
    return;
  }

  // The idea behind checking each request field every time, instead
  // of checking at most one, is that a client could batch
  // requests. We'll see if that's a thing that is reasonable.
  if (concordRequest_.has_protocol_request()) {
    handle_protocol_request();
  }
  if (concordRequest_.has_peer_request()) {
    handle_peer_request();
  }
  if (concordRequest_.has_reconfiguration_request()) {
    handle_reconfiguration_request();
  }
  for (int i = 0; i < concordRequest_.eth_request_size(); i++) {
    // Similarly, a list of ETH RPC requests is supported to allow
    // batching. This seems like a good idea, but may not fit this
    // mode exactly.
    handle_eth_request(i);
  }
  if (concordRequest_.has_block_list_request()) {
    handle_block_list_request();
  }
  if (concordRequest_.has_block_request()) {
    handle_block_request();
  }
  if (concordRequest_.has_transaction_request()) {
    handle_transaction_request();
  }
  if (concordRequest_.has_transaction_list_request()) {
    handle_transaction_list_request();
  }
  if (concordRequest_.has_logs_request()) {
    handle_logs_request();
  }
  if (concordRequest_.has_time_request()) {
    handle_time_request();
  }
  if (concordRequest_.has_test_request()) {
    handle_test_request();
  }
}

/*
 * Handle a protocol request, which is where the client announces its
 * version, and the server responds with its own. A request without a
 * client version might be considered a ping for keep-alive purposes.
 */
void ApiConnection::handle_protocol_request() {
  LOG4CPLUS_TRACE(logger_, "protocol_request enter");

  const ProtocolRequest request = concordRequest_.protocol_request();
  LOG4CPLUS_DEBUG(logger_, "protocol_request, client_version: "
                               << request.client_version());

  // create a response even if the request does not have a client
  // version, as this could be used as a keep-alive ping
  ProtocolResponse *response = concordResponse_.mutable_protocol_response();

  if (request.has_client_version()) {
    response->set_server_version(1);
    if (request.client_version() > 1) {
      // This is just a basic demonstration of how we may want to
      // protect against clients that have been upgraded before
      // servers.
      ErrorResponse *e = concordResponse_.add_error_response();
      e->mutable_description()->assign("Client version unknown");
    }
  }

  // We don't know what we'll do to support this yet. It is being added
  // because Truffle's testing framework requests it.
  // In geth, this is supplied via the --networkid command line parameter
  // and is required for nodes to communicate with each other.  The JSON
  // RPC API which retrieves it is net_version.
  //
  // Regarding chain vs network IDs, see:
  // https://github.com/ethereumproject/go-ethereum/wiki/FAQ
  response->set_net_version(DEFAULT_NETWORK_ID);
  LOG4CPLUS_TRACE(logger_, "protocol_request exit");
}

/*
 * Handle a peer request, which is the client asking for the list of
 * consensus participants, and maybe also asking to change that list
 * (add/remove members).
 */
void ApiConnection::handle_peer_request() {
  LOG4CPLUS_TRACE(logger_, "handle_peer_request");

  const PeerRequest request = concordRequest_.peer_request();
  PeerResponse *response = concordResponse_.mutable_peer_response();
  if (request.return_peers()) {
    auto peers = sag_.get_peers_info();
    for (auto peer : peers) {
      auto p = response->add_peer();
      p->set_address(peer.address);
      p->set_status(peer.state);
      p->set_millis_since_last_message(peer.millisSinceLastMessage);
      p->set_millis_since_last_message_threshold(
          peer.millisSinceLastMessageThreshold);
      p->set_hostname(peer.hostname);
    }
  }
}

/* Handle a reconfiguration request, which changes the value of configuration
 * parameter(s) on the Concod node that receves it. Note that support for this
 * reconfiguration is limited to certain parameter(s), due to the need for
 * parameter-specific logic to change the value of a parameter at runtime
 * (accounting for both where the parameter is loaded to and any special
 * precautions or procedures to safely change the parameter on a running
 * system).
 */
void ApiConnection::handle_reconfiguration_request() {
  LOG4CPLUS_TRACE(logger_, "handle_reconfiguration_request");

  const ReconfigurationRequest request =
      concordRequest_.reconfiguration_request();
  ReconfigurationResponse *response =
      concordResponse_.mutable_reconfiguration_response();
  response->set_successful(true);
  if (request.has_reconfigure_time_pusher_period()) {
    const ReconfigureTimePusherPeriodRequest time_pusher_period_request =
        request.reconfigure_time_pusher_period();
    if (time_pusher_period_request.has_time_pusher_period_ms()) {
      // Note we assume (whehter the client pool was constructed with a time
      // pusher) to be logically equivalent to (whether the time service is
      // enabled and this node is a time source).
      if (!clientPool_.HasTimePusher()) {
        LOG4CPLUS_WARN(
            logger_,
            "Received request to reconfigure the time pusher period, but the "
            "time service is not enabled or this node is not a time source.");
        response->set_successful(false);
        concordResponse_.add_error_response();
        concordResponse_
            .mutable_error_response(concordResponse_.error_response_size() - 1)
            ->set_description(
                "Cannot reconfigure time pusher period: the time service is "
                "not enabled or this node is not a time source.");
      } else {
        Duration new_period =
            time_pusher_period_request.time_pusher_period_ms();
        try {
          clientPool_.SetTimePusherPeriod(new_period);
          LOG4CPLUS_INFO(logger_,
                         "Reconfigured time pusher period to "
                             << TimeUtil::DurationToMilliseconds(new_period)
                             << " milliseconds.");
        } catch (const exception &e) {
          LOG4CPLUS_WARN(
              logger_,
              "Failed to service request to reconfigure time pusher period.");
          response->set_successful(false);
          concordResponse_.add_error_response();
          concordResponse_
              .mutable_error_response(concordResponse_.error_response_size() -
                                      1)
              ->set_description(
                  "An error occurred while trying toreconfigure "
                  "time_pusher_period_ms: " +
                  string(e.what()));
        }
      }
    }
  }
}

/*
 * Handle an ETH RPC request.
 */
void ApiConnection::handle_eth_request(int i) {
  const EthRequest request = concordRequest_.eth_request(i);
  uint64_t sigV, chainID;

  // Verify if request conforms with the requirements
  // for replay protection : EIP-155
  if (request.has_sig_v()) {
    sigV = request.sig_v();
    if (sigV == 27 || sigV == 28) {
      LOG4CPLUS_DEBUG(logger_, "Request uses conventional signature");
      // No support for conventioanl signature's without replay protection
      // The only way this can be done is if the chainID is 1
      if (chainID_ != 1) {
        ErrorResponse *e = concordResponse_.add_error_response();
        std::string errorDescription =
            "Request uses conventional signature \
               and chainID is not 1 (" +
            std::to_string(chainID_) + ")";
        e->mutable_description()->assign(errorDescription);
      }
    } else {
      if (sigV % 2 == 0) {
        chainID = (sigV - 36) / 2;
      } else {
        chainID = (sigV - 35) / 2;
      }
      if (chainID != chainID_) {
        ErrorResponse *e = concordResponse_.add_error_response();
        std::string errorDescription =
            "Request's chainID (" + std::to_string(chainID) +
            ") does not conform with VMware Blockchain ID (" +
            std::to_string(chainID_) + ")";
        e->mutable_description()->assign(errorDescription);
      }
    }
  }

  bool validRequest;
  bool isReadOnly = true;
  switch (request.method()) {
    case EthRequest_EthMethod_SEND_TX:
      validRequest = is_valid_eth_sendTransaction(request);
      isReadOnly = false;
      break;
    case EthRequest_EthMethod_CALL_CONTRACT:
      // TODO: complicated validation; let it through for now
      validRequest = true;
      break;
    case EthRequest_EthMethod_GET_STORAGE_AT:
      validRequest = is_valid_eth_getStorageAt(request);
      break;
    case EthRequest_EthMethod_GET_CODE:
      validRequest = is_valid_eth_getCode(request);
      break;
    case EthRequest_EthMethod_BLOCK_NUMBER:
      // no parameters to validate
      validRequest = true;
      break;
    case EthRequest_EthMethod_GET_TX_COUNT:
      validRequest = is_valid_eth_getTransactionCount(request);
      break;
    case EthRequest_EthMethod_GET_BALANCE:
      validRequest = is_valid_eth_getBalance(request);
      break;
    default:
      validRequest = false;
      ErrorResponse *e = concordResponse_.add_error_response();
      e->mutable_description()->assign("ETH Method Not Implemented");
  }

  // If the request is valid, submit it to SBFT. If the request was not
  // valid, the validation function should have added an error message to
  // concordResponse_.
  if (validRequest) {
    ConcordRequest internalRequest;
    EthRequest *internalEthRequest = internalRequest.add_eth_request();
    internalEthRequest->CopyFrom(request);

    // Transactions create blocks, which need timestamps, and gas.
    if (request.method() == EthRequest_EthMethod_SEND_TX) {
      // remove when FEATURE_time_service is defaulted to true
      time_t currentTime = std::time(nullptr);
      internalEthRequest->set_timestamp(currentTime);

      // Gas limit must be chosen on the client side, because all replicas must
      // store the same gas limit in the block. If replica configs differed,
      // different gas limits would be stored, and then reads would fail
      // because responses wouldn't match.
      internalEthRequest->set_gas_limit(gasLimit_);
    }

    ConcordResponse internalResponse;
    if (clientPool_.send_request_sync(internalRequest, isReadOnly,
                                      internalResponse)) {
      concordResponse_.MergeFrom(internalResponse);
    } else {
      LOG4CPLUS_ERROR(logger_, "Error parsing response");
      ErrorResponse *resp = concordResponse_.add_error_response();
      resp->set_description("Internal concord Error");
    }
  }
}

/**
 * Verify an eth.sendTransaction or eth.sendRawTransaction request is valid.
 * According to the RPC docs, the only thing we absolutely have to know about a
 * transaction is the address of the originating account ("from"). Technically
 * it says data is also not optional, but it's not needed for a simple value
 * transfer, so it's really only needed if there is no destination account
 * ("to").
 */
bool ApiConnection::is_valid_eth_sendTransaction(const EthRequest &request) {
  if (!request.has_addr_from() &&
      !(request.has_sig_v() && request.has_sig_r() && request.has_sig_s())) {
    // We need either "from" or a signature to learn the source of this
    // transaction. In this early validation, just make sure there is
    // something in the fields. We'll check the fields are valid in
    // concord_kvb. TODO: We may want to validate here as well, to avoid
    // consensus work on an obviously invalid transaction.
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("No \"from\" or signature provided");
    return false;
  }

  if (!request.has_addr_to() && !request.has_data()) {
    // If "to" is absent, this must be a contract creation, which requires
    // "data".
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Missing both \"to\" and \"data\"");
    return false;
  }

  // everything checked out
  return true;
}

/**
 * Handle a request for the block list.
 */
void ApiConnection::handle_block_list_request() {
  const BlockListRequest request = concordRequest_.block_list_request();

  ConcordRequest internalConcRequest;
  BlockListRequest *internalBlockRequest =
      internalConcRequest.mutable_block_list_request();
  internalBlockRequest->CopyFrom(request);
  ConcordResponse internalConcResponse;

  if (clientPool_.send_request_sync(internalConcRequest, true /* read only */,
                                    internalConcResponse)) {
    concordResponse_.MergeFrom(internalConcResponse);
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Internal concord Error");
  }
}

/**
 * Handle a request for a specific block.
 */
void ApiConnection::handle_block_request() {
  const BlockRequest request = concordRequest_.block_request();

  if (!(request.has_number() || request.has_hash())) {
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("invalid block request: no id or hash");
    return;
  }

  ConcordRequest internalRequest;
  BlockRequest *blkReq = internalRequest.mutable_block_request();
  blkReq->CopyFrom(request);

  ConcordResponse internalResponse;
  if (clientPool_.send_request_sync(internalRequest, true /* read only */,
                                    internalResponse)) {
    concordResponse_.MergeFrom(internalResponse);
  } else {
    LOG4CPLUS_ERROR(logger_, "Error parsing read-only response");
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("Internal concord Error");
  }
}

/**
 * Handle a request for a specific transaction.
 */
void ApiConnection::handle_transaction_request() {
  const TransactionRequest request = concordRequest_.transaction_request();

  if (!request.has_hash()) {
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("invalid transaction request: no hash");
    return;
  }

  ConcordRequest internalRequest;
  TransactionRequest *txReq = internalRequest.mutable_transaction_request();
  txReq->CopyFrom(request);

  ConcordResponse internalResponse;
  if (clientPool_.send_request_sync(internalRequest, true /* read only */,
                                    internalResponse)) {
    concordResponse_.MergeFrom(internalResponse);
  } else {
    LOG4CPLUS_ERROR(logger_, "Error parsing read-only response");
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("Internal concord Error");
  }
}

void ApiConnection::handle_transaction_list_request() {
  const TransactionListRequest request =
      concordRequest_.transaction_list_request();

  ConcordRequest internalRequest;
  TransactionListRequest *txListReq =
      internalRequest.mutable_transaction_list_request();
  txListReq->CopyFrom(request);

  ConcordResponse internalResponse;
  if (clientPool_.send_request_sync(internalRequest, true, internalResponse)) {
    concordResponse_.MergeFrom(internalResponse);
  } else {
    LOG4CPLUS_ERROR(logger_, "Error parsing read-only response");
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("Internal concord Error");
  }
}

void ApiConnection::handle_logs_request() {
  const LogsRequest request = concordRequest_.logs_request();

  ConcordRequest internalRequest;
  LogsRequest *logsReq = internalRequest.mutable_logs_request();
  logsReq->CopyFrom(request);

  ConcordResponse internalResponse;
  if (clientPool_.send_request_sync(internalRequest, true, internalResponse)) {
    concordResponse_.MergeFrom(internalResponse);
  } else {
    LOG4CPLUS_ERROR(logger_, "Error parsing read-only response");
    ErrorResponse *resp = concordResponse_.add_error_response();
    resp->set_description("Internal concord Error");
  }
}

/**
 * Handle a request for time.
 */
void ApiConnection::handle_time_request() {
  const TimeRequest request = concordRequest_.time_request();

  ConcordRequest internalConcRequest;
  TimeRequest *internalTimeRequest = internalConcRequest.mutable_time_request();
  internalTimeRequest->CopyFrom(request);
  ConcordResponse internalConcResponse;

  // If a sample was provided, this is a request to update the contract. If no
  // sample was provided, this is just a request to read the latest state.
  bool readOnly = !request.has_sample();

  if (clientPool_.send_request_sync(internalConcRequest, readOnly,
                                    internalConcResponse)) {
    concordResponse_.MergeFrom(internalConcResponse);
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Internal concord Error");
  }
}

/**
 * Check that an eth_getStorageAt request is valid.
 */
bool ApiConnection::is_valid_eth_getStorageAt(const EthRequest &request) {
  if (request.has_addr_to() &&
      request.addr_to().size() == sizeof(evmc_address) && request.has_data() &&
      request.data().size() == sizeof(evmc_uint256be)) {
    // must have the address of the contract, and the location to read
    return true;
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Missing account/contract or storage address");
    return false;
  }
}

/**
 * Check that an eth_getCode request is valid.
 */
bool ApiConnection::is_valid_eth_getCode(const EthRequest &request) {
  if (request.has_addr_to() &&
      request.addr_to().size() == sizeof(evmc_address)) {
    return true;
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Missing contract address");
    return false;
  }
}

/**
 * Check that an eth_getTransactionCount request is valid.
 */
bool ApiConnection::is_valid_eth_getTransactionCount(
    const EthRequest &request) {
  if (request.has_addr_to() &&
      request.addr_to().size() == sizeof(evmc_address)) {
    return true;
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Missing account address");
    return false;
  }
}

/**
 * Check that an eth_getBalance request is valid.
 */
bool ApiConnection::is_valid_eth_getBalance(const EthRequest &request) {
  if (request.has_addr_to() &&
      request.addr_to().size() == sizeof(evmc_address)) {
    return true;
  } else {
    ErrorResponse *error = concordResponse_.add_error_response();
    error->set_description("Missing account address");
    return false;
  }
}

/*
 * Handle test request, where the client requests an echo. This is
 * likely something we won't include in the final release, but has
 * been useful for testing.
 */
void ApiConnection::handle_test_request() {
  const TestRequest request = concordRequest_.test_request();
  if (request.has_echo()) {
    TestResponse *response = concordResponse_.mutable_test_response();
    std::string *echo = response->mutable_echo();
    echo->assign(request.echo());
  }
}

uint64_t ApiConnection::current_block_number() {
  ConcordRequest internalReq;
  EthRequest *ethReq = internalReq.add_eth_request();
  ethReq->set_method(EthRequest_EthMethod_BLOCK_NUMBER);
  ConcordResponse internalResp;

  if (clientPool_.send_request_sync(internalReq, true /* read only */,
                                    internalResp)) {
    if (internalResp.eth_response_size() > 0) {
      std::string strblk = internalResp.eth_response(0).data();
      evmc_uint256be rawNumber;
      std::copy(strblk.begin(), strblk.end(), rawNumber.bytes);
      return from_evmc_uint256be(&rawNumber);
    }
  }

  return 0;
}

ApiConnection::ApiConnection(io_service &io_service, ConnectionManager &manager,
                             KVBClientPool &clientPool, StatusAggregator &sag,
                             uint64_t gasLimit, uint64_t chainID,
                             bool ethEnabled)
    : socket_(io_service),
      logger_(
          log4cplus::Logger::getInstance("com.vmware.concord.ApiConnection")),
      connManager_(manager),
      clientPool_(clientPool),
      sag_(sag),
      gasLimit_(gasLimit),
      chainID_(chainID),
      ethEnabled_(ethEnabled) {
  // nothing to do here yet other than initialize the socket and logger
}

}  // namespace api
}  // namespace concord
