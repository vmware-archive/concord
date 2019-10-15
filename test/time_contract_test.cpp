// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Test concord::time::TimeContract and related classes.

#include "time/time_contract.hpp"
#include "blockchain/db_adapter.h"
#include "blockchain/db_types.h"
#include "config/configuration_manager.hpp"
#include "gtest/gtest.h"
#include "hash_defs.h"
#include "memorydb/client.h"
#include "memorydb/key_comparator.h"

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <log4cplus/configurator.h>
#include <log4cplus/hierarchy.h>
#include <log4cplus/loggingmacros.h>

using namespace std;

using com::vmware::concord::kvb::Time;
using concord::config::ConcordConfiguration;
using concord::config::ConfigurationPath;
using concord::storage::IDBClient;
using concord::storage::blockchain::IBlocksAppender;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnly;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnlyIterator;
using concord::storage::blockchain::KeyManipulator;
using concord::storage::memorydb::Client;
using concord::storage::memorydb::KeyComparator;
using concord::time::ClientProxyIDTimeVerifier;
using concord::time::RSATimeSigner;
using concord::time::RSATimeVerifier;
using concord::time::TimeContract;
using concordUtils::BlockId;
using concordUtils::Key;
using concordUtils::SetOfKeyValuePairs;
using concordUtils::Sliver;
using concordUtils::Status;
using concordUtils::Value;

using CryptoPP::AutoSeededRandomPool;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

namespace {

// A small shim to prevent having to set up even more framework to create a
// ReplicaImp. (TODO: move the DB interfaces out of ReplicaImp.)
//
// This shim just reads and writes to block zero in an in-memory DB.
class TestStorage : public ILocalKeyValueStorageReadOnly,
                    public IBlocksAppender {
 private:
  KeyComparator comp = KeyComparator(new KeyManipulator());
  Client db_ = Client(comp);

 public:
  Status get(const Key& key, Value& outValue) const override {
    BlockId outBlockId;
    return get(0, key, outValue, outBlockId);
  }

  Status get(BlockId readVersion, const Sliver& key, Sliver& outValue,
             BlockId& outBlock) const override {
    outBlock = 0;
    // KeyManipulator::genDataDbKey is non-const, but this function must be
    // const. Work around by creating a local manipulator.
    KeyManipulator internalManip;
    return db_.get(internalManip.genDataDbKey(key, 0), outValue);
  }

  BlockId getLastBlock() const override { return 0; }

  Status getBlockData(BlockId blockId,
                      SetOfKeyValuePairs& outBlockData) const override {
    EXPECT_TRUE(false) << "Test should not cause getBlockData to be called";
    return Status::IllegalOperation("getBlockData not supported in test");
  }

  Status mayHaveConflictBetween(const Sliver& key, BlockId fromBlock,
                                BlockId toBlock, bool& outRes) const override {
    EXPECT_TRUE(false)
        << "Test should not cause mayHaveConflictBetween to be called";
    return Status::IllegalOperation(
        "mayHaveConflictBetween not supported in test");
  }

  ILocalKeyValueStorageReadOnlyIterator* getSnapIterator() const override {
    EXPECT_TRUE(false) << "Test should not cause getSnapIterator to be called";
    return nullptr;
  }

  Status freeSnapIterator(
      ILocalKeyValueStorageReadOnlyIterator* iter) const override {
    EXPECT_TRUE(false) << "Test should not cause freeSnapIterator to be called";
    return Status::IllegalOperation("freeSnapIterator not supported in test");
  }

  void monitor() const override {
    EXPECT_TRUE(false) << "Test should not cause monitor to be called";
  }

  Status addBlock(const SetOfKeyValuePairs& updates,
                  BlockId& outBlockId) override {
    for (auto u : updates) {
      KeyManipulator internalManip;
      Status status = db_.put(internalManip.genDataDbKey(u.first, 0), u.second);
      if (!status.isOK()) {
        return status;
      }
    }
    outBlockId = 0;
    return Status::OK();
  }
};

static ConcordConfiguration::ParameterStatus NodeScopeSizer(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  *output = ((std::vector<string>*)state)->size();
  return ConcordConfiguration::ParameterStatus::VALID;
}
static ConcordConfiguration::ParameterStatus ReplicaScopeSizer(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  *output = 1;
  return ConcordConfiguration::ParameterStatus::VALID;
}

static const size_t kClientProxiesPerReplica = 4;

static ConcordConfiguration::ParameterStatus ClientProxyScopeSizer(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  *output = kClientProxiesPerReplica;
  return ConcordConfiguration::ParameterStatus::VALID;
}

// The time contract initializes itself with default values for all known
// sources. This function generates a configuration object with the test sources
// named.
ConcordConfiguration TestConfiguration(
    std::vector<string> sourceIDs,
    string time_verification = "rsa-time-signing") {
  // Note that, under the current implementation of this test, if any additional
  // time verification schemes that use signatures are added, it will be
  // necessary to update this condition to make it aware of them.
  bool has_signatures = (time_verification == "rsa-time-signing");

  ConcordConfiguration config;
  config.declareParameter("FEATURE_time_service", "Enable time service");
  config.loadValue("FEATURE_time_service", "true");
  config.declareParameter("time_verification",
                          "Time verification scheme to use.");
  config.loadValue("time_verification", time_verification);

  config.declareScope("node", "Node scope", NodeScopeSizer, &sourceIDs);
  ConcordConfiguration& nodeTemplate = config.subscope("node");
  nodeTemplate.declareScope("replica", "Replica scope", ReplicaScopeSizer,
                            nullptr);
  nodeTemplate.declareScope("client_proxy", "Client proxy scope.",
                            ClientProxyScopeSizer, nullptr);
  nodeTemplate.declareParameter("time_source_id", "Time Source ID");
  ConcordConfiguration& replica_template = nodeTemplate.subscope("replica");
  replica_template.declareParameter("time_source_id", "Time Source ID");
  if (has_signatures) {
    replica_template.declareParameter("private_key", "Private RSA key.");
    replica_template.declareParameter("public_key", "Public RSA key.");
  }
  ConcordConfiguration& client_proxy_template =
      nodeTemplate.subscope("client_proxy");
  client_proxy_template.declareParameter("principal_id",
                                         "ID for this client proxy.");
  config.instantiateScope("node");

  AutoSeededRandomPool random_pool;
  int i = 0;
  for (std::string name : sourceIDs) {
    ConcordConfiguration& nodeScope = config.subscope("node", i);
    nodeScope.instantiateScope("replica");
    ConcordConfiguration& replicaScope = nodeScope.subscope("replica", 0);
    nodeScope.loadValue("time_source_id", name);

    // Generate keys only if we need them for this config, as this generation is
    // non-trivial.
    if (has_signatures) {
      std::pair<std::string, std::string> rsaKeys =
          concord::config::generateRSAKeyPair(random_pool);
      replicaScope.loadValue("private_key", rsaKeys.first);
      replicaScope.loadValue("public_key", rsaKeys.second);
    }
    nodeScope.instantiateScope("client_proxy");
    for (size_t j = 0; j < kClientProxiesPerReplica; ++j) {
      ConcordConfiguration& client_proxy_scope =
          nodeScope.subscope("client_proxy", j);
      client_proxy_scope.loadValue(
          "principal_id",
          to_string(((size_t)i * kClientProxiesPerReplica) + j));
    }

    i++;
  }

  return config;
}

uint16_t GetSomeClientIDFromNode(size_t node) {
  return (uint16_t)(node * kClientProxiesPerReplica);
}

// Since we're using "median", and odd number of sources gives the most direct,
// obvious answer.
TEST(time_contract_test, five_source_happy_path) {
  TestStorage database;
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C", "D", "E"}, "none");
  TimeContract tc(database, config);

  std::vector<std::pair<std::string, Timestamp>> samples = {
      {"A", TimeUtil::SecondsToTimestamp(1)},
      {"B", TimeUtil::SecondsToTimestamp(2)},
      {"C", TimeUtil::SecondsToTimestamp(3)},
      {"D", TimeUtil::SecondsToTimestamp(4)},
      {"E", TimeUtil::SecondsToTimestamp(5)}};

  for (size_t i = 0; i < samples.size(); ++i) {
    pair<string, Timestamp> s = samples[i];
    tc.Update(s.first, GetSomeClientIDFromNode(i), s.second);
  }

  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(3));
}

// Since we're using "median", verify that an even number of sources gives the
// answer between the middle two.
TEST(time_contract_test, six_source_happy_path) {
  TestStorage database;
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C", "D", "E", "F"}, "none");
  TimeContract tc(database, config);

  std::vector<std::pair<std::string, Timestamp>> samples = {
      {"A", TimeUtil::SecondsToTimestamp(1)},
      {"B", TimeUtil::SecondsToTimestamp(2)},
      {"C", TimeUtil::SecondsToTimestamp(3)},
      {"D", TimeUtil::SecondsToTimestamp(5)},
      {"E", TimeUtil::SecondsToTimestamp(6)},
      {"F", TimeUtil::SecondsToTimestamp(7)}};

  for (size_t i = 0; i < samples.size(); ++i) {
    pair<string, Timestamp> s = samples[i];
    tc.Update(s.first, GetSomeClientIDFromNode(i), s.second);
  }

  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(4));
}

// Verify that a single source moves forward as expected
TEST(time_contract_test, source_moves_forward) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"baz"}, "none");

  std::string source_id = "baz";

  for (uint64_t fake_time = 1; fake_time < 10; fake_time++) {
    TimeContract tc(database, config);
    Timestamp fake_timestamp = TimeUtil::SecondsToTimestamp(fake_time);
    ASSERT_EQ(tc.Update(source_id, GetSomeClientIDFromNode(0), fake_timestamp),
              TimeUtil::SecondsToTimestamp(fake_time));
  }
}

// Verify that time is saved and restored correctly
TEST(time_contract_test, save_restore) {
  // Note this test is performed with both a time contract without signing
  // enabled and one with it, as the database contents differ in these cases
  // because the signatures are saved when signing is enabled.
  TestStorage database;
  TestStorage database_with_signatures;
  ConcordConfiguration config =
      TestConfiguration({"foo", "bar", "baz", "qux"}, "none");
  ConcordConfiguration signing_config =
      TestConfiguration({"foo", "bar", "baz", "qux"}, "rsa-time-signing");

  std::string source_foo = "foo";
  uint16_t client_foo = GetSomeClientIDFromNode(0);
  RSATimeSigner ts_foo(signing_config.subscope("node", 0));
  std::string source_bar = "bar";
  uint16_t client_bar = GetSomeClientIDFromNode(1);
  RSATimeSigner ts_bar(signing_config.subscope("node", 1));
  std::string source_baz = "baz";
  uint16_t client_baz = GetSomeClientIDFromNode(2);
  RSATimeSigner ts_baz(signing_config.subscope("node", 2));
  std::string source_qux = "qux";
  uint16_t client_qux = GetSomeClientIDFromNode(3);
  RSATimeSigner ts_qux(signing_config.subscope("node", 3));

  Timestamp expected_time;
  {
    TimeContract tc(database, config);
    TimeContract signing_tc(database_with_signatures, signing_config);
    vector<uint8_t> signature;
    Timestamp t1 = TimeUtil::SecondsToTimestamp(12345);
    signature = ts_foo.Sign(t1);
    tc.Update(source_foo, client_foo, t1);
    signing_tc.Update(source_foo, client_foo, t1, &signature);
    Timestamp t2 = TimeUtil::SecondsToTimestamp(54321);
    signature = ts_bar.Sign(t2);
    tc.Update(source_bar, client_bar, t2);
    signing_tc.Update(source_bar, client_bar, t2, &signature);
    Timestamp t3 = TimeUtil::SecondsToTimestamp(10293);
    signature = ts_baz.Sign(t3);
    tc.Update(source_baz, client_baz, t3);
    signing_tc.Update(source_baz, client_baz, t3, &signature);
    Timestamp t4 = TimeUtil::SecondsToTimestamp(48576);
    signature = ts_qux.Sign(t4);
    tc.Update(source_qux, client_qux, t4);
    signing_tc.Update(source_qux, client_qux, t4, &signature);
    expected_time = tc.GetTime();

    SetOfKeyValuePairs updates({tc.Serialize()});
    SetOfKeyValuePairs signed_updates({signing_tc.Serialize()});
    BlockId block_id;
    Status result = database.addBlock(updates, block_id);
    ASSERT_EQ(result.isOK(), true);
    result = database_with_signatures.addBlock(signed_updates, block_id);
    ASSERT_EQ(result.isOK(), true);
  }

  // It's not actually necessary to push tc out of scope, since a new
  // TimeContract object would reinitialize itself from storage anyway, but
  // we've done so for completeness, and now we get to reuse the name anyway.

  TimeContract tc(database, config);
  ASSERT_EQ(tc.GetTime(), expected_time);
  TimeContract signing_tc(database_with_signatures, signing_config);
  ASSERT_EQ(signing_tc.GetTime(), expected_time);

  // Validate the signatures are saved if and only if the exist.
  for (const auto& sample : tc.GetSamples()) {
    ASSERT_FALSE(sample.second.signature)
        << "Signature persisted by a time contract without time signing "
           "enabled.";
  }
  for (const auto& sample : signing_tc.GetSamples()) {
    ASSERT_TRUE(sample.second.signature)
        << "Signature failed to be persisted by a time contract with time "
           "signing enabled.";
  }
}

// Verify that the correct source is updated.
TEST(time_contract_test, update_correct_source) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B", "C"}, "none");

  // The idea here is to exploit the fact that the median of a three-reading
  // system will always be equal to one of those three reading. So, by asserting
  // that the resulting summary is equal to a particular reading, we can assert
  // that it was that reading that changed.

  std::string source_A = "A";
  uint16_t client_A = GetSomeClientIDFromNode(0);
  std::string source_B = "B";
  uint16_t client_B = GetSomeClientIDFromNode(1);
  std::string source_C = "C";
  uint16_t client_C = GetSomeClientIDFromNode(2);

  TimeContract tc(database, config);
  Timestamp t1 = TimeUtil::SecondsToTimestamp(1);
  tc.Update(source_A, client_A, t1);
  Timestamp t2 = TimeUtil::SecondsToTimestamp(10);
  tc.Update(source_B, client_B, t2);
  Timestamp t3 = TimeUtil::SecondsToTimestamp(20);
  tc.Update(source_C, client_C, t3);

  // sanity: B is the median
  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(10));

  // directly observe the median reading (B) being updated
  Timestamp t4 = TimeUtil::SecondsToTimestamp(11);
  ASSERT_EQ(tc.Update(source_B, client_B, t4),
            TimeUtil::SecondsToTimestamp(11));

  // first move one of the other values, then make it the median
  Timestamp t5 = TimeUtil::SecondsToTimestamp(21);
  ASSERT_EQ(tc.Update(source_C, client_C, t5),
            TimeUtil::SecondsToTimestamp(11));
  // either A or B moved, because the new summary is C's value
  Timestamp t6 = TimeUtil::SecondsToTimestamp(30);
  ASSERT_EQ(tc.Update(source_A, client_A, t6),
            TimeUtil::SecondsToTimestamp(21));

  // and one more leapfrog, either B or C moved, because the summary is A
  Timestamp t8 = TimeUtil::SecondsToTimestamp(40);
  ASSERT_EQ(tc.Update(source_B, client_B, t8),
            TimeUtil::SecondsToTimestamp(30));
}

// Verify that a source cannot move its own time backward.
TEST(time_contract_test, prevent_source_rollback) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"foo"}, "none");

  std::string source_foo = "foo";
  uint16_t client_foo = GetSomeClientIDFromNode(0);

  TimeContract tc1(database, config);
  Timestamp t1 = TimeUtil::SecondsToTimestamp(1000);
  const Timestamp first_time = tc1.Update(source_foo, client_foo, t1);

  // first make sure a source can't rollback a cached copy
  Timestamp t2 = TimeUtil::SecondsToTimestamp(500);
  const Timestamp second_time = tc1.Update(source_foo, client_foo, t2);
  ASSERT_EQ(second_time, first_time);

  // then make sure a fresh read is also protected
  SetOfKeyValuePairs updates({tc1.Serialize()});
  BlockId block_id;
  Status result = database.addBlock(updates, block_id);
  ASSERT_EQ(result.isOK(), true);

  TimeContract tc2(database, config);
  Timestamp t3 = TimeUtil::SecondsToTimestamp(250);
  const Timestamp third_time = tc2.Update(source_foo, client_foo, t3);
  ASSERT_EQ(third_time, first_time);
}

// Only accept times from preconfigured sources.
TEST(time_contract_test, ignore_unknown_source) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B", "C"}, "none");
  ConcordConfiguration fakeConfig = TestConfiguration({"X", "Y", "Z"}, "none");

  TimeContract tc1(database, config);
  Timestamp timestamp = TimeUtil::SecondsToTimestamp(1000);
  tc1.Update("X", GetSomeClientIDFromNode(0), timestamp);
  tc1.Update("Y", GetSomeClientIDFromNode(1), timestamp);
  tc1.Update("Z", GetSomeClientIDFromNode(2), timestamp);

  // Config specified A,B,C as sources, so all of X,Y,Z updates should be
  // ignored.
  ASSERT_EQ(tc1.GetTime(), TimeUtil::GetEpoch());
}

// Verify general functionality of RSATimeSigner and RSATimeVerifier and their
// compliance with their claimed interface(s).
TEST(time_contract_test, rsa_time_signing_basic_interfaces) {
  ConcordConfiguration empty_config = TestConfiguration({}, "rsa-time-signing");
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C"}, "rsa-time-signing");
  ConcordConfiguration& a_config = config.subscope("node", 0);
  ConcordConfiguration& b_config = config.subscope("node", 1);
  ConcordConfiguration& c_config = config.subscope("node", 2);
  Timestamp arbitrary_time = TimeUtil::SecondsToTimestamp(10000000000);

  RSATimeSigner a_signer(a_config);

  EXPECT_THROW(RSATimeSigner config_signer(config), invalid_argument)
      << "RSATimeSigner's constructor fails to reject an inappropriate "
         "configuration.";

  b_config.eraseValue("time_source_id");
  EXPECT_THROW(RSATimeSigner b_signer(b_config), invalid_argument)
      << "RSATimeSigner's constructor fails to reject configuration for a node "
         "with no time source ID.";

  c_config.eraseAllValues();
  c_config.loadValue("time_source_id", "C");
  EXPECT_THROW(RSATimeSigner c_signer(c_config), invalid_argument)
      << "RSATimeSigner's constructor fails to reject configuration for a node "
         "with no private key.";

  vector<uint8_t> a_signature = a_signer.Sign(arbitrary_time);
  EXPECT_GT(a_signature.size(), 0)
      << "RSATimeSigner fails to produce a signature.";

  RSATimeSigner a_signer_copy(a_signer);
  vector<uint8_t> a_signature_copy = a_signer_copy.Sign(arbitrary_time);
  EXPECT_EQ(a_signature, a_signature_copy)
      << "RSATimeSigner's copy constructor does not work.";
  a_signer_copy = a_signer;
  a_signature_copy = a_signer_copy.Sign(arbitrary_time);
  EXPECT_EQ(a_signature, a_signature_copy)
      << "RSATimeSigner's copy assignment operator does not work.";

  EXPECT_THROW(RSATimeVerifier verifier(config), invalid_argument)
      << "RSATimeVerifier's constructor fails to reject a configuration "
         "lacking a public key for an identified time source.";
  c_config.eraseAllValues();

  RSATimeVerifier verifier(config);
  RSATimeVerifier empty_verifier(empty_config);

  EXPECT_TRUE(verifier.UsesSignatures())
      << "RSATimeVerifier fails to acknowledge that it uses signatures.";

  EXPECT_TRUE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(0),
                                            arbitrary_time, &a_signature))
      << "RSATimeVerifier fails to verify a legitimate signature.";

  Time::Sample a_record;
  a_record.set_source("A");
  a_record.set_allocated_time(new Timestamp(arbitrary_time));
  a_record.set_signature(a_signature.data(), a_signature.size());

  EXPECT_TRUE(verifier.VerifyRecordedUpdate(a_record))
      << "RSATimeVerifier fails to verify a legitimate time record from the "
         "storage format.";

  EXPECT_FALSE(empty_verifier.VerifyReceivedUpdate(
      "A", GetSomeClientIDFromNode(0), arbitrary_time, &a_signature))
      << "RSATimeVerifier constructed from an empty configuration fails to "
         "reject a signature.";

  RSATimeVerifier verifier_copy(verifier);
  EXPECT_TRUE(verifier_copy.VerifyReceivedUpdate(
      "A", GetSomeClientIDFromNode(0), arbitrary_time, &a_signature))
      << "RSATimeVerifier's copy constructor does not work correctly.";
  verifier_copy = verifier;
  EXPECT_TRUE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(0),
                                            arbitrary_time, &a_signature))
      << "RSATimeVerifier's copy assignment operator does not work correctly.";

  b_config.loadValue("time_source_id", "B");
  RSATimeSigner b_signer(b_config);
  vector<uint8_t> b_signature = b_signer.Sign(arbitrary_time);
  b_config.eraseValue("time_source_id");
  EXPECT_FALSE(verifier.VerifyReceivedUpdate("B", GetSomeClientIDFromNode(1),
                                             arbitrary_time, &b_signature))
      << "RSATimeVerifier fails to reject a signature from an unrecognized "
         "source.";

  Timestamp arbitrary_time_plus_one = TimeUtil::SecondsToTimestamp(
      TimeUtil::TimestampToSeconds(arbitrary_time) + 1);
  EXPECT_FALSE(verifier.VerifyReceivedUpdate(
      "A", GetSomeClientIDFromNode(0), arbitrary_time_plus_one, &a_signature))
      << "RSATimeVerifier fails to reject a signature that doesn't match the "
         "claimed source/time combination.";

  a_record.set_allocated_time(new Timestamp(arbitrary_time_plus_one));
  EXPECT_FALSE(verifier.VerifyRecordedUpdate(a_record))
      << "RSATimeVerifier fails to reject a time reord in the storage format "
         "with a signature that doesn't match the claimed source/time "
         "combination.";
  a_record.clear_signature();
  EXPECT_FALSE(verifier.VerifyRecordedUpdate(a_record))
      << "RSATimeVerifier fails to reject a time record in the storage format "
         "with no recorded signature.";
  a_record.set_allocated_time(new Timestamp(TimeUtil::GetEpoch()));
  EXPECT_TRUE(verifier.VerifyRecordedUpdate(a_record))
      << "RSATimeVerifier fails to accept a time record in the storage format "
         "representing that no time sample has been received for the given "
         "source.";

  vector<uint8_t> empty_signature;
  EXPECT_FALSE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(0),
                                             arbitrary_time, &empty_signature))
      << "RSATimeVerifier fails to correctly handle being given an empty "
         "signature.";
}

// Verify general functionality of ClientProxyIDTimeVerifierand its compliance
// with its claimed interface(s).
TEST(time_contract_test, client_proxy_id_verification_basic_interfaces) {
  ConcordConfiguration empty_config =
      TestConfiguration({}, "bft-client-proxy-id");
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C"}, "bft-client-proxy-id");
  Timestamp arbitrary_time = TimeUtil::SecondsToTimestamp(10000000000);

  ClientProxyIDTimeVerifier verifier(config);
  ClientProxyIDTimeVerifier empty_verifier(empty_config);

  EXPECT_FALSE(verifier.UsesSignatures())
      << "ClientProxyIDTimeVerifier fails to acknowledge that it does not use "
         "signatures.";

  EXPECT_TRUE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(0),
                                            arbitrary_time))
      << "ClientProxyIDTimeVerifier fails to verify a time update for a "
         "legitimate source.";

  Time::Sample a_record;
  a_record.set_source("A");
  a_record.set_allocated_time(new Timestamp(arbitrary_time));

  EXPECT_TRUE(verifier.VerifyRecordedUpdate(a_record))
      << "ClientProxyIDTimeVerifie fails to verify a time record from the "
         "storage format.";

  EXPECT_FALSE(empty_verifier.VerifyReceivedUpdate(
      "A", GetSomeClientIDFromNode(0), arbitrary_time))
      << "ClientProxyIDTimeVerifier constructed from an empty configuration "
         "fails to reject a time update.";

  ClientProxyIDTimeVerifier verifier_copy(verifier);
  EXPECT_TRUE(verifier_copy.VerifyReceivedUpdate(
      "A", GetSomeClientIDFromNode(0), arbitrary_time))
      << "ClientProxyIDTimeVerifier's copy constructor does not work "
         "correctly.";
  verifier_copy = verifier;
  EXPECT_TRUE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(0),
                                            arbitrary_time))
      << "ClientProxyIDTimeVerifier's copy assignment operator does not work "
         "correctly.";

  EXPECT_FALSE(verifier.VerifyReceivedUpdate("D", GetSomeClientIDFromNode(3),
                                             arbitrary_time))
      << "ClientProxyIDTimeVerifier fails to reject an update from an "
         "unrecognized source.";

  EXPECT_FALSE(verifier.VerifyReceivedUpdate("A", GetSomeClientIDFromNode(1),
                                             arbitrary_time))
      << "ClientProxyIDTimeVerifier fails to reject an update from a client "
         "proxy with ID that does not match the time source.";

  Time::Sample d_record(a_record);
  d_record.set_source("D");
  EXPECT_FALSE(verifier.VerifyRecordedUpdate(d_record))
      << "ClientProxyIDTimeVerifier fails to reject a time record from the "
         "storage format with an unrecognized source.";
}

// Verify RSATimeSigner and RSATimeVerifier are compatible.
TEST(time_contract_test, time_signature_verifiability) {
  ConcordConfiguration config = TestConfiguration(
      {"A", "B", "C", "D", "E", "F", "G", "H"}, "rsa-time-signing");
  Timestamp arbitrary_time = TimeUtil::SecondsToTimestamp(1000000000);
  RSATimeVerifier verifier(config);

  for (size_t i = 0; i < config.scopeSize("node"); ++i) {
    RSATimeSigner signer(config.subscope("node", i));
    vector<uint8_t> signature = signer.Sign(arbitrary_time);
    for (size_t j = 0; j < config.scopeSize("node"); j++) {
      string claimed_signer =
          config.subscope("node", j).getValue<string>("time_source_id");
      EXPECT_EQ((i == j), verifier.VerifyReceivedUpdate(
                              claimed_signer, GetSomeClientIDFromNode(i),
                              arbitrary_time, &signature))
          << "RSATimeVerifier constructed with public keys fails to correctly "
             "validate time sample signatures from RSATimeSigners constructed "
             "with corresponding private keys.";
    }
  }
}

// Verify TimeContract enforces the verdicts of a TimeVerifier if non-none
// time_verification is configured.
TEST(time_contract_test, time_verification_enforcement) {
  // It is suspected it may be theoretically possible for this test case to fail
  // randomly as a result of duplicate cryptographic keys being generated,
  // however, the probability of such failures is assumed to be negligible,
  // barring misconfiguration of the the randomness source.

  TestStorage rsa_database;
  TestStorage id_database;
  ConcordConfiguration rsa_config =
      TestConfiguration({"A", "B"}, "rsa-time-signing");
  ConcordConfiguration fake_config =
      TestConfiguration({"A", "B"}, "rsa-time-signing");
  ConcordConfiguration id_config =
      TestConfiguration({"A", "B"}, "bft-client-proxy-id");
  TimeContract rsa_tc(rsa_database, rsa_config);
  TimeContract id_tc(id_database, id_config);
  RSATimeSigner a_signer(rsa_config.subscope("node", 0));
  RSATimeSigner b_signer(rsa_config.subscope("node", 1));
  RSATimeSigner fake_a_signer(fake_config.subscope("node", 0));
  RSATimeSigner fake_b_signer(fake_config.subscope("node", 1));

  Timestamp t1 = TimeUtil::SecondsToTimestamp(3);
  vector<uint8_t> a_signature = a_signer.Sign(t1);
  rsa_tc.Update("A", GetSomeClientIDFromNode(0), t1, &a_signature);
  vector<uint8_t> b_signature = b_signer.Sign(t1);
  rsa_tc.Update("B", GetSomeClientIDFromNode(1), t1, &b_signature);

  Timestamp t2 = TimeUtil::SecondsToTimestamp(17);
  vector<uint8_t> empty_signature;
  rsa_tc.Update("A", GetSomeClientIDFromNode(0), t2, &empty_signature);
  rsa_tc.Update("B", GetSomeClientIDFromNode(1), t2, &empty_signature);

  EXPECT_EQ(rsa_tc.GetTime(), t1)
      << "Time Contract with RSA time signing enabled fails to reject time "
         "updates without signatures.";

  a_signature = a_signer.Sign(t2);
  rsa_tc.Update("A", GetSomeClientIDFromNode(0), t2, &b_signature);
  b_signature = b_signer.Sign(t2);
  rsa_tc.Update("B", GetSomeClientIDFromNode(1), t2, &a_signature);

  EXPECT_EQ(rsa_tc.GetTime(), t1)
      << "Time Contract with RSA time signing enabled fails to reject time "
         "updates with incorrect signatures.";

  Timestamp t3 = TimeUtil::SecondsToTimestamp(21);
  rsa_tc.Update("A", GetSomeClientIDFromNode(0), t3, &a_signature);
  rsa_tc.Update("B", GetSomeClientIDFromNode(1), t3, &b_signature);
  a_signature = fake_a_signer.Sign(t3);
  rsa_tc.Update("A", GetSomeClientIDFromNode(0), t3, &a_signature);
  b_signature = fake_b_signer.Sign(t3);
  rsa_tc.Update("B", GetSomeClientIDFromNode(1), t3, &b_signature);

  EXPECT_EQ(rsa_tc.GetTime(), t1)
      << "Time Contract with RSA time signing enabled fails to reject time "
         "updates with invalid signatures.";

  id_tc.Update("A", GetSomeClientIDFromNode(0), t1);
  id_tc.Update("B", GetSomeClientIDFromNode(1), t1);

  id_tc.Update("A", GetSomeClientIDFromNode(1), t2);
  id_tc.Update("B", GetSomeClientIDFromNode(0), t2);

  EXPECT_EQ(id_tc.GetTime(), t1)
      << "Time Contract with client proxy ID-based time verification enabled "
         "fails to reject time updates from client proxies that do not match "
         "the time source.";
}

}  // end namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  log4cplus::initialize();
  log4cplus::Hierarchy& hierarchy = log4cplus::Logger::getDefaultHierarchy();
  hierarchy.disableDebug();
  log4cplus::BasicConfigurator config(hierarchy, false);
  config.configure();
  return RUN_ALL_TESTS();
}
