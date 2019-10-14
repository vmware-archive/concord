// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Shim between generic KVB and Concord-specific commands handlers.

#include "concord_commands_handler.hpp"
#include "hash_defs.h"
#include "time/time_contract.hpp"

#include <vector>

using com::vmware::concord::ErrorResponse;
using com::vmware::concord::TimeRequest;
using com::vmware::concord::TimeResponse;
using com::vmware::concord::TimeSample;
using concordUtils::Sliver;

using google::protobuf::Timestamp;
using std::chrono::steady_clock;

namespace concord {
namespace consensus {

ConcordCommandsHandler::ConcordCommandsHandler(
    const concord::config::ConcordConfiguration &config,
    const concord::storage::blockchain::ILocalKeyValueStorageReadOnly &storage,
    concord::storage::blockchain::IBlocksAppender &appender)
    : logger_(log4cplus::Logger::getInstance(
          "concord.consensus.ConcordCommandsHandler")),
      metadata_storage_(storage),
      storage_(storage),
      timing_enabled_(config.getValue<bool>("replica_timing_enabled")),
      metrics_{concordMetrics::Component(
          "concord_commands_handler",
          std::make_shared<concordMetrics::Aggregator>())},
      timing_parse_("parse", timing_enabled_, metrics_),
      timing_time_update_("time_update", timing_enabled_, metrics_),
      timing_time_response_("time_response", timing_enabled_, metrics_),
      timing_execute_("execute", timing_enabled_, metrics_),
      timing_serialize_("serialize", timing_enabled_, metrics_),
      appender_(appender) {
  if (concord::time::IsTimeServiceEnabled(config)) {
    time_ = std::unique_ptr<concord::time::TimeContract>(
        new concord::time::TimeContract(storage_, config));
  }
  if (timing_enabled_) {
    timing_log_period_ = std::chrono::seconds(
        config.getValue<uint32_t>("replica_timing_log_period_sec"));
    timing_log_last_ = steady_clock::now();
  }
}

int ConcordCommandsHandler::execute(uint16_t client_id, uint64_t sequence_num,
                                    bool read_only, uint32_t request_size,
                                    const char *request_buffer,
                                    uint32_t max_response_size,
                                    char *response_buffer,
                                    uint32_t &out_response_size) {
  executing_bft_sequence_num_ = sequence_num;

  request_.Clear();
  response_.Clear();

  timing_parse_.Start();
  bool result;
  if (request_.ParseFromArray(request_buffer, request_size)) {
    timing_parse_.End();
    if (time_ && request_.has_time_request() &&
        request_.time_request().has_sample()) {
      if (!read_only) {
        timing_time_update_.Start();
        TimeRequest tr = request_.time_request();
        TimeSample ts = tr.sample();
        if (!(time_->SignaturesEnabled()) && ts.has_source() && ts.has_time()) {
          time_->Update(ts.source(), client_id, ts.time());
        } else if (ts.has_source() && ts.has_time() && ts.has_signature()) {
          std::vector<uint8_t> signature(ts.signature().begin(),
                                         ts.signature().end());
          time_->Update(ts.source(), client_id, ts.time(), &signature);
        } else {
          LOG4CPLUS_WARN(
              logger_,
              "Time Sample is missing:"
                  << " [" << (ts.has_source() ? " " : "X") << "] source"
                  << " [" << (ts.has_time() ? " " : "X") << "] time"
                  << (time_->SignaturesEnabled()
                          ? (string(" [") + (ts.has_signature() ? " " : "X") +
                             "] signature")
                          : ""));
        }
        timing_time_update_.End();
      } else {
        LOG4CPLUS_INFO(logger_,
                       "Ignoring time sample sent in read-only command");
      }
    }

    timing_execute_.Start();
    result = Execute(request_, read_only, time_.get(), response_);
    timing_execute_.End();

    if (time_ && request_.has_time_request()) {
      TimeRequest tr = request_.time_request();

      if (time_->Changed()) {
        // We had a sample that updated the time contract, and the execution of
        // the rest of the command did not write its state. What should we do?
        if (result) {
          if (!read_only) {
            // The state machine might have had no commands in the request. Go
            // ahead and store just the time update.
            WriteEmptyBlock(time_.get());

            // Create an empty time response, so that out_response_size is not
            // zero.
            response_.mutable_time_response();
          } else {
            // If this happens, there is a bug above. Either the logic ignoring
            // the update in this function is broken, or the subclass's Execute
            // function modified timeContract_. Log an error for us to deal
            // with, but otherwise ignore.
            LOG4CPLUS_ERROR(
                logger_,
                "Time Contract was modified during read-only operation");

            ErrorResponse *err = response_.add_error_response();
            err->set_description(
                "Ignoring time update during read-only operation");

            // Also reset the time contract now, so that the modification is not
            // accidentally written during the next command.
            time_->Reset();
          }
        } else {
          LOG4CPLUS_WARN(logger_,
                         "Ignoring time update because Execute failed.");

          ErrorResponse *err = response_.add_error_response();
          err->set_description(
              "Ignoring time update because state machine execution failed");
        }
      }

      timing_time_response_.Start();
      if (tr.return_summary()) {
        TimeResponse *tp = response_.mutable_time_response();
        Timestamp *sum = new Timestamp(time_->GetTime());
        tp->set_allocated_summary(sum);
      }

      if (tr.return_samples()) {
        TimeResponse *tp = response_.mutable_time_response();

        for (auto &s : time_->GetSamples()) {
          TimeSample *ts = tp->add_sample();
          ts->set_source(s.first);
          Timestamp *t = new Timestamp(s.second.time);
          ts->set_allocated_time(t);
          if (s.second.signature) {
            ts->set_signature(s.second.signature->data(),
                              s.second.signature->size());
          }
        }
      }
      timing_time_response_.End();
    } else if (!time_ && request_.has_time_request()) {
      ErrorResponse *err = response_.add_error_response();
      err->set_description("Time service is disabled.");
    }
  } else {
    timing_parse_.End();
    ErrorResponse *err = response_.add_error_response();
    err->set_description("Unable to parse concord request");

    // "true" means "resending this request is unlikely to change the outcome"
    result = true;
  }

  timing_serialize_.Start();
  if (response_.ByteSizeLong() == 0) {
    LOG4CPLUS_ERROR(logger_, "Request produced empty response.");
    ErrorResponse *err = response_.add_error_response();
    err->set_description("Request produced empty response.");
  }

  if (response_.SerializeToArray(response_buffer, max_response_size)) {
    out_response_size = response_.GetCachedSize();
  } else {
    size_t response_size = response_.ByteSizeLong();

    LOG4CPLUS_ERROR(
        logger_,
        "Cannot send response to a client request: Response is too large "
        "(size of this response: " +
            std::to_string(response_size) +
            ", maximum size allowed for this response: " +
            std::to_string(max_response_size) + ").");

    response_.Clear();
    ErrorResponse *err = response_.add_error_response();
    err->set_description(
        "Concord could not send response: Response is too large (size of this "
        "response: " +
        std::to_string(response_size) +
        ", maximum size allowed for this response: " +
        std::to_string(max_response_size) + ").");

    if (response_.SerializeToArray(response_buffer, max_response_size)) {
      out_response_size = response_.GetCachedSize();
    } else {
      // This case should never occur; we intend to enforce a minimum buffer
      // size for the communication buffer size that Concord-BFT is configured
      // with, and this minimum should be significantly higher than the size of
      // this error messsage.
      LOG4CPLUS_FATAL(
          logger_,
          "Cannot send error response indicating response is too large: The "
          "error response itself is too large (error response size: " +
              std::to_string(response_.ByteSizeLong()) +
              ", maximum size allowed for this response: " +
              std::to_string(max_response_size) + ").");

      // This will cause the replica to halt.
      out_response_size = 0;
    }
  }
  timing_serialize_.End();

  log_timing();

  return result ? 0 : 1;
}

concordUtils::Status ConcordCommandsHandler::addBlock(
    const concord::storage::SetOfKeyValuePairs &updates,
    concord::storage::blockchain::BlockId &out_block_id) {
  // The IBlocksAppender interface specifies that updates must be const, but we
  // need to add items here, so we have to make a copy and work with that. In
  // the future, maybe we can figure out how to either make updates non-const,
  // or allow addBlock to take a list of const sets.
  concord::storage::SetOfKeyValuePairs amended_updates(updates);

  if (time_ && time_->Changed()) {
    pair<Sliver, Sliver> tc_state = time_->Serialize();
    amended_updates[tc_state.first] = tc_state.second;
  }

  amended_updates[metadata_storage_.BlockMetadataKey()] =
      metadata_storage_.SerializeBlockMetadata(executing_bft_sequence_num_);

  return appender_.addBlock(amended_updates, out_block_id);
}

void ConcordCommandsHandler::log_timing() {
  if (timing_enabled_ &&
      steady_clock::now() - timing_log_last_ > timing_log_period_) {
    LOG_INFO(logger_, metrics_.ToJson());
    timing_log_last_ = steady_clock::now();

    timing_parse_.Reset();
    timing_time_update_.Reset();
    timing_execute_.Reset();
    timing_serialize_.Reset();
    // TODO: reset execution count?
    ClearStats();
  }
}

}  // namespace consensus
}  // namespace concord
