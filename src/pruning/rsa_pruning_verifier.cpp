// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "rsa_pruning_verifier.hpp"
#include "pruning_exception.hpp"
#include "pruning_serialization.hpp"

#include <exception>
#include <iterator>
#include <string>

namespace concord {
namespace pruning {

using namespace detail;

RSAPruningVerifier::RSAPruningVerifier(
    const concord::config::ConcordConfiguration& config) {
  if (!config.containsScope("node") || config.scopeSize("node") == 0) {
    throw PruningConfigurationException{
        "RSAPruningVerifier couldn't find node configurations"};
  }

  for (auto i = 0u; i < config.scopeSize("node"); ++i) {
    const auto& node_config = config.subscope("node", i);
    const auto& replica_config = node_config.subscope("replica", 0);

    // Load replicas.
    {
      replicas_.push_back(
          Replica{replica_config.getValue<std::uint64_t>("principal_id"),
                  replica_config.getValue<std::string>("public_key").c_str()});

      const auto ins_res = replica_ids_.insert(replicas_.back().principal_id);
      if (!ins_res.second) {
        throw PruningConfigurationException{
            "RSAPruningVerifier found duplicate replica principal_id: " +
            std::to_string(replicas_.back().principal_id)};
      }

      const auto& replica = replicas_.back();
      principal_to_replica_idx_[replica.principal_id] = i;
    }

    // Load client proxies to support the PruneRequest message over the API by:
    //  - using the client proxy principal_id as a sender
    //  - using the private key of the replica running on the node to sign the
    //  message in the API before forwarding through the client proxy
    for (auto proxy_id = 0u; proxy_id < node_config.scopeSize("client_proxy");
         ++proxy_id) {
      const auto& client_proxy_config =
          node_config.subscope("client_proxy", proxy_id);
      const auto principal_id =
          client_proxy_config.getValue<std::uint64_t>("principal_id");
      principal_to_replica_idx_[principal_id] = i;
    }
  }
}

bool RSAPruningVerifier::Verify(
    const com::vmware::concord::LatestPrunableBlock& block) const {
  if (!block.has_replica() || !block.has_block_id() || !block.has_signature()) {
    return false;
  }

  // LatestPrunableBlock can only be sent by replicas and not by client proxies.
  if (replica_ids_.find(block.replica()) == std::end(replica_ids_)) {
    return false;
  }

  std::string ser;
  ser << block.replica() << block.block_id();
  return Verify(block.replica(), ser, block.signature());
}

bool RSAPruningVerifier::Verify(
    const com::vmware::concord::PruneRequest& request) const {
  if (!request.has_sender() || !request.has_signature() ||
      request.latest_prunable_block_size() !=
          static_cast<int>(replica_ids_.size())) {
    return false;
  }

  // PruneRequest can only be sent by client proxies and not by replicas.
  if (replica_ids_.find(request.sender()) != std::end(replica_ids_)) {
    return false;
  }

  // Verify the request as a whole.
  std::string ser;
  ser << request.sender();
  for (auto i = 0; i < request.latest_prunable_block_size(); ++i) {
    ser << request.latest_prunable_block(i);
  }
  if (!Verify(request.sender(), ser, request.signature())) {
    return false;
  }

  // Verify that *all* replicas have responded with valid responses.
  auto replica_ids_to_verify = replica_ids_;
  for (auto i = 0; i < request.latest_prunable_block_size(); ++i) {
    const auto& block = request.latest_prunable_block(i);
    if (!Verify(block)) {
      return false;
    }

    auto it = replica_ids_to_verify.find(block.replica());
    if (it == std::end(replica_ids_to_verify)) {
      return false;
    }
    replica_ids_to_verify.erase(it);
  }

  return replica_ids_to_verify.empty();
}

bool RSAPruningVerifier::Verify(std::uint64_t sender, const std::string& ser,
                                const std::string& signature) const {
  auto it = principal_to_replica_idx_.find(sender);
  if (it == std::cend(principal_to_replica_idx_)) {
    return false;
  }

  return GetReplica(it->second)
      .verifier.verify(ser.data(), ser.length(), signature.c_str(),
                       signature.length());
}

const RSAPruningVerifier::Replica& RSAPruningVerifier::GetReplica(
    ReplicaVector::size_type idx) const {
  return replicas_[idx];
}

}  // namespace pruning
}  // namespace concord
