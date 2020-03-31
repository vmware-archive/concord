// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Handler for connections from the API/UI servers.

#ifndef API_API_CONNECTION_HPP
#define API_API_CONNECTION_HPP

#include <log4cplus/loggingmacros.h>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>

#include "common/status_aggregator.hpp"
#include "concord.pb.h"
#include "config/configuration_manager.hpp"
#include "consensus/kvb_client.hpp"
#include "pruning/rsa_pruning_signer.hpp"

namespace concord {
namespace api {

class ConnectionManager;

class ApiConnection : public boost::enable_shared_from_this<ApiConnection> {
 public:
  // Arbitrary big number at this time.
  // Reference for known IDs as of Jan 2018:
  // https://github.com/ethereumbook/ethereumbook/issues/110
  const uint DEFAULT_NETWORK_ID = 5000;

  typedef boost::shared_ptr<ApiConnection> pointer;

  static pointer create(
      boost::asio::io_service &io_service, ConnectionManager &connManager,
      concord::consensus::KVBClientPool &clientPool,
      concord::common::StatusAggregator &sag, uint64_t gasLimit,
      uint64_t chainID, bool ethEnabled,
      const concord::config::ConcordConfiguration &nodeConfig);

  boost::asio::ip::tcp::socket &socket();

  void start_async();

  void close();

 private:
  void dispatch();

  /* Handlers for each type of request in the protobuf definition. */
  void handle_protocol_request();

  void handle_peer_request();

  void handle_reconfiguration_request();

  void handle_eth_request(int i);

  void handle_block_list_request();

  void handle_block_request();

  void handle_transaction_request();

  void handle_transaction_list_request();

  void handle_logs_request();

  void handle_time_request();

  void handle_latest_prunable_block_request();

  void handle_prune_request();

  void handle_test_request();

  bool send_request(com::vmware::concord::ConcordRequest &req, bool isReadOnly,
                    com::vmware::concord::ConcordResponse &resp);

  /* Specific Ethereum Method handlers. */
  bool is_valid_eth_getStorageAt(
      const com::vmware::concord::EthRequest &request);
  bool is_valid_eth_getCode(const com::vmware::concord::EthRequest &request);
  bool is_valid_eth_sendTransaction(
      const com::vmware::concord::EthRequest &request);
  bool is_valid_eth_getTransactionCount(
      const com::vmware::concord::EthRequest &request);
  bool is_valid_eth_getBalance(const com::vmware::concord::EthRequest &request);

  /* This serves eth_blockNumber. */
  uint64_t current_block_number();

  /* Constructor. */
  ApiConnection(boost::asio::io_service &io_service,
                ConnectionManager &connManager,
                concord::consensus::KVBClientPool &clientPool,
                concord::common::StatusAggregator &sag, uint64_t gasLimit,
                uint64_t chainID, bool ethEnabled,
                const concord::config::ConcordConfiguration &nodeConfig);

  uint16_t get_message_length(const char *buffer);

  bool check_async_error(const boost::system::error_code &ec);

  void read_async_header();

  void on_read_async_header_completed(const boost::system::error_code &ec,
                                      const size_t bytesRead);

  void read_async_message(uint16_t offset, uint16_t expectedBytes);

  void on_read_async_message_completed(const boost::system::error_code &ec,
                                       const size_t bytes);

  void on_write_completed(const boost::system::error_code &ec);

  void process_incoming();

  /* Socket being handled. */
  boost::asio::ip::tcp::socket socket_;

  /*
   * Most recent request read. Currently only one request is read at a time,
   * so this is also the request currently being processed.
   */
  com::vmware::concord::ConcordRequest concordRequest_;

  /*
   * Response being built. See above: only one request is read at a time, so
   * only one response is built at a time.
   */
  com::vmware::concord::ConcordResponse concordResponse_;

  /* The active tracing span. */
  std::unique_ptr<opentracing::Span> span_;

  /* Logger. */
  log4cplus::Logger logger_;

  ConnectionManager &connManager_;

  concord::consensus::KVBClientPool &clientPool_;

  boost::asio::ip::tcp::endpoint remotePeer_;

  /* need to be adjusted to real msg max size */
  static constexpr uint32_t BUFFER_LENGTH = 65536;

  /* buffer for incoming messages */
  char inMsgBuffer_[BUFFER_LENGTH];

  /* buffer for incoming messages */
  char outMsgBuffer_[BUFFER_LENGTH];

  const uint8_t MSG_LENGTH_BYTES = 2;

  concord::common::StatusAggregator sag_;
  const uint64_t gasLimit_;
  const uint64_t chainID_;
  const bool ethEnabled_;

  /* This signer is used to sign unsigned PruneRequest messages received over
   * the API so that the pruning state machine can process them. The signer uses
   * the private key of the replica that is running in the same node.
   * Additionally, a client_proxy principal_id from the same node is set as a
   * sender. */
  concord::pruning::RSAPruningSigner pruningSigner_;
  uint64_t pruneRequestSenderId_{0};
};

}  // namespace api
}  // namespace concord

#endif  // API_API_CONNECTION_HPP
