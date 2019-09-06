// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KV Blockchain client implementation.

#include "client_imp.h"

#include "CommFactory.hpp"

using bftEngine::ICommunication;
using bftEngine::PlainUdpConfig;
using bftEngine::SeqNumberGeneratorForClientRequests;
using bftEngine::SimpleClient;
using bftEngine::TlsTcpConfig;
using concord::storage::ClientConsensusConfig;
using concord::storage::CommConfig;
using concord::storage::IClient;

namespace concord {
namespace consensus {

/**
 * in current impl, no start semantics needed
 */
Status ClientImp::start() {
  m_status = Running;
  return Status::OK();
}

/**
 * Switches status to "Stopping", then waits for jobs in thread pool to complete
 * before moving to "Idle".
 */
Status ClientImp::stop() {
  m_status = Idle;
  return Status::OK();
}

bool ClientImp::isRunning() { return (m_status == Running); }

/**
 * execute the command synchronously
 */
Status ClientImp::invokeCommandSynch(const char *request, uint32_t requestSize,
                                     bool isReadOnly,
                                     std::chrono::milliseconds timeout,
                                     uint32_t replySize, char *outReply,
                                     uint32_t *outActualReplySize) {
  uint64_t timeoutMs = timeout <= std::chrono::milliseconds::zero()
                           ? SimpleClient::INFINITE_TIMEOUT
                           : timeout.count();
  auto seqNum = m_SeqNumGenerator->generateUniqueSequenceNumberForRequest();
  auto res = m_bftClient->sendRequest(isReadOnly, request, requestSize, seqNum,
                                      timeoutMs, replySize, outReply,
                                      *outActualReplySize);

  assert(res >= -2 && res < 1);

  if (res == 0)
    return Status::OK();
  else if (res == -1)
    return Status::GeneralError("timeout");
  else
    return Status::InvalidArgument("small buffer");
}

IClient *createClient(CommConfig &commConfig,
                      const ClientConsensusConfig &conf) {
  return new ClientImp(commConfig, conf);
}

void releaseClient(IClient *r) {
  ClientImp *p = (ClientImp *)r;
  delete p;
}

ClientImp::ClientImp(CommConfig &commConfig, const ClientConsensusConfig &conf)
    : m_status(Idle) {
  ICommunication *comm = nullptr;
  if (commConfig.commType == "tls") {
    TlsTcpConfig config(commConfig.listenIp, commConfig.listenPort,
                        commConfig.bufferLength, commConfig.nodes,
                        commConfig.maxServerId, commConfig.selfId,
                        commConfig.certificatesRootPath, commConfig.cipherSuite,
                        commConfig.statusCallback);
    comm = bftEngine::CommFactory::create(config);
  } else if (commConfig.commType == "udp") {
    PlainUdpConfig config(commConfig.listenIp, commConfig.listenPort,
                          commConfig.bufferLength, commConfig.nodes,
                          commConfig.selfId, commConfig.statusCallback);
    comm = bftEngine::CommFactory::create(config);
  } else {
    throw std::invalid_argument("Unknown communication module type" +
                                commConfig.commType);
  }

  comm->Start();
  m_bftClient = SimpleClient::createSimpleClient(comm, conf.clientId,
                                                 conf.maxFaulty, conf.maxSlow);
  m_SeqNumGenerator = SeqNumberGeneratorForClientRequests::
      createSeqNumberGeneratorForClientRequests();
}

ClientImp::~ClientImp() {
  if (m_bftClient) delete m_bftClient;
  if (m_SeqNumGenerator) delete m_SeqNumGenerator;
}

}  // namespace consensus
}  // namespace concord
