// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef INTERNAL_COMMANDS_HANDLER_HPP
#define INTERNAL_COMMANDS_HANDLER_HPP

#include "Logger.hpp"
#include "consensus/sliver.hpp"
#include "simpleKVBTestsBuilder.hpp"
#include "storage/blockchain_interfaces.h"

class InternalCommandsHandler : public concord::storage::ICommandsHandler {
 public:
  InternalCommandsHandler(
      concord::storage::ILocalKeyValueStorageReadOnly *storage,
      concord::storage::IBlocksAppender *blocksAppender,
      concordlogger::Logger &logger)
      : m_storage(storage),
        m_blocksAppender(blocksAppender),
        m_logger(logger) {}

  int execute(uint16_t clientId, uint64_t sequenceNum, bool readOnly,
              uint32_t requestSize, const char *request, uint32_t maxReplySize,
              char *outReply, uint32_t &outActualReplySize) override;

 private:
  bool executeWriteCommand(uint32_t requestSize, const char *request,
                           uint64_t sequenceNum, size_t maxReplySize,
                           char *outReply, uint32_t &outReplySize);

  bool executeReadOnlyCommand(uint32_t requestSize, const char *request,
                              size_t maxReplySize, char *outReply,
                              uint32_t &outReplySize);

  bool verifyWriteCommand(
      uint32_t requestSize,
      const BasicRandomTests::SimpleCondWriteRequest &request,
      size_t maxReplySize, uint32_t &outReplySize) const;

  bool executeReadCommand(uint32_t requestSize, const char *request,
                          size_t maxReplySize, char *outReply,
                          uint32_t &outReplySize);

  bool executeGetLastBlockCommand(uint32_t requestSize, size_t maxReplySize,
                                  char *outReply, uint32_t &outReplySize);

  void addMetadataKeyValue(concord::storage::SetOfKeyValuePairs &updates,
                           uint64_t sequenceNum) const;

 private:
  static concord::consensus::Sliver buildSliverFromStaticBuf(char *buf);

 private:
  concord::storage::ILocalKeyValueStorageReadOnly *m_storage;
  concord::storage::IBlocksAppender *m_blocksAppender;
  concordlogger::Logger &m_logger;
  size_t m_readsCounter = 0;
  size_t m_writesCounter = 0;
  size_t m_getLastBlockCounter = 0;
};

#endif
