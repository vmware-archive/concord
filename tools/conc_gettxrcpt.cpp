// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Get a transaction receipt from concord directly.

#include <inttypes.h>
#include <boost/program_options.hpp>
#include <iostream>

#include "concmdconn.hpp"
#include "concmdex.hpp"
#include "concmdfmt.hpp"
#include "concmdopt.hpp"
#include "concord.pb.h"

#include "evm.h"

using namespace boost::program_options;
using namespace com::vmware::concord;

#define OPT_LIST "list"
#define OPT_COUNT "count"
#define OPT_RECEIPT "receipt"

void add_options(options_description &desc) {
  // clang-format off
  desc.add_options()
    (OPT_LIST ",l", "List transactions from receipt to receipt-count")
    (OPT_COUNT ",c", value<std::uint64_t>(), "Number of transactionss to list")
    (OPT_RECEIPT ",r", value<std::string>(),
     "The transaction hash returned from eth_sendTransaction");
  // clang-format on
}

std::string status_to_string(int32_t status) {
  switch (status) {
    case EVM_SUCCESS:
      return "(success)";
    case EVM_FAILURE:
      return "(failure)";
    case EVM_OUT_OF_GAS:
      return "(out of gas)";
    case EVM_UNDEFINED_INSTRUCTION:
      return "(undefined instruction)";
    case EVM_BAD_JUMP_DESTINATION:
      return "(bad jump destination)";
    case EVM_STACK_OVERFLOW:
      return "(stack overflow)";
    case EVM_REVERT:
      return "(revert)";
    case EVM_STATIC_MODE_ERROR:
      return "(static mode error)";
    case EVM_INVALID_INSTRUCTION:
      return "(invalid instruction)";
    case EVM_INVALID_MEMORY_ACCESS:
      return "(invalid memory access)";
    case EVM_REJECTED:
      return "(rejected)";
    case EVM_INTERNAL_ERROR:
      return "(internal error)";
    default:
      return "(error: unknown status value)";
  }
}

void prepare_transaction_list_request(variables_map &opts,
                                      ConcordRequest &concReq) {
  TransactionListRequest *tReq = concReq.mutable_transaction_list_request();

  if (opts.count(OPT_RECEIPT)) {
    std::string rcpthash;
    dehex0x(opts[OPT_RECEIPT].as<std::string>(), rcpthash);
    tReq->set_latest(rcpthash);
  }

  if (opts.count(OPT_COUNT)) {
    tReq->set_count(opts[OPT_COUNT].as<std::uint64_t>());
  }
}

void prepare_transaction_request(variables_map &opts, ConcordRequest &concReq) {
  TransactionRequest *tReq = concReq.mutable_transaction_request();
  std::string rcpthash;
  dehex0x(opts[OPT_RECEIPT].as<std::string>(), rcpthash);
  tReq->set_hash(rcpthash);
}

void print_transaction(TransactionResponse &tResp) {
  if (tResp.has_status()) {
    uint32_t status = tResp.status();
    std::cout << "Transaction status: " << status << " "
              << status_to_string(status) << std::endl;
  } else {
    std::cerr << "EthResponse has no status" << std::endl;
  }

  if (tResp.has_contract_address()) {
    std::string result;
    hex0x(tResp.contract_address(), result);
    std::cout << "Contract address: " << result << std::endl;
  }
}

void handle_transaction_list_response(ConcordResponse &concResp) {
  if (concResp.has_transaction_list_response()) {
    TransactionListResponse tlResp = concResp.transaction_list_response();

    if (tlResp.has_next()) {
      std::string next;
      hex0x(tlResp.next(), next);
      std::cout << "Next transaction: " << next << std::endl;
    }

    for (int i = 0; i < tlResp.transaction_size(); i++) {
      TransactionResponse tResp = tlResp.transaction(i);
      std::string hash;
      hex0x(tResp.hash(), hash);
      std::cout << "Transaction " << i << ": " << hash << std::endl;
      print_transaction(tResp);
    }
  } else {
    std::cerr << "transaction list response not found" << std::endl;
  }
}

void handle_transaction_response(ConcordResponse &concResp) {
  if (concResp.has_transaction_response()) {
    TransactionResponse tResp = concResp.transaction_response();
    print_transaction(tResp);
  } else {
    std::cerr << "transaction response not found" << std::endl;
  }
}

int main(int argc, char **argv) {
  try {
    variables_map opts;
    if (!parse_options(argc, argv, &add_options, opts)) {
      return 0;
    }

    // Create request

    ConcordRequest concReq;
    if (opts.count(OPT_LIST) == 0) {
      // list not requested
      if (opts.count(OPT_COUNT)) {
        std::cerr
            << "The count parameter is not valid if a list is not request."
            << std::endl;
        return -1;
      }
      if (opts.count(OPT_RECEIPT) == 0) {
        std::cerr << "Please provide a transaction receipt." << std::endl;
        return -1;
      }
      prepare_transaction_request(opts, concReq);
    } else {
      prepare_transaction_list_request(opts, concReq);
    }

    // Send & Receive

    ConcordResponse concResp;
    if (call_concord(opts, concReq, concResp)) {
      if (opts.count(OPT_LIST)) {
        handle_transaction_list_response(concResp);
      } else {
        handle_transaction_response(concResp);
      }
    } else {
      return -1;
    }
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return -1;
  }

  return 0;
}
