// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
// This module creates an instance of ClientImp class using input
// parameters and launches a bunch of tests created by TestsBuilder towards
// concord::consensus::ReplicaImp objects.

#include <stdio.h>
#include <string.h>

#include "basicRandomTestsRunner.hpp"
#include "consensus/bft_configuration.hpp"
#include "consensus/client_imp.h"
#include "test_comm_config.hpp"
#include "test_parameters.hpp"

#ifndef _WIN32
#include <sys/param.h>
#include <unistd.h>
#else
#include "winUtils.h"
#endif

using namespace bftEngine;
using namespace BasicRandomTests;

using std::string;

using concord::consensus::createClient;
using concord::storage::BlockId;
using concord::storage::ClientConsensusConfig;
using concord::storage::CommConfig;
using concord::storage::IClient;

#if defined(_WIN32)
initWinSock();
#endif

ClientParams setupClientParams(int argc, char **argv) {
  ClientParams clientParams;
  clientParams.clientId = UINT16_MAX;
  clientParams.numOfFaulty = UINT16_MAX;
  clientParams.numOfSlow = UINT16_MAX;
  clientParams.numOfOperations = UINT16_MAX;
  char argTempBuffer[PATH_MAX + 10];
  int o = 0;
  while ((o = getopt(argc, argv, "i:f:c:p:n:")) != EOF) {
    switch (o) {
      case 'i': {
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        string idStr = argTempBuffer;
        int tempId = std::stoi(idStr);
        if (tempId >= 0 && tempId < UINT16_MAX)
          clientParams.clientId = (uint16_t)tempId;
      } break;

      case 'f': {
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        string fStr = argTempBuffer;
        int tempfVal = std::stoi(fStr);
        if (tempfVal >= 1 && tempfVal < UINT16_MAX)
          clientParams.numOfFaulty = (uint16_t)tempfVal;
      } break;

      case 'c': {
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        string cStr = argTempBuffer;
        int tempcVal = std::stoi(cStr);
        if (tempcVal >= 0 && tempcVal < UINT16_MAX)
          clientParams.numOfSlow = (uint16_t)tempcVal;
      } break;

      case 'p': {
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        string numOfOpsStr = argTempBuffer;
        int tempfVal = std::stoi(numOfOpsStr);
        if (tempfVal >= 1 && tempfVal < UINT32_MAX)
          clientParams.numOfOperations = (uint32_t)tempfVal;
      } break;

      case 'n': {
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        clientParams.configFileName = argTempBuffer;
      } break;

      default:
        break;
    }
  }
  return clientParams;
}

auto logger = concordlogger::Log::getLogger("skvbtest.client");

CommConfig setupCommunicationParams(ClientParams &clientParams) {
  CommConfig commParams;

  auto numOfReplicas = clientParams.get_numOfReplicas();
  commParams.maxServerId = numOfReplicas - 1;
  TestCommConfig testCommConfig(logger);

#ifdef USE_COMM_PLAIN_TCP
  PlainTcpConfig commConfig = testCommConfig.GetTCPConfig(
      true, clientParams.clientId, clientParams.numOfClients, numOfReplicas,
      clientParams.configFileName);
  commParams.maxServerId = commConfig.maxServerId;
  commParams.commType = "tcp";
#elif USE_COMM_TLS_TCP
  TlsTcpConfig commConfig = testCommConfig.GetTlsTCPConfig(
      true, clientParams.clientId, clientParams.numOfClients, numOfReplicas,
      clientParams.configFileName);
  commParams.certificatesRootPath = commConfig.certificatesRootPath;
  commParams.commType = "tls";
  commParams.cipherSuite = commConfig.cipherSuite;
#else
  PlainUdpConfig commConfig = testCommConfig.GetUDPConfig(
      true, clientParams.clientId, clientParams.numOfClients, numOfReplicas,
      clientParams.configFileName);
  commParams.commType = "udp";
#endif

  commParams.listenIp = commConfig.listenIp;
  commParams.listenPort = commConfig.listenPort;
  commParams.bufferLength = commConfig.bufferLength;
  commParams.nodes = commConfig.nodes;
  commParams.statusCallback = commConfig.statusCallback;
  commParams.selfId = commConfig.selfId;

  return commParams;
}

ClientConsensusConfig setupConsensusParams(ClientParams &clientParams) {
  ClientConsensusConfig consensusConfig;
  consensusConfig.clientId = clientParams.clientId;
  consensusConfig.maxFaulty = clientParams.numOfFaulty;
  consensusConfig.maxSlow = clientParams.numOfSlow;
  return consensusConfig;
}

int main(int argc, char **argv) {
  ClientParams clientParams = setupClientParams(argc, argv);
  if (clientParams.clientId == UINT16_MAX ||
      clientParams.numOfFaulty == UINT16_MAX ||
      clientParams.numOfSlow == UINT16_MAX ||
      clientParams.numOfOperations == UINT32_MAX) {
    LOG_ERROR(logger, "Wrong usage! Required parameters: "
                          << argv[0] << " -f F -c C -p NUM_OPS -i ID");
    exit(-1);
  }

  ClientConsensusConfig consensusConfig = setupConsensusParams(clientParams);
  CommConfig commConfig = setupCommunicationParams(clientParams);
  IClient *client = createClient(commConfig, consensusConfig);
  BasicRandomTestsRunner testsRunner(logger, *client,
                                     clientParams.numOfOperations);
  testsRunner.run();
}
