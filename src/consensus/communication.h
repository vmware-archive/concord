// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// BFT Communication Configuration

#ifndef CONCORD_CONSENSUS_COMMUNICATION_H_
#define CONCORD_CONSENSUS_COMMUNICATION_H_

#include "communication/CommDefs.hpp"

namespace concord {
namespace consensus {

struct CommConfig {
  // common fields
  std::string listenIp;
  uint16_t listenPort;
  uint32_t bufferLength;
  std::unordered_map<NodeNum, NodeInfo> nodes;
  UPDATE_CONNECTIVITY_FN statusCallback;
  uint32_t selfId;

  // tcp
  uint32_t maxServerId;

  // tls (tcp fields should be set as well
  std::string certificatesRootPath;
  std::string cipherSuite;

  // possible values: "udp" and "tcp", lowercase
  std::string commType;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_COMMUNICATION_H_
