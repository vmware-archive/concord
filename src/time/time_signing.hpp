// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// This header file includes the classes to be used by the time service for
// signing time samples (TimeSigner) and validating those
// signatures(TimeVerifier).
//
// At the time of this writing, we re-use replica RSA keys already in Concord's
// configuration for signing time samples in order to avoid needing to add more
// cryptographic keys to the configuration. We re-use the RSASigner and
// RSAVerifier for this purpose. Although these classes are from a source
// directory of Concord-BFT that we generally don't (and probably shouldn't)
// include from outside of Concord-BFT and these classes are themselves just
// fairly thin wrappers of crypto implementations from CryptoPP, we currently
// choose to go through the unsightliness of including them here because it is
// preferable to have this use of the RSA keys use the same signer/verifier
// classes as the keys' original use in case Concord-BFT later decides to change
// what RSA algorithms/parameters they are using from CryptoPP.

#ifndef TIME_TIME_SIGNING_HPP
#define TIME_TIME_SIGNING_HPP

#include <google/protobuf/timestamp.pb.h>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "config/configuration_manager.hpp"
#include "src/bftengine/Crypto.hpp"

namespace concord {
namespace time {

// Given the logical content of a time update, computes the data that we
// expect a signature over to validate that time update.
std::vector<uint8_t> GetSignableUpdateData(
    const std::string& source, const google::protobuf::Timestamp& time);

// A TimeSigner handles signing the time updates for a particular time source. A
// TimeSigner should produce signatures that can be validated with a
// TimeVerifier (see below).
class TimeSigner {
 public:
  // Constructor for a TimeSigner, given the node-specific configuration for the
  // Concord node, acting as a time source, whose time updates this TimeSigner
  // will be signing. This constructor will read the time source ID and private
  // key for the Concord node from nodeConfig. Throws an std::invalid_argument
  // if a time source ID and appropriate private key cannot be found in
  // nodeConfig.
  explicit TimeSigner(const concord::config::ConcordConfiguration& node_config);

  explicit TimeSigner(const TimeSigner& original);
  TimeSigner& operator=(const TimeSigner& original);

  // Create/compute a cryptographic signature for a time update from the time
  // source this TimeSigner was constructed for with the given time value. The
  // signature is returned as a vector of bytes. May throw a TimeException if
  // the underlying cryptographic signature implementation unexpectedly reports
  // that it failed to produce a signature.
  std::vector<uint8_t> Sign(const google::protobuf::Timestamp& time);

 private:
  std::string sourceID_;

  // The underlying signer implementation is stored through a pointer rather
  // than by value in order to defer initialization until partway through
  // TimeSigner's constructor so we can verify that we have the private key we
  // need in the given configuration. This allows us to avoid relying on
  // RSASigner to have a default constructor implemented. Note we also remember
  // the signer's original private key so that we will be able to copy this
  // TimeSigner without having to copy its internal RSASigner.
  std::unique_ptr<bftEngine::impl::RSASigner> signer_;
  std::string private_key_;
  std::mutex sign_mutex_;
};

// A TimeVerifier handles verification of time updates, given the source, new
// time, and signature for the update from any source in the cluster.
// TimeVerifier expects that the signatures it is verifying were made with a
// TimeSigner (see above).
class TimeVerifier {
 public:
  // Construct a TimeVerifier, given the ConcordConfiguration for the cluster;
  // reads the configured time sources and their public keys from config. Throws
  // an std::invalid_argument if config differs structurally from TimeVerifier's
  // expectations of a Concord cluster's configuration, or if the constructor
  // cannot find the public key for any configured time source.
  explicit TimeVerifier(const concord::config::ConcordConfiguration& config);

  explicit TimeVerifier(const TimeVerifier& original);
  ~TimeVerifier();
  TimeVerifier& operator=(const TimeVerifier& original);

  // Check whether this verifier recognizes the named time source and can verify
  // its signatures. Returns true if this verifier recognizes the time source
  // and false otherwise.
  bool HasTimeSource(const std::string& source) const;

  // Verify a time update, given the ID for the update's time source, the time
  // of the update, and a signature provint this update comes from the claimed
  // source. Returns true if this is a valid time update and false otherwise.
  // Note causes for update invalidity include that the source is unrecognized
  // and that the update does not match the signature under the claimed source's
  // configured public key.
  bool Verify(const std::string& source,
              const google::protobuf::Timestamp& time,
              const std::vector<uint8_t>& signature);

 private:
  // Note we store the RSAVerifiers in the map through unique_ptrs rather than
  // by value; this is to make sure we can avoid using any copy/move
  // constructors/assignment operators of RSAVerifier. At the time of this
  // writing, it is believed copying/moving RSAVerifiers can ultimately cause
  // segmentation faults. Note we also remember the public keys this
  // TimeVerifier was constructed with to facilitate copying it without having
  // to copy any of the underlying RSAVerifiers.
  std::unordered_map<std::string, std::unique_ptr<bftEngine::impl::RSAVerifier>>
      verifiers_;
  std::unordered_map<std::string, std::string> public_keys_;
};

}  // namespace time
}  // namespace concord

#endif  // TIME_TIME_SIGNING_HPP
