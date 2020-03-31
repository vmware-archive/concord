// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_PRUNING_RSA_PRUNING_SIGNER_HPP
#define CONCORD_PRUNING_RSA_PRUNING_SIGNER_HPP

#include "config/configuration_manager.hpp"
#include "src/bftengine/Crypto.hpp"

#include "concord.pb.h"

namespace concord {
namespace pruning {

// This class signs pruning messages via the replica's private key that it gets
// through the configuration. Message contents used to generate the signature
// are generated via the mechanisms provided in pruning_serialization.hpp/cpp .
class RSAPruningSigner {
 public:
  // Construct by passing the configuration for the node the signer is running
  // on.
  RSAPruningSigner(const concord::config::ConcordConfiguration& node_config);

  // Sign() methods sign the passed message and store the signature in the
  // 'signature' field of the message. An exception is thrown on error.
  void Sign(com::vmware::concord::LatestPrunableBlock&) const;
  void Sign(com::vmware::concord::PruneRequest&) const;

 private:
  std::string GetSignatureBuffer() const;

 private:
  bftEngine::impl::RSASigner signer_;
};

}  // namespace pruning
}  // namespace concord

#endif  // CONCORD_PRUNING_RSA_PRUNING_SIGNER_HPP
