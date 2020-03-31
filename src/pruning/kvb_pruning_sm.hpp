// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_PRUNING_KVB_PRUNING_SM_HPP
#define CONCORD_PRUNING_KVB_PRUNING_SM_HPP

#include <log4cplus/logger.h>

#include "rsa_pruning_signer.hpp"
#include "rsa_pruning_verifier.hpp"

#include "blockchain/db_interfaces.h"
#include "config/configuration_manager.hpp"
#include "kv_types.hpp"
#include "time/time_contract.hpp"

#include "concord.pb.h"

#include <opentracing/tracer.h>

#include <cstdint>

namespace concord {
namespace pruning {

// This class implements the KVB pruning state machine. Main functionalities
// include executing pruning based on configuration policy and replica states as
// well as providing read-only information to the operator node.
//
// The following configuration options are honored by the state machine:
// * pruning_enabled - a system-wide configuration option that indicates if
// pruning is enabled for the replcia. If set to false,
// LatestPrunableBlockRequest will return 0 as a latest block(indicating no
// blocks can be pruned) and PruneRequest will return an error. If not
// specified, a value of false is assumed.
//
//  * pruning_num_blocks_to_keep - a system-wide configuration option that
//  specifies a minimum number of blocks to always keep in storage when pruning.
//  If not specified, a value of 0 is assumed. If
//  pruning_duration_to_keep_minutes is specified too, the more conservative
//  pruning range of the two options will be used (the one that prunes less
//  blocks).
//
//  * pruning_duration_to_keep_minutes - a system-wide configuration option that
//  specifies a time range (in minutes) from now to the past that determines
//  which blocks to keep and which are older than (now -
//  pruning_duration_to_keep_minutes) and can, therefore, be pruned. If not
//  specified, a value of 0 is assumed. If pruning_num_blocks_to_keep is
//  specified too, the more conservative pruning range of the two will be used
//  (the one that prunes less blocks). This option requires the time service to
//  be enabled.
//
// The LatestPrunableBlockRequest command returns the latest block ID from the
// replica's storage that is safe to prune. If no blocks can be pruned, 0 is
// returned.
//
// The PruneRequest command prunes blocks from storage by:
//  - verifying the PruneRequest message's signature
//  - verifying that the number of LatestPrunableBlock messages in the
//  PruneRequest is equal to the number of replicas in the system
//  - verifying the signatures of individual LatestPrunableBlock messages in the
//  PruneRequest.
// If all above conditions are met, the state machine will prune blocks from the
// genesis block up to the the minimum of all the block IDs in
// LatestPrunableBlock messages in the PruneRequest message.
class KVBPruningSM {
 public:
  // Construct by providing an interface to the storage engine, configuration
  // and tracing facilities.
  KVBPruningSM(const storage::blockchain::ILocalKeyValueStorageReadOnly&,
               const config::ConcordConfiguration& config,
               const config::ConcordConfiguration& node_config,
               concord::time::TimeContract*);

  // Handles a ConcordRequest that contains a pruning request. If an invalid
  // request has been passed or no pruning request is contained, the request is
  // ignored.
  void Handle(const com::vmware::concord::ConcordRequest&,
              com::vmware::concord::ConcordResponse&, bool read_only,
              opentracing::Span& parent_span) const;

 private:
  void Handle(const com::vmware::concord::LatestPrunableBlockRequest&,
              com::vmware::concord::ConcordResponse&,
              opentracing::Span& parent_span) const;
  void Handle(const com::vmware::concord::PruneRequest&,
              com::vmware::concord::ConcordResponse&, bool read_only,
              opentracing::Span& parent_span) const;

  concordUtils::BlockId LatestBasedOnNumBlocks() const;
  concordUtils::BlockId LatestBasedOnTimeRange() const;

 private:
  log4cplus::Logger logger_;
  RSAPruningSigner signer_;
  RSAPruningVerifier verifier_;
  const storage::blockchain::ILocalKeyValueStorageReadOnly& ro_storage_;
  concord::time::TimeContract* time_contract_{nullptr};
  bool pruning_enabled_{false};
  std::uint64_t replica_id_{0};
  std::uint64_t num_blocks_to_keep_{0};
  std::uint32_t duration_to_keep_minutes_{0};
};

}  // namespace pruning
}  // namespace concord

#endif  // CONCORD_PRUNING_KVB_PRUNING_SM_HPP
