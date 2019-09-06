// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_STORAGE_BLOCKCHAIN_INTERFACES_H_
#define CONCORD_STORAGE_BLOCKCHAIN_INTERFACES_H_

// TODO: write about thread-safety

#include <Replica.hpp>
#include <chrono>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include "CommDefs.hpp"
#include "ICommunication.hpp"
#include "SimpleBCStateTransfer.hpp"
#include "StatusInfo.h"
#include "ThresholdSignaturesSchemes.h"
#include "consensus/sliver.hpp"
#include "consensus/status.hpp"
#include "storage/database_interface.h"

using std::pair;
using std::string;
using std::unordered_map;

namespace concord {
namespace storage {

// forward declarations
class ILocalKeyValueStorageReadOnlyIterator;
class ILocalKeyValueStorageReadOnly;
class IBlocksAppender;
class ICommandsHandler;
class ReplicaStateSync;

// Communication
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

// REPLICA

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
  virtual Status start() = 0;
  virtual Status stop() = 0;
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
  virtual const ILocalKeyValueStorageReadOnly& getReadOnlyStorage() = 0;

  // Used to append blocks to storage, only when a replica is Idle. Useful
  // for initialization and maintenance.
  virtual Status addBlockToIdleReplica(const SetOfKeyValuePairs& updates) = 0;

  /// TODO(IG) the following methods are probably temp solution,
  /// need to split interfaces implementations to differrent modules
  /// instead of being all implemented bt ReplicaImpl
  virtual void set_command_handler(ICommandsHandler* handler) = 0;
};

// CLIENT

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
  virtual Status start() = 0;
  virtual Status stop() = 0;

  virtual bool isRunning() = 0;

  // Status of the client
  enum ClientStatus {
    UnknownError = -1,
    Idle = 0,  // Idle == the internal threads are not running now
    Running,
    Stopping,
  };

  typedef void (*CommandCompletion)(uint64_t completionToken,
                                    Status returnedStatus, Sliver outreply);

  virtual Status invokeCommandSynch(const char* request, uint32_t requestSize,
                                    bool isReadOnly,
                                    std::chrono::milliseconds timeout,
                                    uint32_t replySize, char* outReply,
                                    uint32_t* outActualReplySize) = 0;
};

// COMMANDS HANDLER

// Upcall interface from KVBlockchain to application using it as storage.
class ICommandsHandler : public bftEngine::RequestsHandler {
 public:
  virtual ~ICommandsHandler() = 0;
  virtual int execute(uint16_t clientId, uint64_t sequenceNum, bool readOnly,
                      uint32_t requestSize, const char* request,
                      uint32_t maxReplySize, char* outReply,
                      uint32_t& outActualReplySize) = 0;
};

// STORAGE MODELS

class ILocalKeyValueStorageReadOnly {
 public:
  // convenience where readVersion==latest, and block is not needed?
  virtual Status get(Key key, Value& outValue) const = 0;
  virtual Status get(BlockId readVersion, Sliver key, Sliver& outValue,
                     BlockId& outBlock) const = 0;

  virtual BlockId getLastBlock() const = 0;
  virtual Status getBlockData(BlockId blockId,
                              SetOfKeyValuePairs& outBlockData) const = 0;
  // TODO(GG): explain motivation
  virtual Status mayHaveConflictBetween(Sliver key, BlockId fromBlock,
                                        BlockId toBlock,
                                        bool& outRes) const = 0;

  virtual ILocalKeyValueStorageReadOnlyIterator* getSnapIterator() const = 0;
  virtual Status freeSnapIterator(
      ILocalKeyValueStorageReadOnlyIterator* iter) const = 0;

  virtual void monitor() const = 0;
};

/*
 * TODO: Iterator cleanup. The general interface should expose only
 * getCurrent() and isEnd(), and there should be two interfaces inheriting
 * from it:
 * 1. One which is "clean", without awareness to blocks
 * 2. Second which is aware of blocks and versions.
 */
class ILocalKeyValueStorageReadOnlyIterator {
 public:
  virtual KeyValuePair first(BlockId readVersion, BlockId& actualVersion,
                             bool& isEnd) = 0;
  virtual KeyValuePair first() = 0;

  // Assumes lexicographical ordering of the keys, seek the first element
  // k >= key
  virtual KeyValuePair seekAtLeast(BlockId readVersion, Key key,
                                   BlockId& actualVersion, bool& isEnd) = 0;
  virtual KeyValuePair seekAtLeast(Key key) = 0;

  // Proceed to next element and return it
  virtual KeyValuePair next(BlockId readVersion, Key key,
                            BlockId& actualVersion, bool& isEnd) = 0;
  virtual KeyValuePair next() = 0;

  // Return current element without moving
  virtual KeyValuePair getCurrent() = 0;

  // Check if iterator is in the end.
  virtual bool isEnd() = 0;
};

// Blocks appender
class IBlocksAppender {
 public:
  virtual Status addBlock(const SetOfKeyValuePairs& updates,
                          BlockId& outBlockId) = 0;
};

// TBDs9gg0:
// (1) Allow direct reading from the replicas
// (2) State transfer in the key-val blockchain (and not in the consensus
//     module)

}  // namespace storage
}  // namespace concord

#endif  // CONCORD_STORAGE_BLOCKCHAIN_INTERFACES_H_
