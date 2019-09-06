// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KV Blockchain client definition.

#ifndef CONCORD_CONSENSUS_CLIENT_IMP_H_
#define CONCORD_CONSENSUS_CLIENT_IMP_H_

#include <boost/thread.hpp>
#include <chrono>
#include <map>
#include "ICommunication.hpp"
#include "SimpleClient.hpp"
#include "storage/blockchain_interfaces.h"

namespace concord {
namespace consensus {

concord::storage::IClient *createClient(
    concord::storage::CommConfig &commConfig,
    const concord::storage::ClientConsensusConfig &conf);

void releaseClient(concord::storage::IClient *r);

class ClientImp : public concord::storage::IClient {
 public:
  // concord::storage::IClient methods
  virtual Status start() override;
  virtual Status stop() override;

  virtual bool isRunning() override;

  virtual Status invokeCommandSynch(const char *request, uint32_t requestSize,
                                    bool isReadOnly,
                                    std::chrono::milliseconds timeout,
                                    uint32_t replySize, char *outReply,
                                    uint32_t *outActualReplySize) override;

 protected:
  // ctor & dtor
  ClientImp(concord::storage::CommConfig &commConfig,
            const concord::storage::ClientConsensusConfig &conf);
  virtual ~ClientImp();

  int m_status;

  friend concord::storage::IClient *createClient(
      concord::storage::CommConfig &commConfig,
      const concord::storage::ClientConsensusConfig &conf);
  friend void releaseClient(concord::storage::IClient *r);

 private:
  bftEngine::SimpleClient *m_bftClient = nullptr;
  bftEngine::SeqNumberGeneratorForClientRequests *m_SeqNumGenerator = nullptr;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_CLIENT_IMP_H_
