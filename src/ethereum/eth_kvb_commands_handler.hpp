// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KVBlockchain replica command handler interface for EVM.

#ifndef ETHEREUM_KVB_COMMANDS_HANDLER_HPP_
#define ETHEREUM_KVB_COMMANDS_HANDLER_HPP_

#include <log4cplus/loggingmacros.h>
#include <boost/program_options.hpp>

#include "blockchain/db_interfaces.h"
#include "concord.pb.h"
#include "config/configuration_manager.hpp"
#include "consensus/concord_commands_handler.hpp"
#include "ethereum/concord_evm.hpp"
#include "time/time_contract.hpp"
#include "utils/concord_eth_sign.hpp"

namespace concord {
namespace ethereum {

class EthKvbCommandsHandler
    : public concord::consensus::ConcordCommandsHandler {
 private:
  log4cplus::Logger logger;
  concord::ethereum::EVM &concevm_;
  concord::utils::EthSign &verifier_;
  const concord::config::ConcordConfiguration &nodeConfiguration;
  const uint64_t gas_limit_;

 public:
  EthKvbCommandsHandler(
      concord::ethereum::EVM &concevm, concord::utils::EthSign &verifier,
      const concord::config::ConcordConfiguration &config,
      const concord::config::ConcordConfiguration &nodeConfig,
      const concord::storage::blockchain::ILocalKeyValueStorageReadOnly
          &storage,
      concord::storage::blockchain::IBlocksAppender &appender,
      std::shared_ptr<concord::utils::PrometheusRegistry> prometheus_registry);
  ~EthKvbCommandsHandler();

  // concord::consensus::ConcordStateMachine
  bool Execute(const com::vmware::concord::ConcordRequest &request,
               uint8_t flags, concord::time::TimeContract *time,
               opentracing::Span &parent_span,
               com::vmware::concord::ConcordResponse &response) override;
  void WriteEmptyBlock(concord::time::TimeContract *time) override;

 private:
  // Handlers
  bool handle_transaction_request(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_transaction_list_request(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_logs_request(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_block_list_request(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_block_request(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_eth_request(const com::vmware::concord::ConcordRequest &concreq,
                          EthKvbStorage &kvb_storage,
                          concord::time::TimeContract *time,
                          opentracing::Span &parent_span,
                          com::vmware::concord::ConcordResponse &concresp);
  bool handle_eth_sendTransaction(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage, concord::time::TimeContract *time,
      opentracing::Span &parent_span,
      com::vmware::concord::ConcordResponse &concresp);
  bool handle_eth_request_read_only(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage, concord::time::TimeContract *time,
      opentracing::Span &parent_span,
      com::vmware::concord::ConcordResponse &concresp);
  bool handle_eth_callContract(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage, concord::time::TimeContract *time,
      opentracing::Span &parent_span,
      com::vmware::concord::ConcordResponse &concresp);
  bool handle_eth_blockNumber(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_eth_getCode(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_eth_getStorageAt(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_eth_getTransactionCount(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;
  bool handle_eth_getBalance(
      const com::vmware::concord::ConcordRequest &concreq,
      EthKvbStorage &kvbStorage,
      com::vmware::concord::ConcordResponse &concresp) const;

  // Utilites
  void build_transaction_response(
      evmc_uint256be &hash, concord::common::EthTransaction &tx,
      com::vmware::concord::TransactionResponse *response) const;

  void recover_from(const com::vmware::concord::EthRequest &request,
                    evmc_address *sender) const;

  uint64_t parse_block_parameter(
      const com::vmware::concord::EthRequest &request,
      EthKvbStorage &kvbStorage) const;

  evmc_result run_evm(const com::vmware::concord::EthRequest &request,
                      EthKvbStorage &kvbStorage, uint64_t timestamp,
                      opentracing::Span &parent_span,
                      evmc_uint256be &txhash /* OUT */);

  evmc_uint256be record_transaction(
      const evmc_message &message,
      const com::vmware::concord::EthRequest &request, const uint64_t nonce,
      const evmc_result &result, const uint64_t timestamp,
      const std::vector<::concord::common::EthLog> &logs,
      EthKvbStorage &kvbStorage) const;

  void collect_logs_from_block(
      const concord::common::EthBlock &block, EthKvbStorage &kvbStorage,
      const com::vmware::concord::LogsRequest &request,
      com::vmware::concord::LogsResponse *response) const;
};

}  // namespace ethereum
}  // namespace concord

#endif  // ETHEREUM_KVB_COMMANDS_HANDLER_HPP_
