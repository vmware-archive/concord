// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KV Blockchain client interface.

#ifndef CONCORD_CONSENSUS_CLIENT_INTERFACE_H_
#define CONCORD_CONSENSUS_CLIENT_INTERFACE_H_

#include "sliver.hpp"
#include "status.hpp"

namespace concord {
namespace consensus {
// configuration
// structs representing the actual configuration
// should be here since Client impl is not in the BFT responsibility,
// opposite to the replica
struct ClientConsensusConfig {
  uint16_t clientId;
  uint16_t maxFaulty;
  uint16_t maxSlow;
};

// Represents a client of the blockchain database
class IClient {
 public:
  virtual concordUtils::Status start() = 0;
  virtual concordUtils::Status stop() = 0;

  virtual bool isRunning() = 0;

  // Status of the client
  enum ClientStatus {
    UnknownError = -1,
    Idle = 0,  // Idle == the internal threads are not running now
    Running,
    Stopping,
  };

  typedef void (*CommandCompletion)(uint64_t completionToken,
                                    concordUtils::Status returnedStatus,
                                    concordUtils::Sliver outreply);

  virtual concordUtils::Status invokeCommandSynch(
      const char* request, uint32_t requestSize, bool isReadOnly,
      std::chrono::milliseconds timeout, uint32_t replySize, char* outReply,
      uint32_t* outActualReplySize) = 0;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_CLIENT_INTERFACE_H_
