// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// KV Blockchain replica interface.

#ifndef CONCORD_CONSENSUS_REPLICA_INTERFACE_H_
#define CONCORD_CONSENSUS_REPLICA_INTERFACE_H_

#include "bftengine/Replica.hpp"
#include "commands_handler.h"
#include "sliver.hpp"
#include "status.hpp"

namespace concord {
namespace consensus {
// configuration
struct ReplicaConsensusConfig {
  // F value - max number of faulty/malicious replicas. fVal >= 1
  uint16_t fVal;

  // C value. cVal >=0
  uint16_t cVal;

  // unique identifier of the replica.
  // The number of replicas in the system should be N = 3*fVal + 2*cVal + 1
  // In the current version, replicaId should be a number between 0 and  N-1
  // replicaId should also represent this replica in ICommunication.
  uint16_t replicaId;

  // number of objects that represent clients.
  // numOfClientProxies >= 1
  uint16_t numOfClientProxies;

  // a time interval in milliseconds. represents how often the replica sends a
  // status report to the other replicas. statusReportTimerMillisec > 0
  uint16_t statusReportTimerMillisec;

  // number of consensus operations that can be executed in parallel
  // 1 <= concurrencyLevel <= 30
  uint16_t concurrencyLevel;

  // autoViewChangeEnabled=true , if the automatic view change protocol is
  // enabled
  bool autoViewChangeEnabled;

  // a time interval in milliseconds. represents the timeout used by the  view
  // change protocol (TODO: add more details)
  uint16_t viewChangeTimerMillisec;

  // public keys of all replicas. map from replica identifier to a public key
  std::set<std::pair<uint16_t, std::string>> publicKeysOfReplicas;

  // private key of the current replica
  std::string replicaPrivateKey;

  /// TODO(IG): the fields below not be here,
  /// their init should happen within BFT library

  // signer and verifier of a threshold signature (for threshold fVal+1 out of
  // N) In the current version, both should be nullptr
  IThresholdSigner* thresholdSignerForExecution;
  IThresholdVerifier* thresholdVerifierForExecution;

  // signer and verifier of a threshold signature (for threshold N-fVal-cVal out
  // of N)
  IThresholdSigner* thresholdSignerForSlowPathCommit;
  IThresholdVerifier* thresholdVerifierForSlowPathCommit;

  // signer and verifier of a threshold signature (for threshold N-cVal out of
  // N) If cVal==0, then both should be nullptr
  IThresholdSigner* thresholdSignerForCommit;
  IThresholdVerifier* thresholdVerifierForCommit;

  // signer and verifier of a threshold signature (for threshold N out of N)
  IThresholdSigner* thresholdSignerForOptimisticCommit;
  IThresholdVerifier* thresholdVerifierForOptimisticCommit;

  // Messages
  uint32_t maxExternalMessageSize = 0;
  uint32_t maxReplyMessageSize = 0;

  // StateTransfer
  uint32_t maxNumOfReservedPages = 0;
  uint32_t sizeOfReservedPage = 0;

  // If set to true, this replica will periodically log debug statistics such as
  // throughput and number of messages sent.
  bool debugStatisticsEnabled = false;
};

// Represents a replica of the blockchain database
class IReplica {
 public:
  virtual concordUtils::Status start() = 0;
  virtual concordUtils::Status stop() = 0;
  virtual ~IReplica(){};

  // status of the replica
  enum class RepStatus {
    UnknownError = -1,
    Idle = 0,  // Idle == the internal threads are not running now
    Starting,
    Running,
    Stopping
  };

  virtual bool isRunning() = 0;

  // returns the current status of the replica
  virtual RepStatus getReplicaStatus() const = 0;

  // this callback is called by the library every time the replica status
  // is changed
  typedef void (*StatusNotifier)(RepStatus newStatus);
  /*
   * TODO(GG): Implement:
   *  virtual Status setStatusNotifier(StatusNotifier statusNotifier);
   */

  // Used to read from storage, only when a replica is Idle. Useful for
  // initialization and maintenance.
  virtual const concord::storage::blockchain::ILocalKeyValueStorageReadOnly&
  getReadOnlyStorage() = 0;

  // Used to append blocks to storage, only when a replica is Idle. Useful
  // for initialization and maintenance.
  virtual concordUtils::Status addBlockToIdleReplica(
      const concordUtils::SetOfKeyValuePairs& updates) = 0;

  /// TODO(IG) the following methods are probably temp solution,
  /// need to split interfaces implementations to differrent modules
  /// instead of being all implemented bt ReplicaImpl
  virtual void set_command_handler(ICommandsHandler* handler) = 0;
};

}  // namespace consensus
}  // namespace concord

#endif  // CONCORD_CONSENSUS_REPLICA_INTERFACE_H_
