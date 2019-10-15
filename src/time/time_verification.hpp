// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// This header file includes the classes that may be used by the time service
// for verifying the legitimacy of time samples under currently supported time
// verification schemes. The currently supported schemes are "rsa-time-signing",
// in which an RSA cryptographic signature is added to each time sample to prove
// its legitimacy, and "bft-client-proxy-id", in which samples are validated on
// receipt based on the Concord-BFT client proxy ID through which the sample
// originally entered consensus (Concord-BFT should guarantee that it is
// intractable to impersonate a client proxy without knowledge of a private key
// it owns).
//
// TimeVerifier is an abstract class defining the interface through which the
// time service will use a time verification scheme implementation. Subclasses
// of TimeVerifier implementing this interface for each scheme we currently
// support are also declared int his header file. For time verification schemes
// that use cryptographic signatures or other data added to each time sample to
// prove its legitimacy, the TimeSigner class defines interface through which
// the time service will get these signatures. It and any subclass
// implementations of it for current time verification schemes are declared in
// this file.
//
// Note that, at the time of this writing, RSATimeSigner and RSATimeVerifier
// re-use replica RSA keys already in Concord's configuration for signing time
// samples in order to avoid needing to add more cryptographic keys to the
// configuration. We re-use the RSASigner and RSAVerifier from Concord-BFT for
// this purpose. Although these classes are from a source directory of
// Concord-BFT that we generally don't (and probably shouldn't) include from
// outside of Concord-BFT and these classes are themselves just fairly thin
// wrappers of crypto implementations from CryptoPP, we currently choose to go
// through the unsightliness of including them here because it is preferable to
// have this use of the RSA keys use the same signer/verifier classes as the
// keys' original use in case Concord-BFT later decides to change what RSA
// algorithms/parameters they are using from CryptoPP.

#ifndef TIME_TIME_VERIFICATION_HPP
#define TIME_TIME_VERIFICATION_HPP

#include <google/protobuf/timestamp.pb.h>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "concord_storage.pb.h"
#include "config/configuration_manager.hpp"
#include "src/bftengine/Crypto.hpp"

namespace concord {
namespace time {

// Given the logical content of a time update, computes the data that we expect
// a signature over to validate that time update if we are using a time
// verification scheme involving direct signatrues over time updates.
std::vector<uint8_t> GetSignableUpdateData(
    const std::string& source, const google::protobuf::Timestamp& time);

// A TimeSigner implementation is to be used in any time verification scheme
// requiring the addition of more data to time samples to prove their legitimacy
// (generally, such additional data would be a cryptographic signature).
// TimeSigner abstracts the logic to generate this data (ex: compute the
// cryptographic signature in the case that the data is a cryptographic
// signature). Note it is expected TimeSigner implementations will be
// constructed and owned by the specific time source that the TimeSigner whose
// updates the TimeSigner proves the legitimacy of.
class TimeSigner {
 public:
  virtual ~TimeSigner() {}

  // Compute and return (as a vector of uint8_t) the data to be included as
  // proof of legitimacy in a time sample with a given time for the time source
  // and under the time verification scheme for which the TimeSigner was
  // constructed.
  virtual std::vector<uint8_t> Sign(
      const google::protobuf::Timestamp& time) = 0;
};

// A TimeVerifier implementation is used to validate that a submitted time
// update is legitimate and not a forgery; how this validation is made may vary
// between different implementing subclasses.
class TimeVerifier {
 public:
  virtual ~TimeVerifier() {}

  // Check whether the time verification scheme implemented by a time verifier
  // uses signatures or other data proving the legitimacy of an update included
  // directly as part of a time sample.
  virtual bool UsesSignatures() const = 0;

  // Validate that a given update received from a time source through consensus
  // is legitimate, given information this interface currently considers
  // possibly relevant to update legitimacy, and return true if it is and false
  // otherwise. Parameters:
  // - source: The time source ID of the time source allegedly publishing this
  // update.
  // - client_id: The principal ID of the Concord-BFT client proxy from which
  // this update entered consensus. Note it should be intractable to forge a
  // request to the consensus layer as having originated from a client the
  // forger does not have control of as the consensus layer cryptographically
  // validates the identity of the client.
  // - time: The time sample for this update.
  // - signature: An optional pointer to additional data included with the time
  // sample that could prove its legitimacy (generally, this would be a
  // cryptographic signatures in time verification schemes that directly use
  // one). The pointer may be null if no additional data was included.
  virtual bool VerifyReceivedUpdate(
      const std::string& source, uint16_t client_id,
      const google::protobuf::Timestamp& time,
      const std::vector<uint8_t>* signature = nullptr) = 0;

  // Validate that a given time sample being loaded from storage at least could
  // be legitimate (samples are written to storage as Time::Sample messages
  // encoded with protobuf). This function should return false if the sample can
  // be shown to be illegitimate under the current verification scheme and true
  // otherwise. Note this function may not necessarily be able to conclude that
  // the sample is definitely legitimate, as certain metadata (particularly
  // metadata from the consensus layer may not actually be persistently recorded
  // by the time service).
  virtual bool VerifyRecordedUpdate(
      const com::vmware::concord::kvb::Time::Sample& record) = 0;
};

// Under a direct RSA crytography-based time verification scheme, an
// RSATimeSigner handles signing the time updates for a particular time source.
// A TimeSigner should produce signatures that can be validated with an
// RSATimeVerifier (see below).
class RSATimeSigner : public TimeSigner {
 public:
  // Constructor for an RSATimeSigner, given the node-specific configuration for
  // the Concord node, acting as a time source, whose time updates this
  // RSATimeSigner will be signing. This constructor will read the time source
  // ID and private RSA key for the Concord node from nodeConfig. Throws an
  // std::invalid_argument if a time source ID and appropriate private key
  // cannot be found in nodeConfig.
  explicit RSATimeSigner(
      const concord::config::ConcordConfiguration& node_config);

  explicit RSATimeSigner(const RSATimeSigner& original);
  virtual ~RSATimeSigner();
  RSATimeSigner& operator=(const RSATimeSigner& original);

  // Create/compute a cryptographic signature for a time update from the time
  // source this RSATimeSigner was constructed for with the given time value.
  // The signature is returned as a vector of bytes. May throw a TimeException
  // if the underlying cryptographic signature implementation unexpectedly
  // reports that it failed to produce a signature.
  virtual std::vector<uint8_t> Sign(
      const google::protobuf::Timestamp& time) override;

 private:
  std::string sourceID_;

  // The underlying signer implementation is stored through a pointer rather
  // than by value in order to defer initialization until partway through
  // RSATimeSigner's constructor so we can verify that we have the private key
  // we need in the given configuration. This allows us to avoid relying on
  // RSASigner to have a default constructor implemented. Note we also remember
  // the signer's original private key so that we will be able to copy this
  // TimeSigner without having to copy its internal RSASigner.
  std::unique_ptr<bftEngine::impl::RSASigner> signer_;
  std::string private_key_;
  std::mutex sign_mutex_;
};

// An RSATimeVerifier handles verification of time updates from any source under
// a direct RSA cryptography-based time verification scheme. RSATimeVerifier
// expects that the signatures it is verifying were made with an RSATimeSigner
// (see above).
class RSATimeVerifier : public TimeVerifier {
 public:
  // Construct an RSATimeVerifier, given the ConcordConfiguration for the
  // cluster; reads the configured time sources and their public keys from
  // config. Throws an std::invalid_argument if config differs structurally from
  // RSATimeVerifier's expectations of a Concord cluster's configuration, or if
  // the constructor cannot find the appropriate public key for any configured
  // time source.
  explicit RSATimeVerifier(const concord::config::ConcordConfiguration& config);

  explicit RSATimeVerifier(const RSATimeVerifier& original);
  virtual ~RSATimeVerifier();
  RSATimeVerifier& operator=(const RSATimeVerifier& original);

  virtual bool UsesSignatures() const override { return true; }

  // Override for TimerVerifier::VerifyReceivedUpdate to verify a time update
  // (with a given source and time) based on the provided cryptographic
  // signature for that update. Returns true if this is a valid time update and
  // false otherwise. Note causes for update invalidity under this direct RSA
  // cryptography-based scheme include that the source is unrecognized and that
  // the update does not match the signature under the claimed source's
  // configured public key.
  virtual bool VerifyReceivedUpdate(
      const std::string& source, uint16_t client_id,
      const google::protobuf::Timestamp& time,
      const std::vector<uint8_t>* signature = nullptr) override;

  // Override for TimeVerifier::VerifyRecordedUpdate to validate a time update
  // recorded in persistent storage. Since the cryptographic signatures for the
  // update should be recorded in a time verification scheme that uses them, the
  // recorded signature will be used to validate the update. Returns true if the
  // recorded update is found to be valid and false otherwise. Note that updates
  // with a timestamp equivalent to the earliest possible timestamp under the
  // current timestamp scheme may be found to be valid (assuming the source ID
  // is recognized) even if the signature is missing; this is because we assume
  // an unsigned update with the earliest possible time is used to represent the
  // state where no time update has yet been recorded for the named source.
  virtual bool VerifyRecordedUpdate(
      const com::vmware::concord::kvb::Time::Sample& record) override;

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

// TimeVerifier implementation which verifies the legitimacy of time samples
// based on the client proxy ID of the Concord-BFT client proxy used to submit
// the update. The consensus layer should guarantee that the client proxy it
// reports a request as having originated from is correct (that is, it is
// intractable to impersonate a client proxy or forge a request as having
// originated from a client proxy without knowing a private key that belongs to
// that client proxy). We also know from the configuration which client proxies
// are on which Concord nodes, so we can therefore tell based on the client
// proxy ID whether a time update (sent through a request) originated on the
// node matching the claimed time source ID for the update.
//
// Note this TimeVerifier implementation does not marginally require the use of
// any cryptographic signatures (i.e. the guarantees it provides are rooted in
// existing signatures Concord-BFT already uses); therefore, no corresponding
// TimeSigner implementation exists because ClientProxyIDTimeVerifier does not
// require one.
//
// Note since this approach to time verification does not add any new signatures
// to recorded time updates, a single replica's records kept for the time
// contract are no longer sufficient to prove to another party that that replica
// is not lying about the state of the time contract, as the records no longer
// include signatures to prove their legitimacy; one would need to consult
// multiple replicas (F + 1 of them that do not disagree on the state of the
// time contract specifically) to be sure one has found the legitimate state of
// the time contract. Also note this lack of recorded signatures prevents
// ClientProxyIDTimeVerifier from effectively detecting illegitimate samples
// being loaded to the time contract from storage, and therefore an attacker
// that can tamper with the time contract's persistent storage can falsify the
// state of the time contract (though it should be noted that this isn't the
// only serious attack that would be easy to an attacker with the access
// required to tamper with a Concord node's persistent storage).
class ClientProxyIDTimeVerifier : public TimeVerifier {
 public:
  // Construct a ClientProxyIDTimeVerifier, given the ConcordConfiguration for
  // the cluster; reads the configured time sources, which nodes they are
  // located on, and which nodes each client proxy is located on from the
  // config. Throws an std::invalid_argument if the config differs structurally
  // from ClientProxyIDTimeVerifier's expectations.
  explicit ClientProxyIDTimeVerifier(
      const concord::config::ConcordConfiguration& config);

  explicit ClientProxyIDTimeVerifier(const ClientProxyIDTimeVerifier& original);
  virtual ~ClientProxyIDTimeVerifier();
  ClientProxyIDTimeVerifier& operator=(
      const ClientProxyIDTimeVerifier& original);

  virtual bool UsesSignatures() const override { return false; }

  // Validate that a given update received is legitimate, returning true if it
  // is and false otherwise. ClientProxyIDTimeVerifier confirms legitimacy based
  // on whether the client proxy ID for the client proxy throuch which the
  // update was originally submitted matches the node whose time source ID is
  // the claimed source. Note that the signature field here is ignored for this
  // TimeVerifier implementation because ClientProxyIDTimeVerifier does not use
  // signatures.
  virtual bool VerifyReceivedUpdate(
      const std::string& source, uint16_t client_id,
      const google::protobuf::Timestamp& time,
      const std::vector<uint8_t>* signature = nullptr) override;

  // Validate that a given time sample loaded from storage cannot be shown to be
  // illegitimate, returning false if the sample is definitely illegitimate and
  // true otherwise. Note that ClientProxyIDTimeVerifier's time verification
  // approach cannot detect all illegitimate updates if storage has been
  // tampered with. Note an unrecognized time source ID is cause for
  // illegitimacy.
  virtual bool VerifyRecordedUpdate(
      const com::vmware::concord::kvb::Time::Sample& record) override;

 private:
  std::unordered_map<std::string, std::unordered_set<uint16_t>>
      client_proxies_by_time_source_;
};

}  // namespace time
}  // namespace concord

#endif  // TIME_TIME_VERIFICATION_HPP
