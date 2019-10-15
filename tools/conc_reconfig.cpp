// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Reconfigure parameter(s) of a running Concord node (on the same machine/host
// as you are running this conc_reconfig tool). Note support for runtime
// reconfiguration is limited to certain configuration parameters. Which
// parameters reconfiguration is currently supported for is documented in this
// tool's help text; usese --help with this utility to check (or just read the
// text's definition in the add_options function below).

#include <google/protobuf/util/time_util.h>

#include "concmdconn.hpp"
#include "concmdopt.hpp"

using boost::program_options::options_description;
using boost::program_options::variables_map;
using com::vmware::concord::ConcordRequest;
using com::vmware::concord::ConcordResponse;
using com::vmware::concord::ErrorResponse;
using com::vmware::concord::ReconfigurationRequest;
using com::vmware::concord::ReconfigurationResponse;
using com::vmware::concord::ReconfigureTimePusherPeriodRequest;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using std::cerr;
using std::cout;
using std::endl;
using std::to_string;

namespace po = boost::program_options;

#define OPT_PUSHER_PERIOD "time_pusher_period_ms"

void add_options(options_description& desc) {
  // clang-format off
  desc.add_options()
    (OPT_PUSHER_PERIOD, po::value<int32_t>(), "Set the maximum interval with "
     "which this node should guarantee that its time is published, in "
     "milliseconds. A 0 or negative value means this node will stop "
     "proactively publishing time updates when no commands are being "
     "processed. This reconfiguration request will be effectively ignored by "
     "Concord if the time service is not enabled or the reconfigured Concord "
     "node is not a time source.");
  // clang-format on
}

int main(int argc, char** argv) {
  try {
    variables_map optionsInput;
    if (!parse_options(argc, argv, &add_options, optionsInput)) {
      return 0;
    }

    // Build a reconfiguration request containing all requested
    // reconfigurations.
    bool hasAnyRequest = false;
    ConcordRequest concReq;
    ReconfigurationRequest* reconfigReq =
        concReq.mutable_reconfiguration_request();

    if (optionsInput.count(OPT_PUSHER_PERIOD)) {
      ReconfigureTimePusherPeriodRequest* pusherReq =
          reconfigReq->mutable_reconfigure_time_pusher_period();
      Duration period = TimeUtil::MillisecondsToDuration(
          optionsInput[OPT_PUSHER_PERIOD].as<int32_t>());
      Duration* store = new Duration(period);
      pusherReq->set_allocated_time_pusher_period_ms(store);
      hasAnyRequest = true;
    }

    // Send the reconfiguration request if it isn't still empty.
    if (hasAnyRequest) {
      ConcordResponse concResp;
      if (call_concord(optionsInput, concReq, concResp)) {
        if (concResp.error_response_size() > 0) {
          cout << "Concord reported error(s) during requested reconfiguration:"
               << endl;
          for (size_t i = 0; i < concResp.error_response_size(); ++i) {
            ErrorResponse errorResp = concResp.error_response(i);
            if (errorResp.has_description()) {
              cout << " ErrorResponse " << to_string(i) << ": "
                   << errorResp.description() << endl;
            } else {
              cout << "ErrorResponse " << to_string(i) << " has no description."
                   << endl;
            }
          }
        }
        if (concResp.has_reconfiguration_response()) {
          ReconfigurationResponse reconfigResp =
              concResp.reconfiguration_response();
          if (!(reconfigResp.has_successful())) {
            cout << "Concord gave a ReconfigurationResponse, but did not "
                    "report whether the reconfiguration was successful."
                 << endl;
            return -1;
          } else if (reconfigResp.successful()) {
            cout << "Reconfiguration was successful." << endl;
          } else {
            cout << "Reconfiguration was NOT successful." << endl;
            return -1;
          }
        } else {
          cout << "Received unexpected response from Concord with no "
                  "ReconfigurationResponse."
               << endl;
          return -1;
        }
      } else {
        return -1;
      }
    } else {
      cout << "No reconfiguration was requested." << endl;
    }

  } catch (const std::exception& e) {
    cerr << "Exception: " << e.what() << endl;
    return -1;
  }

  return 0;
}
