// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Commands handler interface from KVB replica.

#ifndef CONSENSUS_COMMANDS_HANDLER_HPP_
#define CONSENSUS_COMMANDS_HANDLER_HPP_

#include "bftengine/Replica.hpp"

namespace concord {
namespace consensus {

// Upcall interface from KVBlockchain to application using it as storage.
class ICommandsHandler : public bftEngine::RequestsHandler {
 public:
  virtual ~ICommandsHandler() = 0;
  virtual int execute(uint16_t clientId, uint64_t sequenceNum, bool readOnly,
                      uint32_t requestSize, const char* request,
                      uint32_t maxReplySize, char* outReply,
                      uint32_t& outActualReplySize) = 0;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONSENSUS_COMMANDS_HANDLER_HPP_
