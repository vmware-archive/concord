// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Temporary solution for creating configuration structs for concord-bft
// Ideally, the ReplicaConfig from BlockchainInterfaces.h

#ifndef CONCORD_CONSENSUS_BFT_CONFIGURATION_HPP_
#define CONCORD_CONSENSUS_BFT_CONFIGURATION_HPP_

#include <set>
#include <string>
#include "IThresholdFactory.h"
#include "IThresholdSigner.h"
#include "IThresholdVerifier.h"
#include "config/configuration_manager.hpp"
#include "storage/blockchain_interfaces.h"

namespace concord {
namespace consensus {

const size_t MAX_ITEM_LENGTH = 4096;
const std::string MAX_ITEM_LENGTH_STR = std::to_string(MAX_ITEM_LENGTH);

void initializeSBFTThresholdPublicKeys(
    concord::config::ConcordConfiguration& config, bool isClient, uint16_t f,
    uint16_t c, bool supportDirectProofs,
    IThresholdVerifier*& thresholdVerifierForExecution,
    IThresholdVerifier*& thresholdVerifierForSlowPathCommit,
    IThresholdVerifier*& thresholdVerifierForCommit,
    IThresholdVerifier*& thresholdVerifierForOptimisticCommit) {
  concord::config::ConcordPrimaryConfigurationAuxiliaryState* auxState;
  assert(auxState = dynamic_cast<
             concord::config::ConcordPrimaryConfigurationAuxiliaryState*>(
             config.getAuxiliaryState()));

  if (supportDirectProofs) {
    assert(auxState->executionCryptosys);
    thresholdVerifierForExecution =
        auxState->executionCryptosys->createThresholdVerifier();
  }

  // The Client class only needs the f+1 parameters
  if (isClient) {
    return;
  }

  assert(auxState->slowCommitCryptosys);
  thresholdVerifierForSlowPathCommit =
      auxState->slowCommitCryptosys->createThresholdVerifier();

  if (c > 0) {
    assert(auxState->commitCryptosys);
    thresholdVerifierForCommit =
        auxState->commitCryptosys->createThresholdVerifier();
  }

  assert(auxState->optimisticCommitCryptosys);
  thresholdVerifierForOptimisticCommit =
      auxState->optimisticCommitCryptosys->createThresholdVerifier();
}

/*
 * Reads the secret keys for the multisig and threshold schemes!
 */
void initializeSBFTThresholdPrivateKeys(
    concord::config::ConcordConfiguration& config, uint16_t myReplicaId,
    uint16_t f, uint16_t c, IThresholdSigner*& thresholdSignerForExecution,
    IThresholdSigner*& thresholdSignerForSlowPathCommit,
    IThresholdSigner*& thresholdSignerForCommit,
    IThresholdSigner*& thresholdSignerForOptimisticCommit,
    bool supportDirectProofs) {
  concord::config::ConcordPrimaryConfigurationAuxiliaryState* auxState;
  assert(auxState = dynamic_cast<
             concord::config::ConcordPrimaryConfigurationAuxiliaryState*>(
             config.getAuxiliaryState()));

  // f + 1
  if (supportDirectProofs) {
    assert(auxState->executionCryptosys);
    thresholdSignerForExecution =
        auxState->executionCryptosys->createThresholdSigner();
  } else {
    printf("\n does not support direct proofs!");
  }

  // 2f + c + 1
  assert(auxState->slowCommitCryptosys);
  thresholdSignerForSlowPathCommit =
      auxState->slowCommitCryptosys->createThresholdSigner();

  // 3f + c + 1
  if (c > 0) {
    assert(auxState->commitCryptosys);
    thresholdSignerForCommit =
        auxState->commitCryptosys->createThresholdSigner();
  } else {
    printf("\n c <= 0");
  }

  // Reading multisig secret keys for the case where everybody sign case where
  // everybody signs
  assert(auxState->optimisticCommitCryptosys);
  thresholdSignerForOptimisticCommit =
      auxState->optimisticCommitCryptosys->createThresholdSigner();
}

inline bool initializeSBFTCrypto(
    uint16_t nodeId, uint16_t numOfReplicas, uint16_t maxFaulty,
    uint16_t maxSlow, concord::config::ConcordConfiguration& config,
    concord::config::ConcordConfiguration& replicaConfig,
    std::set<std::pair<uint16_t, std::string>> publicKeysOfReplicas,
    concord::storage::ReplicaConsensusConfig* outConfig) {
  // Threshold signatures
  IThresholdSigner* thresholdSignerForExecution;
  IThresholdVerifier* thresholdVerifierForExecution;

  IThresholdSigner* thresholdSignerForSlowPathCommit;
  IThresholdVerifier* thresholdVerifierForSlowPathCommit;

  IThresholdSigner* thresholdSignerForCommit;
  IThresholdVerifier* thresholdVerifierForCommit;

  IThresholdSigner* thresholdSignerForOptimisticCommit;
  IThresholdVerifier* thresholdVerifierForOptimisticCommit;

  /// TODO(IG): move to config
  const bool supportDirectProofs = false;

  initializeSBFTThresholdPublicKeys(
      config, false, maxFaulty, maxSlow, supportDirectProofs,
      thresholdVerifierForExecution, thresholdVerifierForSlowPathCommit,
      thresholdVerifierForCommit, thresholdVerifierForOptimisticCommit);

  initializeSBFTThresholdPrivateKeys(
      config, nodeId + 1, maxFaulty, maxSlow, thresholdSignerForExecution,
      thresholdSignerForSlowPathCommit, thresholdSignerForCommit,
      thresholdSignerForOptimisticCommit, supportDirectProofs);

  outConfig->publicKeysOfReplicas = publicKeysOfReplicas;

  outConfig->thresholdSignerForExecution = nullptr;
  outConfig->thresholdVerifierForExecution = nullptr;

  outConfig->thresholdSignerForSlowPathCommit =
      thresholdSignerForSlowPathCommit;
  outConfig->thresholdVerifierForSlowPathCommit =
      thresholdVerifierForSlowPathCommit;

  outConfig->thresholdSignerForCommit = nullptr;
  outConfig->thresholdVerifierForCommit = nullptr;

  outConfig->thresholdSignerForOptimisticCommit =
      thresholdSignerForOptimisticCommit;
  outConfig->thresholdVerifierForOptimisticCommit =
      thresholdVerifierForOptimisticCommit;

  return true;
}

inline bool initializeSBFTPrincipals(
    concord::config::ConcordConfiguration& config, uint16_t selfNumber,
    uint16_t numOfPrincipals, uint16_t numOfReplicas,
    concord::storage::CommConfig* outCommConfig,
    std::set<std::pair<uint16_t, std::string>>& outReplicasPublicKeys) {
  uint16_t clientProxiesPerReplica =
      config.getValue<uint16_t>("client_proxies_per_replica");
  for (uint16_t i = 0; i < numOfReplicas; ++i) {
    concord::config::ConcordConfiguration& nodeConfig =
        config.subscope("node", i);
    concord::config::ConcordConfiguration& replicaConfig =
        nodeConfig.subscope("replica", 0);
    uint16_t replicaId = replicaConfig.getValue<uint16_t>("principal_id");
    uint16_t replicaPort = replicaConfig.getValue<uint16_t>("replica_port");
    outReplicasPublicKeys.insert(
        {replicaId, replicaConfig.getValue<std::string>("public_key")});
    if (outCommConfig) {
      outCommConfig->nodes.insert(
          {replicaId,
           NodeInfo{replicaConfig.getValue<std::string>("replica_host"),
                    replicaPort, true}});
      if (replicaId == selfNumber) {
        outCommConfig->listenPort = replicaPort;
      }
      for (uint16_t j = 0; j < clientProxiesPerReplica; ++j) {
        concord::config::ConcordConfiguration& clientConfig =
            nodeConfig.subscope("client_proxy", j);
        uint16_t clientId = clientConfig.getValue<uint16_t>("principal_id");
        uint16_t clientPort = clientConfig.getValue<uint16_t>("client_port");
        outCommConfig->nodes.insert(
            {clientId,
             NodeInfo{clientConfig.getValue<std::string>("client_host"),
                      clientPort, false}});
        if (clientId == selfNumber) {
          outCommConfig->listenPort = clientPort;
        }
      }
    }
  }

  if (outCommConfig) {
    outCommConfig->bufferLength =
        config.getValue<uint32_t>("concord-bft_communication_buffer_length");

    /// TODO(IG): add to config file
    outCommConfig->listenIp = "0.0.0.0";
    outCommConfig->maxServerId = numOfReplicas - 1;
    outCommConfig->selfId = selfNumber;
    outCommConfig->cipherSuite =
        config.getValue<std::string>("tls_cipher_suite_list");
    outCommConfig->certificatesRootPath =
        config.getValue<std::string>("tls_certificates_folder_path");
    outCommConfig->commType = config.getValue<std::string>("comm_to_use");
  }

  return true;
}

inline bool initializeSBFTConfiguration(
    concord::config::ConcordConfiguration& config,
    concord::config::ConcordConfiguration& nodeConfig,
    concord::storage::CommConfig* commConfig,
    concord::storage::ClientConsensusConfig* clConf, uint16_t clientIndex,
    concord::storage::ReplicaConsensusConfig* repConf) {
  assert(!clConf != !repConf);

  // Initialize random number generator
  srand48(getpid());

  concord::config::ConcordConfiguration& replicaConfig =
      nodeConfig.subscope("replica", 0);
  uint16_t selfNumber = (repConf)
                            ? (replicaConfig.getValue<uint16_t>("principal_id"))
                            : (nodeConfig.subscope("client_proxy", clientIndex)
                                   .getValue<uint16_t>("principal_id"));
  uint16_t maxFaulty = config.getValue<uint16_t>("f_val");
  uint16_t maxSlow = config.getValue<uint16_t>("c_val");
  uint16_t numOfPrincipals = config.getValue<uint16_t>("num_principals");
  uint16_t numOfReplicas = config.getValue<uint16_t>("num_replicas");

  std::set<pair<uint16_t, string>> publicKeysOfReplicas;
  if (commConfig) {
    bool res = initializeSBFTPrincipals(config, selfNumber, numOfPrincipals,
                                        numOfReplicas, commConfig,
                                        publicKeysOfReplicas);
    if (!res) return false;
  }

  if (repConf) {
    repConf->replicaPrivateKey =
        replicaConfig.getValue<std::string>("private_key");

    bool res = initializeSBFTCrypto(selfNumber, numOfReplicas, maxFaulty,
                                    maxSlow, config, replicaConfig,
                                    publicKeysOfReplicas, repConf);
    if (!res) return false;

    repConf->publicKeysOfReplicas = publicKeysOfReplicas;
    repConf->viewChangeTimerMillisec =
        config.getValue<uint16_t>("view_change_timeout");
    repConf->statusReportTimerMillisec =
        config.getValue<uint16_t>("status_time_interval");
    repConf->concurrencyLevel = config.getValue<uint16_t>("concurrency_level");

    repConf->replicaId = selfNumber;
    repConf->fVal = maxFaulty;
    repConf->cVal = maxSlow;
    repConf->numOfClientProxies = numOfPrincipals - numOfReplicas;

    repConf->debugStatisticsEnabled =
        nodeConfig.getValue<bool>("concord-bft_enable_debug_statistics");

    // TODO(IG): add to config file
    repConf->autoViewChangeEnabled = true;

#define DEFAULT(field, userCfg)                            \
  {                                                        \
    if (config.hasValue<uint32_t>(userCfg)) {              \
      repConf->field = config.getValue<uint32_t>(userCfg); \
    }                                                      \
  }
    DEFAULT(maxExternalMessageSize, "concord-bft_max_external_message_size");
    DEFAULT(maxReplyMessageSize, "concord-bft_max_reply_message_size");
    DEFAULT(maxNumOfReservedPages, "concord-bft_max_num_of_reserved_pages");
    DEFAULT(sizeOfReservedPage, "concord-bft_size_of_reserved_page");
#undef DEFAULT
  } else {
    clConf->clientId = selfNumber;
    clConf->maxFaulty = maxFaulty;
    clConf->maxSlow = maxSlow;
  }

  return true;
}

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_BFT_CONFIGURATION_HPP_
