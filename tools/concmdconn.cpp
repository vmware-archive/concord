// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// concord connection for command line tools.

#include "concmdconn.hpp"
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <boost/asio.hpp>
#include <iostream>
#include "concmdopt.hpp"
#include "concord.pb.h"

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::tcp;

void format_message(boost::program_options::variables_map &opts,
                    google::protobuf::Message &message, std::string &pbtext) {
  if (opts[OPT_FORMAT].as<std::string>() == OPT_FORMAT_TEXT) {
    google::protobuf::TextFormat::PrintToString(message, &pbtext);
  } else if (opts[OPT_FORMAT].as<std::string>() == OPT_FORMAT_JSON) {
    pbtext.clear();
    google::protobuf::util::MessageToJsonString(message, &pbtext);
  } else {
    pbtext = "Unknown output format";
  }
}

/**
 * Send a request to concord, and wait for the response. Returns true if a valid
 * response was received, or false if any error happened.
 */
bool call_concord(boost::program_options::variables_map &opts,
                  com::vmware::concord::ConcordRequest &request,
                  com::vmware::concord::ConcordResponse &response) {
  std::string pbtext;
  format_message(opts, request, pbtext);
  std::cout << "Message prepared: " << pbtext << std::endl;

  /*** Open connection ***/

  io_service io_service;
  tcp::socket s(io_service);
  tcp::resolver resolver(io_service);
  boost::asio::connect(s, resolver.resolve({opts[OPT_ADDRESS].as<std::string>(),
                                            opts[OPT_PORT].as<std::string>()}));

  std::cout << "Connected" << std::endl;

  /*** Send request ***/

  std::string pb;
  request.SerializeToString(&pb);
  size_t msglen = request.ByteSize();
  // only sixteen bits available
  assert(msglen < 0x10000);
  // little-endian!
  uint8_t prefix[2] = {(uint8_t)msglen, (uint8_t)(msglen >> 8)};

  boost::asio::write(s, boost::asio::buffer(prefix, 2));
  boost::asio::write(s, boost::asio::buffer(pb, msglen));

  std::cout << "Message Sent (" << msglen << " bytes)" << std::endl;

  /*** Receive response ***/
  bool result = true;

  size_t reply_length = boost::asio::read(s, boost::asio::buffer(prefix, 2));
  if (reply_length != 2) {
    std::cerr << "Did not read full prefix, reply_length = " << reply_length
              << std::endl;
    result = false;
  } else {
    // little-endian!
    msglen = ((size_t)prefix[1] << 8) | prefix[0];
    char reply[msglen];
    reply_length = boost::asio::read(s, boost::asio::buffer(reply, msglen));
    if (reply_length != msglen) {
      std::cerr << "Did not read full reply, expected " << msglen
                << " bytes, but got " << reply_length << std::endl;
    } else {
      // deserialize into response
      if (!response.ParseFromString(std::string(reply, msglen))) {
        std::cerr << "Failed to parse respons" << std::endl;
        result = false;
      } else {
        format_message(opts, response, pbtext);
        std::cout << "Received response: " << pbtext << std::endl;
      }
    }
  }

  /*** Close Connection ***/
  s.close();

  return result;
}
