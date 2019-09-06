// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Send a transaction to concord directly.

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

#define OPT_FROM "from"
#define OPT_TO "to"
#define OPT_VALUE "value"
#define OPT_DATA "data"
#define OPT_SIG_V "sigv"
#define OPT_SIG_R "sigr"
#define OPT_SIG_S "sigs"

void add_options(options_description &desc) {
  // clang-format off
  desc.add_options()
    (OPT_FROM ",f", value<std::string>(), "Address to send the TX from")
    (OPT_TO ",t", value<std::string>(), "Address to send the TX to")
    (OPT_VALUE ",v", value<std::string>(), "Amount to pass as value")
    (OPT_DATA ",d", value<std::string>(), "Hex-encoded string to pass as data")
    (OPT_SIG_V, value<std::string>(),"Signature V")
    (OPT_SIG_R, value<std::string>(), "Signature R")
    (OPT_SIG_S, value<std::string>(), "Signature S");
  // clang-format on
}

int main(int argc, char **argv) {
  try {
    variables_map opts;
    if (!parse_options(argc, argv, &add_options, opts)) {
      return 0;
    }

    // Create request

    ConcordRequest concReq;
    EthRequest *ethReq = concReq.add_eth_request();
    std::string from;
    std::string to;
    std::string data;
    std::string value;
    int sig_v;
    std::string sig_r;
    std::string sig_s;

    if (opts.count(OPT_FROM) > 0) {
      dehex0x(opts[OPT_FROM].as<std::string>(), from);
      ethReq->set_addr_from(from);
    }
    if (opts.count(OPT_TO) > 0) {
      dehex0x(opts[OPT_TO].as<std::string>(), to);
      ethReq->set_addr_to(to);
    }
    if (opts.count(OPT_VALUE) > 0) {
      dehex0x(opts[OPT_VALUE].as<std::string>(), value);
      ethReq->set_value(value);
    }
    if (opts.count(OPT_DATA) > 0) {
      dehex0x(opts[OPT_DATA].as<std::string>(), data);
      ethReq->set_data(data);
    }
    if (opts.count(OPT_SIG_V) > 0) {
      std::string sig_v_s;
      dehex0x(opts[OPT_SIG_V].as<std::string>(), sig_v_s);
      sig_v = 0;
      for (size_t i = 0; i < sig_v_s.size(); i++) {
        sig_v = (sig_v << 8) + sig_v_s[i];
      }
      ethReq->set_sig_v(sig_v);
    }
    if (opts.count(OPT_SIG_R) > 0) {
      dehex0x(opts[OPT_SIG_R].as<std::string>(), sig_r);
      ethReq->set_sig_r(sig_r);
    }
    if (opts.count(OPT_SIG_S) > 0) {
      dehex0x(opts[OPT_SIG_S].as<std::string>(), sig_s);
      ethReq->set_sig_s(sig_s);
    }

    // Send & Receive

    ConcordResponse concResp;
    if (call_concord(opts, concReq, concResp)) {
      if (concResp.eth_response_size() == 1) {
        EthResponse ethResp = concResp.eth_response(0);
        if (ethResp.has_data()) {
          std::string result;
          hex0x(ethResp.data(), result);
          std::cout << "Transaction Receipt: " << result << std::endl;
        } else {
          std::cerr << "EthResponse has no data" << std::endl;
          return -1;
        }
      } else if (concResp.error_response_size() == 1) {
        ErrorResponse errorResp = concResp.error_response(0);
        if (errorResp.has_description()) {
          std::cout << "Error Response: " << errorResp.description()
                    << std::endl;
          return -1;
        } else {
          std::cout << "Error response had no description" << std::endl;
          return -1;
        }
      } else {
        std::cerr << "Wrong number of eth_responses ("
                  << concResp.eth_response_size() << ") or errors ("
                  << concResp.error_response_size() << ")"
                  << " (expected 1)" << std::endl;
        return -1;
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
