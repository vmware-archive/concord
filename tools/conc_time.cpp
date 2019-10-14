// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Update and/or read the time contract.
//
// Some of the options to this tool are mutually exclusive. Here are some ways
// the tool should or should not be used:
//
// The "get" and "list" options can be used together (to get both the summary
// and the sample list at once), and can also be used with any other option (for
// example to first publish an update and then read the result), except for
// "nosend", since the tool does actually have to send the request in order to
// read the time.
//
// When publishing a time sample, set the sample value with --time. Then use one
// of the following options to set the ID and (if applicable) signature:
//
//  * Specify just --config. This will use the time_source_id from that file. If
//    the configuraiton enables a time verificaiton scheme that uses signatures,
//    the appropriate signing key from the config file will also be used to
//    produce a signature; otherwise the update will be sent without a
//    signature.
//
//  * Specify --config and -n. If the given configuration enables a time
//    verification scheme that uses signatures, this will print the name of the
//    time source this config file corresponds to and a signature for the sample
//    value using the time_source_id and the appropriate signing key from the
//    given file. Otherwise, only the source will be printed.
//
//  * Specify --source and --signature. This will send your sample with your
//    chosen source ID and your chosen signature. Note the signature will be
//    ignored in the event Concord is not configured with a time verification
//    scheme that uses signatures.
//
// Some uses that might cause your sample to be rejected:
//
//  * Specifying only --source if a time verification scheme that uses
//    signatures is configured. The empty signature will not match.
//
//  * Specifying only --signature. The empty source will not match.
//
//  * Specifying the wrong --source for our --config, your --signature, or the
//    Concord node you are running this tool on. If time verification is
//    configured, unconvincing impersonations of a source from a different node
//    may be rejected.
//
// A short recipe book of expected uses:
//
// ```
// # get the current summary time
// conc_time -g
//
// # update the current time for this source
// conc_time -c /concord/config-local/concord.config -t 1000000000
//
// # prepare a future update for this source
// conc_time -c /concord/config-local/concord.config -t 1000000000 -n
//
// # use pre-prepared update
// conc_time -s time-source1 -t 1000000000 -x <signature output from previous
// command, if applicable>
// ```

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <inttypes.h>
#include <boost/program_options.hpp>
#include <iomanip>
#include <iostream>

#include "concmdconn.hpp"
#include "concmdex.hpp"
#include "concmdfmt.hpp"
#include "concmdopt.hpp"
#include "concord.pb.h"
#include "config/configuration_manager.hpp"
#include "time/time_verification.hpp"

using namespace boost::program_options;
using namespace com::vmware::concord;
using concord::config::ConcordConfiguration;
using concord::config::YAMLConfigurationInput;
using concord::time::RSATimeSigner;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;
using std::cerr;
using std::endl;

#define OPT_SOURCE "source"
#define OPT_TIME "time"
#define OPT_SIGNATURE "signature"
#define OPT_CONFIG "config"
#define OPT_GET "get"
#define OPT_LIST "list"
#define OPT_NO_SEND "nosend"

void add_options(options_description &desc) {
  // clang-format off
  desc.add_options()
    (OPT_SOURCE ",s", value<std::string>(), "Source of the update sample")
    (OPT_TIME ",t", value<uint64_t>(), "Time of the update sample")
    (OPT_SIGNATURE ",x", value<std::string>(),
     "Signature of the time and sample. Hex-encoded.")
    (OPT_CONFIG ",c", value<std::string>(),
     "Concord config file to get the time source configuration from.")
    (OPT_GET ",g", bool_switch()->default_value(false),
     "Fetch the accumulated time")
    (OPT_LIST ",l", bool_switch()->default_value(false),
     "Fetch all stored samples")
    (OPT_NO_SEND ",n", bool_switch()->default_value(false),
     "Do not send the request; only print the configured source and (if the "
     "configuration enables a time verification scheme that uses signatures) a "
     "signature for the given sample. Requires \'" OPT_CONFIG "\' parameter");
  // clang-format on
}

// Allows sending malformed sample (no value included) for testing
void require_sample(TimeRequest *timeReq, TimeSample **sample) {
  if (!*sample) {
    *sample = timeReq->mutable_sample();
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
    TimeRequest *timeReq = concReq.mutable_time_request();
    TimeSample *sample = nullptr;

    if (opts.count(OPT_TIME) > 0) {
      require_sample(timeReq, &sample);
      Timestamp time = google::protobuf::util::TimeUtil::SecondsToTimestamp(
          opts[OPT_TIME].as<uint64_t>());
      Timestamp *store = new Timestamp(time);
      sample->set_allocated_time(store);
    }
    if (opts[OPT_GET].as<bool>()) {
      timeReq->set_return_summary(true);
    }
    if (opts[OPT_LIST].as<bool>()) {
      timeReq->set_return_samples(true);
    }

    if (opts.count(OPT_TIME) == 0 &&
        !(opts[OPT_GET].as<bool>() || opts[OPT_LIST].as<bool>())) {
      // if no options are specified, default to just "get"
      timeReq->set_return_summary(true);
    }

    ConcordConfiguration config;
    ConcordConfiguration nodeConfig;
    if (opts.count(OPT_CONFIG) > 0) {
      std::ifstream fileInput(opts[OPT_CONFIG].as<std::string>());
      specifyConfiguration(config);
      YAMLConfigurationInput input(fileInput);
      input.parseInput();
      concord::config::loadNodeConfiguration(config, input);
      size_t nodeIndex = concord::config::detectLocalNode(config);
      nodeConfig = config.subscope("node", nodeIndex);
    }

    if (opts.count(OPT_SOURCE) > 0) {
      require_sample(timeReq, &sample);
      sample->set_source(opts[OPT_SOURCE].as<std::string>());
    } else if (opts.count(OPT_CONFIG) > 0) {
      require_sample(timeReq, &sample);
      sample->set_source(nodeConfig.getValue<string>("time_source_id"));
    }

    if (opts.count(OPT_SIGNATURE) > 0) {
      require_sample(timeReq, &sample);
      std::string bytes;
      dehex0x(opts[OPT_SIGNATURE].as<std::string>(), bytes);
      sample->set_signature(bytes);

      // Note that, under the current implementation of this conc_time utility,
      // if any additional time verification schemes that use signatures are
      // added, an else-if case to create a signature under each new scheme
      // added will need to be manually added here.
    } else if ((opts.count(OPT_CONFIG) > 0) &&
               config.hasValue<string>("time_verification") &&
               (config.getValue<string>("time_verification") ==
                "rsa-time-signing")) {
      require_sample(timeReq, &sample);
      RSATimeSigner signer(nodeConfig);
      std::vector<uint8_t> signature = signer.Sign(sample->time());
      sample->set_signature(signature.data(), signature.size());
    }

    if (opts[OPT_NO_SEND].as<bool>()) {
      if (timeReq->has_return_summary() || timeReq->has_return_samples()) {
        std::cerr << "--" << OPT_NO_SEND << " used with --" << OPT_GET
                  << " or --" OPT_LIST << " will not produce useful output."
                  << std::endl;
        return -1;
      }

      if (!sample) {
        cerr << "No sample was given." << endl;
        return -1;
      }

      // Note that, under the current implementation of this conc_time utility,
      // if any additional time verification schemes that use signatures are
      // added, this condition will need to be modified to recognizes whether
      // one of them is in use from the configuration.
      bool signing_enable =
          (opts.count(OPT_CONFIG) > 0) &&
          config.hasValue<string>("time_verification") &&
          (config.getValue<string>("time_verification") == "rsa-time-signing");

      if (signing_enable && !sample->has_signature()) {
        std::cerr << "No signature was generated." << std::endl;
        return -1;
      }

      std::cout << "Source: " << sample->source() << std::endl;
      if (signing_enable) {
        std::string bytes;
        hex0x(sample->signature(), bytes);
        std::cout << "Signature: " << bytes << std::endl;
      }
      return 0;
    }

    // Send & Receive

    ConcordResponse concResp;
    if (call_concord(opts, concReq, concResp)) {
      if (concResp.has_time_response()) {
        TimeResponse tr = concResp.time_response();
        if (tr.has_summary()) {
          std::cout << "The current time is: "
                    << TimeUtil::ToString(tr.summary()) << std::endl;
        }

        if (tr.sample_size() > 0) {
          for (int i = 0; i < tr.sample_size(); i++) {
            TimeSample ts = tr.sample(i);
            std::cout << "Sample " << (i + 1) << " from \'"
                      << (ts.has_source() ? ts.source() : "[unknown]")
                      << "\' read "
                      << (ts.has_time() ? TimeUtil::ToString(ts.time())
                                        : "[unknown]")
                      << std::endl;
          }
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
        std::cerr << "No time_response found, and wrong number of  errors ("
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
