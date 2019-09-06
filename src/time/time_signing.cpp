// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "time_signing.hpp"
#include "time_exception.hpp"

#include <google/protobuf/util/time_util.h>

using std::invalid_argument;
using std::string;
using std::unique_ptr;
using std::vector;

using bftEngine::impl::RSASigner;
using bftEngine::impl::RSAVerifier;
using concord::config::ConcordConfiguration;
using concord::config::ConfigurationPath;

using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

namespace concord {
namespace time {

// Note we expect GetSignableUpdateData's implementation to produce the same
// series of bytes given the same time source ID and time, independent of the
// platform it is compiled for (note this includes independence of the system's
// endianness) and run on to the greatest extent possible, as if this function
// does not yield the same result on every node in the Concord cluster, the
// nodes will not be able to accept each other's time samples.
//
// The implementation of GetSignableUpdateData gives a concatenation of the
// bytes of the string source (in order, excluding any null terminator
// character) and the bytes of the time value (in little-endian order).
vector<uint8_t> GetSignableUpdateData(const string& source,
                                      const Timestamp& timestamp) {
  // Note std::string is defined to be std::basic_string<char>, that is, it is
  // generally treated as and operated on like an array or list of chars. Some
  // online references on std::string that were consulted in writing this
  // function document that std::string does not itself consider or handle
  // multi-char or variable-length character encodings; it just thinks of itself
  // as a list of char-type integers. Therefore, this function does not itself
  // have to consider the underlying character encoding, though this function
  // does (at this time) make the assumption that the same encoding (or at least
  // compatible encodings such as ASCII and UTF-8 using only ASCII characters)
  // is used on all nodes and time sources in this concord cluster.

  // Note that, another conceivable way in which the same string could have
  // different bytes on different machines is a case where the same character
  // encoding is used on every machine, but char has a different width on
  // different machines. This is probably unlikely, given that chars are often
  // assumed to be equivalent to bytes in C++, but, at the time of this writing,
  // the implementer of this function could not definitively verify that C++
  // does not permit char to be wider than a byte like it permits other integer
  // to be of greater width. Since this case is believed to be unlikely, we have
  // chosen simply not to support systems with unusually wide chars at this
  // time.
  static_assert(sizeof(char) == sizeof(uint8_t),
                "Current Concord time service implementation does not support "
                "platforms where char is not 1 byte.");

  uint64_t time = TimeUtil::TimestampToNanoseconds(timestamp);
  vector<uint8_t> signable_data(source.length() + sizeof(uint64_t));

  const uint8_t* source_first_byte =
      reinterpret_cast<const uint8_t*>(source.data());
  const uint8_t* source_last_byte = source_first_byte + source.length();
  const uint8_t* time_first_byte = reinterpret_cast<const uint8_t*>(&time);
  const uint8_t* time_last_byte = time_first_byte + sizeof(time);
  uint8_t* current_data = signable_data.data();

  std::copy(source_first_byte, source_last_byte, current_data);
  current_data += source.length();
#ifdef BOOST_LITTLE_ENDIAN
  std::copy(time_first_byte, time_last_byte, current_data);
#else   // BOOST_LITTLE_ENDIAN not defined in this case
  std::reverse_copy(time_first_byte, time_last_byte, current_data);
#endif  // if BOOST_LITTLE_ENDIAN defined/ else

  return signable_data;
}

TimeSigner::TimeSigner(const ConcordConfiguration& node_config) {
  if (!node_config.hasValue<string>("time_source_id")) {
    throw invalid_argument(
        "Cannot construct TimeSigner for given node configuration: "
        "time_source_id not found.");
  }

  ConfigurationPath private_key_path("replica", size_t{0});
  private_key_path.subpath.reset(new ConfigurationPath("private_key"));
  if (!node_config.hasValue<string>(private_key_path)) {
    throw invalid_argument(
        "Cannot construct TimeSigner for given node configuration: private_key "
        "not found for this time source.");
  }

  sourceID_ = node_config.getValue<string>("time_source_id");
  private_key_ = node_config.getValue<string>(private_key_path);
  signer_.reset(new RSASigner(private_key_.c_str()));
}

TimeSigner::TimeSigner(const TimeSigner& original)
    : sourceID_(original.sourceID_),
      signer_(new RSASigner(original.private_key_.c_str())),
      private_key_(original.private_key_.c_str()) {}

TimeSigner& TimeSigner::operator=(const TimeSigner& original) {
  sourceID_ = original.sourceID_;
  signer_.reset(new RSASigner(original.private_key_.c_str()));
  private_key_ = original.private_key_;
  return *this;
}

vector<uint8_t> TimeSigner::Sign(const Timestamp& time) {
  vector<uint8_t> data_to_sign = GetSignableUpdateData(sourceID_, time);
  vector<uint8_t> signature(signer_->signatureLength(), 0);
  size_t signature_size = 0;

  bool sig_made;
  {
    std::lock_guard<std::mutex> lock(sign_mutex_);
    sig_made = signer_->sign(reinterpret_cast<const char*>(data_to_sign.data()),
                             data_to_sign.size(),
                             reinterpret_cast<char*>(signature.data()),
                             signature.size(), signature_size);
  }

  if (!sig_made) {
    throw TimeException(
        "TimeSigner unexpectedly failed to sign a time update for source \"" +
        sourceID_ + "\".");
  } else if (signature_size < signature.size()) {
    signature.resize(signature_size);
  }
  return signature;
}

TimeVerifier::TimeVerifier(const ConcordConfiguration& config)
    : verifiers_(), public_keys_() {
  if (!config.scopeIsInstantiated("node")) {
    throw invalid_argument(
        "Cannot construct TimeVerifier for given configuration: cannot find "
        "instantiated node scope.");
  }
  for (size_t i = 0; i < config.scopeSize("node"); ++i) {
    const ConcordConfiguration& node_config = config.subscope("node", i);
    if (node_config.hasValue<string>("time_source_id")) {
      string source_id = node_config.getValue<string>("time_source_id");
      ConfigurationPath public_key_path("replica", size_t{0});
      public_key_path.subpath.reset(new ConfigurationPath("public_key"));
      if (!node_config.hasValue<string>(public_key_path)) {
        throw invalid_argument(
            "Cannot construct TimeVerifier for given configuration: cannot "
            "find public key for time source \"" +
            source_id + "\"");
      }

      std::string public_key = node_config.getValue<string>(public_key_path);

      verifiers_.emplace(source_id, unique_ptr<RSAVerifier>(
                                        new RSAVerifier(public_key.c_str())));
      public_keys_.emplace(source_id, public_key);
    }
  }
}

TimeVerifier::TimeVerifier(const TimeVerifier& original)
    : verifiers_(), public_keys_() {
  for (auto time_source : original.public_keys_) {
    verifiers_.emplace(
        time_source.first,
        unique_ptr<RSAVerifier>(new RSAVerifier(time_source.second.c_str())));
    public_keys_.emplace(time_source.first, time_source.second);
  }
}

TimeVerifier::~TimeVerifier() {}

TimeVerifier& TimeVerifier::operator=(const TimeVerifier& original) {
  verifiers_.clear();
  public_keys_.clear();
  for (auto time_source : original.public_keys_) {
    verifiers_.emplace(
        time_source.first,
        unique_ptr<RSAVerifier>(new RSAVerifier(time_source.second.c_str())));
    public_keys_.emplace(time_source.first, time_source.second);
  }
  return *this;
}

bool TimeVerifier::HasTimeSource(const string& source) const {
  return (verifiers_.count(source) > 0);
}

bool TimeVerifier::Verify(const string& source, const Timestamp& time,
                          const vector<uint8_t>& signature) {
  if (verifiers_.count(source) < 1) {
    return false;
  }

  vector<uint8_t> expected_signed_data = GetSignableUpdateData(source, time);
  return verifiers_.at(source)->verify(
      reinterpret_cast<char*>(expected_signed_data.data()),
      expected_signed_data.size(),
      reinterpret_cast<const char*>(signature.data()), signature.size());
}

}  // namespace time
}  // namespace concord
