// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_PRUINING_RSA_PRUNING_VERIFIER_HPP
#define CONCORD_PRUINING_RSA_PRUNING_VERIFIER_HPP

#include "config/configuration_manager.hpp"
#include "src/bftengine/Crypto.hpp"

#include "concord.pb.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace concord {
namespace pruning {

// This class verifies pruning messages that were signed by serializing message
// contents via mechanisms provided in pruning_serialization.hpp/cpp . Public
// keys for verification are extracted from the passed configuration.
//
// Idea is to use the principal_id as an ID that identifies senders in pruning
// messages since it is unique across clients and replicas.
class RSAPruningVerifier {
 public:
  // Construct by passing the system configuration.
  RSAPruningVerifier(const concord::config::ConcordConfiguration& config);

  // Verify() methods verify that the message comes from the advertised sender.
  // Methods return true on successful verification and false on unsuccessful.
  // An exception is thrown on error.
  bool Verify(const com::vmware::concord::LatestPrunableBlock&) const;
  bool Verify(const com::vmware::concord::PruneRequest&) const;

 private:
  struct Replica {
    std::uint64_t principal_id{0};
    bftEngine::impl::RSAVerifier verifier;
  };

  bool Verify(std::uint64_t sender, const std::string& ser,
              const std::string& signature) const;

  using ReplicaVector = std::vector<Replica>;

  // Get a replica from the replicas vector by its index.
  const Replica& GetReplica(ReplicaVector::size_type idx) const;

  // A vector of all the replicas in the system.
  ReplicaVector replicas_;
  // We map a principal_id to a replica index in the replicas_ vector to be able
  // to verify a message through the Replica's verifier that is associated with
  // its public key.
  std::unordered_map<std::uint64_t, ReplicaVector::size_type>
      principal_to_replica_idx_;

  // Contains a set of replica principal_ids for use in verification. Filled in
  // once during construction.
  std::unordered_set<std::uint64_t> replica_ids_;
};

}  // namespace pruning
}  // namespace concord

#endif  // CONCORD_PRUINING_RSA_PRUNING_VERIFIER_HPP
