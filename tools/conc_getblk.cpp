// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Get a block or block list from concord.

#include <inttypes.h>
#include <boost/program_options.hpp>
#include <iostream>

#include "concmdconn.hpp"
#include "concmdex.hpp"
#include "concmdfmt.hpp"
#include "concmdopt.hpp"
#include "concord.pb.h"

using namespace boost::program_options;
using namespace com::vmware::concord;

#define OPT_LIST "list"
#define OPT_NUMBER "number"
#define OPT_COUNT "count"
#define OPT_HASH "hash"

void add_options(options_description &desc) {
  // clang-format off
  desc.add_options()
    (OPT_LIST ",l", "List blocks from index to index-count")
    (OPT_NUMBER ",n", value<std::uint64_t>(),
     "Number of block to get (latest if listing)")
    (OPT_COUNT ",c", value<std::uint64_t>(), "Number of blocks to list")
    (OPT_HASH ",s", value<std::string>(), "Hash of block to get");
  // clang-format on
}

void prepare_block_list_request(variables_map &opts, ConcordRequest &concReq) {
  BlockListRequest *blkReq = concReq.mutable_block_list_request();

  if (opts.count(OPT_NUMBER)) {
    blkReq->set_latest(opts[OPT_NUMBER].as<std::uint64_t>());
  }
  if (opts.count(OPT_COUNT)) {
    blkReq->set_count(opts[OPT_COUNT].as<std::uint64_t>());
  }
}

void prepare_block_request(variables_map &opts, ConcordRequest &concReq) {
  BlockRequest *blkReq = concReq.mutable_block_request();

  if (opts.count(OPT_NUMBER)) {
    blkReq->set_number(opts[OPT_NUMBER].as<std::uint64_t>());
  } else {
    std::string blkhash;
    dehex0x(opts[OPT_HASH].as<std::string>(), blkhash);
    blkReq->set_hash(blkhash);
  }
}

void handle_block_list_response(ConcordResponse &concResp) {
  if (!concResp.has_block_list_response()) {
    std::cerr << "No block list response found." << std::endl;
    if (concResp.error_response_size() == 1) {
      std::cerr << "Error response: '"
                << concResp.error_response(0).description() << std::endl;
    }
  }

  BlockListResponse blkResp = concResp.block_list_response();

  std::cout << "Blocks: (" << blkResp.block_size() << ")" << std::endl;

  for (int i = 0; i < blkResp.block_size(); i++) {
    BlockBrief bb = blkResp.block(i);
    std::string hash;
    hex0x(bb.hash(), hash);
    std::cout << bb.number() << " == " << hash << std::endl;
  }
}

void handle_block_response(ConcordResponse &concResp) {
  if (!concResp.has_block_response()) {
    std::cerr << "No block response found." << std::endl;
    if (concResp.error_response_size() == 1) {
      std::cerr << "Error response: '"
                << concResp.error_response(0).description() << std::endl;
    }
  }

  BlockResponse blkResp = concResp.block_response();

  std::string hash, parent;
  hex0x(blkResp.hash(), hash);
  hex0x(blkResp.parent_hash(), parent);
  std::cout << "Number: " << blkResp.number() << std::endl
            << "  Hash: " << hash << std::endl
            << "Parent: " << parent << std::endl
            << "Transactions:" << std::endl;

  for (int i = 0; i < blkResp.transaction_size(); i++) {
    std::string tx;
    hex0x(blkResp.transaction(i).hash(), tx);
    std::cout << "   " << tx << std::endl;
  }
}

int main(int argc, char **argv) {
  try {
    variables_map opts;
    if (!parse_options(argc, argv, &add_options, opts)) {
      return 0;
    }

    // Create request

    if (opts.count(OPT_LIST) == 0) {
      // list not requested
      if (opts.count(OPT_COUNT)) {
        std::cerr
            << "The count parameter is not valid if a list is not requested."
            << std::endl;
        return -1;
      }
      if (!opts.count(OPT_NUMBER) && !opts.count(OPT_HASH)) {
        std::cerr << "Please provide either a number or a hash." << std::endl;
        return -1;
      }
      if (opts.count(OPT_NUMBER) && opts.count(OPT_HASH)) {
        std::cerr << "Please provide only one of number or hash." << std::endl;
        return -1;
      }
    } else {
      // we are listing
      if (opts.count(OPT_HASH)) {
        std::cerr << "The hash parameter is not valid if a list is requested."
                  << std::endl;
        return -1;
      }
    }

    ConcordRequest concReq;
    if (opts.count(OPT_LIST)) {
      prepare_block_list_request(opts, concReq);
    } else {
      prepare_block_request(opts, concReq);
    }

    // Send & Receive

    ConcordResponse concResp;
    if (call_concord(opts, concReq, concResp)) {
      if (opts.count(OPT_LIST)) {
        handle_block_list_response(concResp);
      } else {
        handle_block_response(concResp);
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
