// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "time_contract.hpp"

#include <google/protobuf/util/time_util.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "concord_storage.pb.h"
#include "config/configuration_manager.hpp"
#include "sliver.hpp"

using std::unordered_set;
using std::vector;

using concord::config::ConcordConfiguration;
using concord::config::ConfigurationPath;
using concord::config::ParameterSelection;
using concordUtils::BlockId;
using concordUtils::Sliver;
using concordUtils::Status;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

namespace concord {
namespace time {

// Add a sample to the time contract.
Timestamp TimeContract::Update(const string &source, uint16_t client_id,
                               const Timestamp &time,
                               const vector<uint8_t> *signature) {
  LoadLatestSamples();

  auto old_sample = samples_->find(source);
  if (old_sample != samples_->end()) {
    if (!verifier_ ||
        (verifier_->VerifyReceivedUpdate(source, client_id, time, signature))) {
      if (time > old_sample->second.time) {
        LOG4CPLUS_DEBUG(logger_,
                        "Applying time " << time << " from source " << source);
        old_sample->second.time = time;
        if (verifier_ && signature && verifier_->UsesSignatures()) {
          old_sample->second.signature.reset(new vector<uint8_t>(*signature));
        }
        changed_ = true;
      }
    } else {
      LOG4CPLUS_WARN(logger_,
                     "Received a possibly-illegitimate time sample claiming to "
                     "be from source \""
                         << source << "\" that failed time verification.");
    }
  } else {
    LOG4CPLUS_WARN(logger_,
                   "Ignoring sample from uknown source \"" << source << "\"");
  }

  return SummarizeTime();
}

// Get the current time at the latest block (including any updates that have
// been applied since this TimeContract was instantiated).
Timestamp TimeContract::GetTime() {
  LoadLatestSamples();

  return SummarizeTime();
}

Timestamp TimeContract::GetSummarizedTimeAtBlock(BlockId id) const {
  BlockId out_block_id{};
  Sliver raw_time;
  const auto read_status =
      storage_.get(id, summarized_time_key_, raw_time, out_block_id);
  if (!read_status.isOK() || raw_time.empty()) {
    LOG4CPLUS_ERROR(logger_, "Failed to read summarized time from storage");
    throw TimeException{"Failed to read summarized time from storage"};
  }

  Timestamp time;
  if (!time.ParseFromArray(raw_time.data(), raw_time.length())) {
    LOG4CPLUS_ERROR(logger_, "Failed to parse summarized time storage");
    throw TimeException{"Failed to parse summarized time storage"};
  }

  return time;
}

// Combine samples into a single defintion of "now". Samples must have been
// loaded before this function is called.
//
// TODO: refuse to give a summary if there are not enough samples to guarantee
// monotonicity
Timestamp TimeContract::SummarizeTime() {
  assert(samples_);

  if (samples_->empty()) {
    return TimeUtil::GetEpoch();
  }

  vector<uint64_t> times;
  for (const auto &s : *samples_) {
    times.push_back(TimeUtil::TimestampToNanoseconds(s.second.time));
  }

  // middle is either the actual median, or the high side of it for even counts
  // - remember zero indexing!
  //  odd: 1 2 3 4 5 ... 5 / 2 = 2
  //  even: 1 2 3 4 5 6 ... 6 / 2 = 3
  int middle = times.size() / 2;

  // only need to sort the first "half" to find out where the median is
  std::partial_sort(times.begin(), times.begin() + middle + 1, times.end());

  if (times.size() % 2 == 0) {
    uint64_t nanos =
        (*(times.begin() + middle) + *(times.begin() + (middle - 1))) / 2;
    return TimeUtil::NanosecondsToTimestamp(nanos);
  }

  return TimeUtil::NanosecondsToTimestamp(*(times.begin() + middle));
}

// Get the list of samples.
const unordered_map<string, TimeContract::SampleBody>
    &TimeContract::GetSamples() {
  LoadLatestSamples();
  return *samples_;
}

// Find node[*].time_source_id fields in the config.
static bool TimeSourceIdSelector(const ConcordConfiguration &config,
                                 const ConfigurationPath &path, void *state) {
  // isScope: the parameter is inside "node" scope
  // useInstance: we don't care about the template
  return path.isScope && path.useInstance &&
         path.subpath->name == "time_source_id";
}

// Load samples from storage, if they haven't been already.
//
// An exception is thrown if data was found in the time key in storage, but that
// data could not be parsed.
//
// If time signing is enabled, any entries in the storage containing invalid
// signatures will be ignored (with the special exception of entries for a
// recognized source containing both a 0 time and an empty signature, which may
// be used to indicate no samples is yet available for the given source. If the
// sample for a recognized source is rejected, that source's sample for this
// TimeContract will be initialized to the default of time 0. Any sample for an
// unrecognized source will always be completely ignored.
void TimeContract::LoadLatestSamples() {
  if (samples_) {
    // we already loaded the samples; don't load them again, or we could
    // overwrite updates that have been made
    return;
  }

  samples_ = new unordered_map<string, SampleBody>();

  Sliver raw_time;
  Status read_status = storage_.get(time_samples_key_, raw_time);

  if (read_status.isOK() && raw_time.length() > 0) {
    com::vmware::concord::kvb::Time time_storage;
    if (time_storage.ParseFromArray(raw_time.data(), raw_time.length())) {
      if (time_storage.version() == kTimeStorageVersion) {
        LOG4CPLUS_DEBUG(logger_, "Loading " << time_storage.sample_size()
                                            << " time samples");
        if (!verifier_) {
          // This const_cast is ugly. We don't actually change the config
          // values, but the iterator registers itself with the config object,
          // so the reference can't be const.
          ParameterSelection time_source_ids(
              const_cast<ConcordConfiguration &>(config_), TimeSourceIdSelector,
              nullptr);
          unordered_set<string> valid_id_set;
          for (auto id : time_source_ids) {
            valid_id_set.emplace(config_.getValue<string>(id));
          }

          for (int i = 0; i < time_storage.sample_size(); i++) {
            const com::vmware::concord::kvb::Time::Sample &sample =
                time_storage.sample(i);

            if (valid_id_set.count(sample.source()) > 0) {
              samples_->emplace(sample.source(), SampleBody());
              samples_->at(sample.source()).time = sample.time();
            } else {
              LOG4CPLUS_ERROR(
                  logger_,
                  "Time storage contained sample from unrecognized source: \""
                      << sample.source() << "\".");
              throw TimeException(
                  "Cannot load time storage: found time update recorded from "
                  "unrecognized source.");
            }
          }
        } else {  // Case where verifier_ is non-null.
          for (int i = 0; i < time_storage.sample_size(); i++) {
            const com::vmware::concord::kvb::Time::Sample &sample =
                time_storage.sample(i);

            if (verifier_->VerifyRecordedUpdate(sample)) {
              samples_->emplace(sample.source(), SampleBody());
              samples_->at(sample.source()).time = sample.time();
              if (sample.has_signature() && verifier_->UsesSignatures()) {
                samples_->at(sample.source())
                    .signature.reset(new vector<uint8_t>(
                        sample.signature().begin(), sample.signature().end()));
              }
            } else {
              LOG4CPLUS_ERROR(logger_,
                              "Time storage contained invalid time sample "
                              "claimed to be from source: "
                                  << sample.source()
                                  << " (the sample failed time verification).");
              throw TimeException(
                  "Cannot load time storage: a recorded time update failed "
                  "time verification.");
            }
          }
        }
      } else {
        LOG4CPLUS_ERROR(logger_, "Unknown time storage version: "
                                     << time_storage.version());
        throw TimeException("Unknown time storage version");
      }
    } else {
      LOG4CPLUS_ERROR(logger_, "Unable to parse time storage");
      throw TimeException("Unable to parse time storage");
    }
  } else {
    // This const_cast is ugly. We don't actually change the config values, but
    // the iterator registers itself with the config object, so the reference
    // can't be const.
    ParameterSelection time_source_ids(
        const_cast<ConcordConfiguration &>(config_), TimeSourceIdSelector,
        nullptr);
    for (auto id : time_source_ids) {
      LOG4CPLUS_DEBUG(logger_, "source id: " << config_.getValue<string>(id));
      samples_->emplace(config_.getValue<string>(id), SampleBody());
      samples_->at(config_.getValue<string>(id)).time = TimeUtil::GetEpoch();
    }

    LOG4CPLUS_INFO(logger_, "Initializing time contract with "
                                << samples_->size() << " sources");
  }
}

// Prepare a key/value pair for storage.
pair<Sliver, Sliver> TimeContract::Serialize() {
  com::vmware::concord::kvb::Time proto;
  proto.set_version(kTimeStorageVersion);

  for (const auto &s : *samples_) {
    auto sample = proto.add_sample();

    sample->set_source(s.first);
    Timestamp *t = new Timestamp(s.second.time);
    sample->set_allocated_time(t);
    if (verifier_ && verifier_->UsesSignatures()) {
      if (s.second.signature) {
        sample->set_signature(s.second.signature->data(),
                              s.second.signature->size());
      } else {
        LOG4CPLUS_WARN(
            logger_,
            "Serializing sample with no signature in a TimeContract configured "
            "with a time verification scheme that uses signatures.");
      }
    }
  }

  size_t storage_size = proto.ByteSize();
  Sliver time_storage(new char[storage_size], storage_size);
  proto.SerializeToArray(const_cast<char *>(time_storage.data()), storage_size);

  changed_ = false;

  return pair<Sliver, Sliver>(time_samples_key_, time_storage);
}

pair<Sliver, Sliver> TimeContract::SerializeSummarizedTime() {
  const auto current_time = GetTime();
  const auto storage_size = current_time.ByteSize();
  Sliver storage(new char[storage_size], storage_size);
  current_time.SerializeToArray(const_cast<char *>(storage.data()),
                                storage_size);
  return std::make_pair(summarized_time_key_, storage);
}

}  // namespace time
}  // namespace concord
