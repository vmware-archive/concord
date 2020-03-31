// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Time Contract is a state machine run by each replica to combine timestamps
// from other replicas into a non-decreasing time that has some resilience to
// false readings.
//
// Optionally, the time contract can validate updates received from other time
// sources in order to protect against Byzantine-faulty sources forging updates
// from other sources. Two means of time verification are currently available,
// "rsa-time-signing", which adds an additional cryptographic signature to each
// time update to prove its legitimacy, and "bft-client-proxy-id", in which time
// samples are verified based on the Concord-BFT client proxy IDs from which the
// samples enter consensus (Concord-BFT should guarantee it is intractable to
// impersonate a client proxy without a public key belonging to it).
//
// One important note is that reconfiguration of a cluster after it has been
// deployed to switch to or from a time verification mechanism that employs
// signatures is NOT supported. That is, if any node in a cluster has run at all
// before, we explicitly leave the behavior resulting from changing the time
// verification mechanism after that point undefined (even if one stops every
// node in the cluster before making this change). A possible exception to this
// is that it may be possible to switch time verification schemes if both the
// scheme being switched from and the scheme being switched to do not employ
// signatures added to each time update. Switching to, from, or between schemes
// is non-trivial and currently left undefined because signatures for time
// samples are persisted when a signature-employing verification scheme is
// enabled.
//
// Readings are submitted as (source, time, signature) triples (if a
// signature-based time verification scheme is in use) or (source, time) pairs
// (otherwise), where source is a string identifier of the submitter, time is a
// timestamp represetnted as google::protobuf::Timestamp, and signature (if
// present) is a digital signature proving that this time sample actually
// originates from the named source (based on a public key given for that time
// source in this Concord cluster's configuration).
//
// Aggregation can be done via any statistical means that give the guarantees
// discussed above. The current implementation is to choose the median of the
// most recent readings. See TimeContract::SummarizeTime.

#ifndef TIME_TIME_CONTRACT_HPP
#define TIME_TIME_CONTRACT_HPP

#include <google/protobuf/timestamp.pb.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blockchain/db_interfaces.h"
#include "config/configuration_manager.hpp"
#include "kv_types.hpp"
#include "storage/kvb_key_types.h"
#include "time_exception.hpp"
#include "time_verification.hpp"

namespace concord {
namespace time {

const int64_t kTimeStorageVersion = 1;

class TimeContract {
 public:
  // Constructor for TimeContract. Arguments:
  //   - storage: KVB Storage backend to persist the state of the time contract
  //   to.
  //   - config: ConcordConfiguration for this Concord cluster; note that the
  //   set of valid time sources, what time verification scheme (if any) is to
  //   be used, and possibly also configuration specific to the selected scheme
  //   will be taken from this configuration. An std::invalid argument may be
  //   thrown if the configuraion is missing any information this TimeContract
  //   needs or if the configuration otherwise differs from the Time Service's
  //   expectations.
  explicit TimeContract(
      const concord::storage::blockchain::ILocalKeyValueStorageReadOnly&
          storage,
      const concord::config::ConcordConfiguration& config)
      : logger_(log4cplus::Logger::getInstance("concord.time")),
        storage_(storage),
        config_(config),
        verifier_(),
        samples_(nullptr),
        changed_(false),
        time_samples_key_(new char[1]{concord::storage::kKvbKeyTimeSamples}, 1),
        summarized_time_key_(
            new char[1]{concord::storage::kKvbKeySummarizedTime}, 1) {
    if (config.hasValue<std::string>("time_verification")) {
      if (config.getValue<std::string>("time_verification") ==
          "rsa-time-signing") {
        verifier_.reset(new concord::time::RSATimeVerifier(config));
      } else if (config.getValue<std::string>("time_verification") ==
                 "bft-client-proxy-id") {
        verifier_.reset(new concord::time::ClientProxyIDTimeVerifier(config));
      } else if (config.getValue<std::string>("time_verification") != "none") {
        throw std::invalid_argument(
            "Cannot construct TimeContract: configuration contains "
            "unrecognized selection for time_verification: \"" +
            config.getValue<std::string>("time_verification") + "\".");
      }
    }
  }

  ~TimeContract() {
    if (samples_) {
      delete samples_;
    }
  }

  // Update the latest time reading from a given source. Any invalid update is
  // simply ignored, though the time contract may also choose to log
  // particularly suspicious updates (ignoring these updates enables the time
  // contract to automatically filter out both  updates duplicated or re-ordered
  // by the network (or attempted replay attackers) and any unconvincing
  // forgeries of updates). Arguments:
  //   - source: String identifier for the time source submitting this update.
  //   - client_id: Concord-BFT client proxy ID for the client proxy from which
  //   this time update was originally submitted to consensus (this information
  //   may or may not be used for time verification depending on how this
  //   TimeContract is configured). It is expected that this client ID has been
  //   received from the consensus layer along with the provided update and that
  //   the consensus layer guarantees that it is intractable to impersonate
  //   Concord-BFT clients proxies without their private keys.
  //   - time: Time value for this update.
  //   - signature: Optional pointer to a signature proving this update comes
  //   from the claimed source. If a time verification scheme that uses
  //   signatures is enabled, the update will be rejected if this pointer does
  //   not provide a signature proving the update's legitimacy under the
  //   configured scheme. Note this pointer will be completely ignored if a
  //   signature-based time verification scheme is not in use.
  // Returns the current time reading after making this update, which may or may
  // not have increased since before the update.
  // Throws a TimeException if this operation causes the TimeContract to load
  // state from its persistent storage but the data loaded is corrupted or
  // otherwise invalid.
  google::protobuf::Timestamp Update(
      const std::string& source, const uint16_t client_id,
      const google::protobuf::Timestamp& time,
      const std::vector<uint8_t>* signature = nullptr);

  // Get the current time as specified by this TimeContract based on what
  // samples it currently has. Throws a TimeException if this operation causes
  // the TimeContract to load state from its persistent storage but the data
  // loaded is corrupted or otherwise invalid.
  google::protobuf::Timestamp GetTime();

  // Get the time at the passed block ID based on the summarized time saved in
  // storage. The summarized time is the time as returned by GetTime() at the
  // time of block creation. If no summarized time is set for the passed block
  // ID, the summarized time at the most recent block before it is returned.
  // Throws a TimeException if the summarized time cannot be read from storage.
  google::protobuf::Timestamp GetSummarizedTimeAtBlock(
      concordUtils::BlockId) const;

  // Has the contract been updated since being loaded or since last
  // serialization?
  bool Changed() { return changed_; }

  bool SignaturesEnabled() const {
    return (verifier_ && verifier_->UsesSignatures());
  }

  // Produce a key-value pair that encodes the state of the time contract for
  // KVB.
  pair<concordUtils::Sliver, concordUtils::Sliver> Serialize();

  // Produce a key-value pair that encodes the current summarized time as
  // returned by GetTime() .
  pair<concordUtils::Sliver, concordUtils::Sliver> SerializeSummarizedTime();

  // Clear all cached data.
  void Reset() {
    if (samples_) {
      delete samples_;
    }
    changed_ = false;
  }

  // Struct containing the data stored for the latest time sample known from
  // each source. SampleBody specifically excludes the name of the source the
  // sample is for because it is intended for use in data structures and things
  // where time samples are looked up by source name.
  struct SampleBody {
    // Time for this sample.
    google::protobuf::Timestamp time;

    // Optional pointer to a cryptographic signature. If a signature-based time
    // verification scheme is enabled, this signature should prove this sample
    // was published by the time source it is recorded for. Otherwise, this
    // pointer should point to null.
    std::unique_ptr<std::vector<uint8_t>> signature;
  };

  // Gets a const reference to the current set of time samples this time
  // contract has. The samples are returned in a map from time source names to
  // TimeContract::SampleBody structs containing the data for the latest
  // verified sample known from that source. Throws a TimeException if this
  // operation causes the TimeContract to load state from its persistent storage
  // but the data loaded is corrupted or otherwise invalid.
  const std::unordered_map<std::string, SampleBody>& GetSamples();

 private:
  log4cplus::Logger logger_;
  const concord::storage::blockchain::ILocalKeyValueStorageReadOnly& storage_;
  const concord::config::ConcordConfiguration& config_;
  std::unique_ptr<concord::time::TimeVerifier> verifier_;
  std::unordered_map<std::string, SampleBody>* samples_;
  bool changed_;
  const concordUtils::Sliver time_samples_key_;
  const concordUtils::Sliver summarized_time_key_;

  void LoadLatestSamples();
  google::protobuf::Timestamp SummarizeTime();
};

}  // namespace time
}  // namespace concord

#endif  // TIME_TIME_CONTRACT_HPP
