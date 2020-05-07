// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Read peers from Concord directly.

#include <inttypes.h>
#include <boost/program_options.hpp>
#include <iostream>

#include "concmdconn.hpp"
#include "concmdopt.hpp"
#include "concord.pb.h"

using namespace com::vmware::concord;

int main(int argc, char **argv) {
  try {
    boost::program_options::variables_map opts;
    if (!parse_options(argc, argv, NULL, opts)) {
      return 0;
    }

    // Create request

    ConcordRequest concReq;
    PeerRequest *peerReq = concReq.mutable_peer_request();
    peerReq->set_return_peers(true);

    // Send & Receive

    ConcordResponse concResp;
    if (call_concord(opts, concReq, concResp)) {
      if (concResp.has_peer_response()) {
        PeerResponse peerResp = concResp.peer_response();
        if (peerResp.peer_size() <= 0) {
          std::cout << "No peers found" << std::endl;
        }
      } else {
        std::cerr << "Didn't get a PeerResponse" << std::endl;
        return -1;
      }
    } else {
      std::cerr << "Call to Concord failed" << std::endl;
      return -1;
    }
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return -1;
  }

  return 0;
}
