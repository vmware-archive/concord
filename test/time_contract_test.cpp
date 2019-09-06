// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Test concord::time::TimeContract and related classes.

#define USE_ROCKSDB
#include "time/time_contract.hpp"
#include "config/configuration_manager.hpp"
#include "consensus/hash_defs.h"
#include "consensus/status.hpp"
#include "gtest/gtest.h"
#include "storage/blockchain_db_adapter.h"
#include "storage/blockchain_db_types.h"
#include "storage/comparators.h"
#include "storage/in_memory_db_client.h"

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <log4cplus/configurator.h>
#include <log4cplus/hierarchy.h>
#include <log4cplus/loggingmacros.h>

using namespace std;

using concord::config::ConcordConfiguration;
using concord::config::ConfigurationPath;
using concord::consensus::Sliver;
using concord::consensus::Status;
using concord::storage::BlockId;
using concord::storage::IBlocksAppender;
using concord::storage::IDBClient;
using concord::storage::ILocalKeyValueStorageReadOnly;
using concord::storage::ILocalKeyValueStorageReadOnlyIterator;
using concord::storage::InMemoryDBClient;
using concord::storage::Key;
using concord::storage::KeyManipulator;
using concord::storage::RocksKeyComparator;
using concord::storage::SetOfKeyValuePairs;
using concord::storage::Value;
using concord::time::TimeContract;
using concord::time::TimeSigner;
using concord::time::TimeVerifier;

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
  InMemoryDBClient db_ = InMemoryDBClient(
      (IDBClient::KeyComparator)&RocksKeyComparator::InMemKeyComp);

 public:
  Status get(Key key, Value& outValue) const override {
    BlockId outBlockId;
    return get(0, key, outValue, outBlockId);
  }

  Status get(BlockId readVersion, Sliver key, Sliver& outValue,
             BlockId& outBlock) const override {
    outBlock = 0;
    return db_.get(KeyManipulator::genDataDbKey(key, 0), outValue);
  }

  BlockId getLastBlock() const override { return 0; }

  Status getBlockData(BlockId blockId,
                      SetOfKeyValuePairs& outBlockData) const override {
    EXPECT_TRUE(false) << "Test should not cause getBlockData to be called";
    return Status::IllegalOperation("getBlockData not supported in test");
  }

  Status mayHaveConflictBetween(Sliver key, BlockId fromBlock, BlockId toBlock,
                                bool& outRes) const override {
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
      Status status =
          db_.put(KeyManipulator::genDataDbKey(u.first, 0), u.second);
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

// The time contract initializes itself with default values for all known
// sources. This function generates a configuration object with the test sources
// named.
ConcordConfiguration TestConfiguration(std::vector<string> sourceIDs) {
  ConcordConfiguration config;
  config.declareScope("node", "Node scope", NodeScopeSizer, &sourceIDs);
  ConcordConfiguration& nodeTemplate = config.subscope("node");
  nodeTemplate.declareScope("replica", "Replica scope", ReplicaScopeSizer,
                            nullptr);
  config.instantiateScope("node");

  AutoSeededRandomPool random_pool;
  int i = 0;
  for (std::string name : sourceIDs) {
    ConcordConfiguration& nodeScope = config.subscope("node", i);
    nodeScope.instantiateScope("replica");
    ConcordConfiguration& replicaScope = nodeScope.subscope("replica", 0);
    nodeScope.declareParameter("time_source_id", "Time Source ID");
    nodeScope.loadValue("time_source_id", name);

    std::pair<std::string, std::string> rsaKeys =
        concord::config::generateRSAKeyPair(random_pool);
    replicaScope.declareParameter("private_key", "Private RSA key.");
    replicaScope.declareParameter("public_key", "Public RSA key.");
    replicaScope.loadValue("private_key", rsaKeys.first);
    replicaScope.loadValue("public_key", rsaKeys.second);

    i++;
  }

  return config;
}

// Since we're using "median", and odd number of sources gives the most direct,
// obvious answer.
TEST(time_contract_test, five_source_happy_path) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B", "C", "D", "E"});
  TimeContract tc(database, config);

  std::vector<std::pair<std::string, Timestamp>> samples = {
      {"A", TimeUtil::SecondsToTimestamp(1)},
      {"B", TimeUtil::SecondsToTimestamp(2)},
      {"C", TimeUtil::SecondsToTimestamp(3)},
      {"D", TimeUtil::SecondsToTimestamp(4)},
      {"E", TimeUtil::SecondsToTimestamp(5)}};

  for (size_t i = 0; i < samples.size(); ++i) {
    pair<string, Timestamp> s = samples[i];
    const ConcordConfiguration& node_config = config.subscope("node", i);

    TimeSigner ts(node_config);
    vector<uint8_t> signature = ts.Sign(s.second);
    tc.Update(s.first, s.second, signature);
    tc.Update(s.first, s.second, signature);
  }

  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(3));
}

// Since we're using "median", verify that an even number of sources gives the
// answer between the middle two.
TEST(time_contract_test, six_source_happy_path) {
  TestStorage database;
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C", "D", "E", "F"});
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
    const ConcordConfiguration& node_config = config.subscope("node", i);

    TimeSigner ts(node_config);
    vector<uint8_t> signature = ts.Sign(s.second);
    tc.Update(s.first, s.second, signature);
  }

  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(4));
}

// Verify that a single source moves forward as expected
TEST(time_contract_test, source_moves_forward) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"baz"});
  const ConcordConfiguration& node_config = config.subscope("node", 0);
  TimeSigner ts(node_config);

  std::string source_id = "baz";

  for (uint64_t fake_time = 1; fake_time < 10; fake_time++) {
    TimeContract tc(database, config);
    Timestamp fake_timestamp = TimeUtil::SecondsToTimestamp(fake_time);
    vector<uint8_t> signature = ts.Sign(fake_timestamp);
    ASSERT_EQ(tc.Update(source_id, fake_timestamp, signature),
              TimeUtil::SecondsToTimestamp(fake_time));
  }
}

// Verify that time is saved and restored correctly
TEST(time_contract_test, save_restore) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"foo", "bar", "baz", "qux"});

  std::string source_foo = "foo";
  TimeSigner ts_foo(config.subscope("node", 0));
  std::string source_bar = "bar";
  TimeSigner ts_bar(config.subscope("node", 1));
  std::string source_baz = "baz";
  TimeSigner ts_baz(config.subscope("node", 2));
  std::string source_qux = "qux";
  TimeSigner ts_qux(config.subscope("node", 3));

  Timestamp expected_time;
  {
    TimeContract tc(database, config);
    vector<uint8_t> signature;
    Timestamp t1 = TimeUtil::SecondsToTimestamp(12345);
    signature = ts_foo.Sign(t1);
    tc.Update(source_foo, t1, signature);
    Timestamp t2 = TimeUtil::SecondsToTimestamp(54321);
    signature = ts_bar.Sign(t2);
    tc.Update(source_bar, t2, signature);
    Timestamp t3 = TimeUtil::SecondsToTimestamp(10293);
    signature = ts_baz.Sign(t3);
    tc.Update(source_baz, t3, signature);
    Timestamp t4 = TimeUtil::SecondsToTimestamp(48576);
    signature = ts_qux.Sign(t4);
    tc.Update(source_qux, t4, signature);
    expected_time = tc.GetTime();

    SetOfKeyValuePairs updates({tc.Serialize()});
    BlockId block_id;
    Status result = database.addBlock(updates, block_id);
    ASSERT_EQ(result.isOK(), true);
  }

  // It's not actually necessary to push tc out of scope, since a new
  // TimeContract object would reinitialize itself from storage anyway, but
  // we've done so for completeness, and now we get to reuse the name anyway.

  TimeContract tc(database, config);
  ASSERT_EQ(tc.GetTime(), expected_time);
}

// Verify that the correct source is updated.
TEST(time_contract_test, update_correct_source) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B", "C"});

  // The idea here is to exploit the fact that the median of a three-reading
  // system will always be equal to one of those three reading. So, by asserting
  // that the resulting summary is equal to a particular reading, we can assert
  // that it was that reading that changed.

  std::string source_A = "A";
  TimeSigner ts_a(config.subscope("node", 0));
  std::string source_B = "B";
  TimeSigner ts_b(config.subscope("node", 1));
  std::string source_C = "C";
  TimeSigner ts_c(config.subscope("node", 2));

  TimeContract tc(database, config);
  vector<uint8_t> signature;
  Timestamp t1 = TimeUtil::SecondsToTimestamp(1);
  signature = ts_a.Sign(t1);
  tc.Update(source_A, t1, signature);
  Timestamp t2 = TimeUtil::SecondsToTimestamp(10);
  signature = ts_b.Sign(t2);
  tc.Update(source_B, t2, signature);
  Timestamp t3 = TimeUtil::SecondsToTimestamp(20);
  signature = ts_c.Sign(t3);
  tc.Update(source_C, t3, signature);

  // sanity: B is the median
  ASSERT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(10));

  // directly observe the median reading (B) being updated
  Timestamp t4 = TimeUtil::SecondsToTimestamp(11);
  signature = ts_b.Sign(t4);
  ASSERT_EQ(tc.Update(source_B, t4, signature),
            TimeUtil::SecondsToTimestamp(11));

  // first move one of the other values, then make it the median
  Timestamp t5 = TimeUtil::SecondsToTimestamp(21);
  signature = ts_c.Sign(t5);
  ASSERT_EQ(tc.Update(source_C, t5, signature),
            TimeUtil::SecondsToTimestamp(11));
  // either A or B moved, because the new summary is C's value
  Timestamp t6 = TimeUtil::SecondsToTimestamp(30);
  signature = ts_a.Sign(t6);
  ASSERT_EQ(tc.Update(source_A, t6, signature),
            TimeUtil::SecondsToTimestamp(21));

  // and one more leapfrog, either B or C moved, because the summary is A
  Timestamp t8 = TimeUtil::SecondsToTimestamp(40);
  signature = ts_b.Sign(t8);
  ASSERT_EQ(tc.Update(source_B, t8, signature),
            TimeUtil::SecondsToTimestamp(30));
}

// Verify that a source cannot move its own time backward.
TEST(time_contract_test, prevent_source_rollback) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"foo"});

  std::string source_foo = "foo";
  TimeSigner ts(config.subscope("node", 0));

  TimeContract tc1(database, config);
  vector<uint8_t> signature;
  Timestamp t1 = TimeUtil::SecondsToTimestamp(1000);
  signature = ts.Sign(t1);
  const Timestamp first_time = tc1.Update(source_foo, t1, signature);

  // first make sure a source can't rollback a cached copy
  Timestamp t2 = TimeUtil::SecondsToTimestamp(500);
  signature = ts.Sign(t2);
  const Timestamp second_time = tc1.Update(source_foo, t2, signature);
  ASSERT_EQ(second_time, first_time);

  // then make sure a fresh read is also protected
  SetOfKeyValuePairs updates({tc1.Serialize()});
  BlockId block_id;
  Status result = database.addBlock(updates, block_id);
  ASSERT_EQ(result.isOK(), true);

  TimeContract tc2(database, config);
  Timestamp t3 = TimeUtil::SecondsToTimestamp(250);
  signature = ts.Sign(t3);
  const Timestamp third_time = tc2.Update(source_foo, t3, signature);
  ASSERT_EQ(third_time, first_time);
}

// Only accept times from preconfigured sources.
TEST(time_contract_test, ignore_unknown_source) {
  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B", "C"});
  ConcordConfiguration fakeConfig = TestConfiguration({"X", "Y", "Z"});

  TimeContract tc1(database, config);
  vector<uint8_t> signature;
  Timestamp timestamp = TimeUtil::SecondsToTimestamp(1000);
  signature = TimeSigner(fakeConfig.subscope("node", 0)).Sign(timestamp);
  tc1.Update("X", timestamp, signature);
  signature = TimeSigner(fakeConfig.subscope("node", 1)).Sign(timestamp);
  tc1.Update("Y", timestamp, signature);
  signature = TimeSigner(fakeConfig.subscope("node", 2)).Sign(timestamp);
  tc1.Update("Z", timestamp, signature);

  // Config specified A,B,C as sources, so all of X,Y,Z updates should be
  // ignored.
  ASSERT_EQ(tc1.GetTime(), TimeUtil::GetEpoch());
}

// Verify general functionality of TimeSigner and TimeVerifier and their
// compliance with their claimed interface(s).
TEST(time_contract_test, time_signing_basic_interfaces) {
  ConcordConfiguration empty_config = TestConfiguration({});
  ConcordConfiguration config = TestConfiguration({"A", "B", "C"});
  ConcordConfiguration& a_config = config.subscope("node", 0);
  ConcordConfiguration& b_config = config.subscope("node", 1);
  ConcordConfiguration& c_config = config.subscope("node", 2);
  Timestamp arbitrary_time = TimeUtil::SecondsToTimestamp(10000000000);

  TimeSigner a_signer(a_config);

  EXPECT_THROW(TimeSigner config_signer(config), invalid_argument)
      << "TimeSigner's constructor fails to reject an inappropriate "
         "configuration.";

  b_config.eraseValue("time_source_id");
  EXPECT_THROW(TimeSigner b_signer(b_config), invalid_argument)
      << "TimeSigner's constructor fails to reject configuration for a node "
         "with no time source ID.";

  c_config.eraseAllValues();
  c_config.loadValue("time_source_id", "C");
  EXPECT_THROW(TimeSigner c_signer(c_config), invalid_argument)
      << "TimeSigner's constructor fails to reject configuration for a node "
         "with no private key.";

  vector<uint8_t> a_signature = a_signer.Sign(arbitrary_time);
  EXPECT_GT(a_signature.size(), 0)
      << "TimeSigner fails to produce a signature.";

  TimeSigner a_signer_copy(a_signer);
  vector<uint8_t> a_signature_copy = a_signer_copy.Sign(arbitrary_time);
  EXPECT_EQ(a_signature, a_signature_copy)
      << "TimeSigner's copy constructor does not work.";
  a_signer_copy = a_signer;
  a_signature_copy = a_signer_copy.Sign(arbitrary_time);
  EXPECT_EQ(a_signature, a_signature_copy)
      << "TimeSigner's copy assignment operator does not work.";

  EXPECT_THROW(TimeVerifier verifier(config), invalid_argument)
      << "TimeVerifier's constructor fails to reject a configuration lacking a "
         "public key for an identified time source.";
  c_config.eraseAllValues();

  TimeVerifier verifier(config);
  TimeVerifier empty_verifier(empty_config);

  EXPECT_TRUE(verifier.HasTimeSource("A"))
      << "TimeVerifier's constructor fails to construct a verifier that "
         "recognizes a legitimate time source.";
  EXPECT_FALSE(verifier.HasTimeSource("B"))
      << "TimeVerifier's constructor constructs a verifier that recognizes an "
         "illegitimate time source.";
  EXPECT_FALSE(empty_verifier.HasTimeSource("A"))
      << "TimeVerifier's constructor constructs a verifier that recognizes a "
         "time source when given an empty configuration.";

  TimeVerifier verifier_copy(verifier);
  EXPECT_TRUE(verifier_copy.HasTimeSource("A"))
      << "TimeVerifier's copy constructor doesn't work.";
  verifier_copy = verifier;
  EXPECT_TRUE(verifier_copy.HasTimeSource("A"))
      << "TimeVerifier's copy assignment operator doesn't work.";

  EXPECT_TRUE(verifier.Verify("A", arbitrary_time, a_signature))
      << "TimeVerifier fails to verify a legitimate signature.";

  b_config.loadValue("time_source_id", "B");
  TimeSigner b_signer(b_config);
  vector<uint8_t> b_signature = b_signer.Sign(arbitrary_time);
  b_config.eraseValue("time_source_id");
  EXPECT_FALSE(verifier.Verify("B", arbitrary_time, b_signature))
      << "TimeVerifier fails to reject a signature from an unrecognized "
         "source.";

  Timestamp arbitrary_time_plus_one = TimeUtil::SecondsToTimestamp(
      TimeUtil::TimestampToSeconds(arbitrary_time) + 1);
  EXPECT_FALSE(verifier.Verify("A", arbitrary_time_plus_one, a_signature))
      << "TimeVerifier fails to reject a signature that doesn't match the "
         "claimed source/time combination.";

  vector<uint8_t> empty_signature;
  EXPECT_FALSE(verifier.Verify("A", arbitrary_time, empty_signature))
      << "TimeVerifier fails to correctly handle being given an empty "
         "signature.";
}

// Verify TimeSigner and TimeVerifier are compatible.
TEST(time_contract_test, time_signature_verifiability) {
  ConcordConfiguration config =
      TestConfiguration({"A", "B", "C", "D", "E", "F", "G", "H"});
  Timestamp arbitrary_time = TimeUtil::SecondsToTimestamp(1000000000);
  TimeVerifier verifier(config);

  for (size_t i = 0; i < config.scopeSize("node"); ++i) {
    TimeSigner signer(config.subscope("node", i));
    vector<uint8_t> signature = signer.Sign(arbitrary_time);
    for (size_t j = 0; j < config.scopeSize("node"); j++) {
      string claimed_signer =
          config.subscope("node", j).getValue<string>("time_source_id");
      EXPECT_EQ((i == j),
                verifier.Verify(claimed_signer, arbitrary_time, signature))
          << "TimeVerifier constructed with public keys fails to correctly "
             "validate time sample signatures from TimeSigners constructed "
             "with corresponding private keys.";
    }
  }
}

// Verify TimeContract enforces signatures.
TEST(time_contract_test, time_signature_enforcement) {
  // It is suspected it may be theoretically possible for this test case to fail
  // randomly as a result of duplicate cryptographic keys being generated,
  // however, the probability of such failures is assumed to be negligible,
  // barring misconfiguration of the the randomness source.

  TestStorage database;
  ConcordConfiguration config = TestConfiguration({"A", "B"});
  ConcordConfiguration fake_config = TestConfiguration({"A", "B"});
  TimeContract tc(database, config);
  TimeSigner a_signer(config.subscope("node", 0));
  TimeSigner b_signer(config.subscope("node", 1));
  TimeSigner fake_a_signer(fake_config.subscope("node", 0));
  TimeSigner fake_b_signer(fake_config.subscope("node", 1));

  Timestamp t1 = TimeUtil::SecondsToTimestamp(3);
  vector<uint8_t> a_signature = a_signer.Sign(t1);
  tc.Update("A", t1, a_signature);
  vector<uint8_t> b_signature = b_signer.Sign(t1);
  tc.Update("B", t1, b_signature);

  Timestamp t2 = TimeUtil::SecondsToTimestamp(17);
  vector<uint8_t> empty_signature;
  tc.Update("A", t2, empty_signature);
  tc.Update("B", t2, empty_signature);

  EXPECT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(3))
      << "Time Contract fails to reject time updates without signatures.";

  a_signature = a_signer.Sign(t2);
  tc.Update("A", t2, a_signature);
  b_signature = b_signer.Sign(t2);
  tc.Update("B", t2, b_signature);

  EXPECT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(17))
      << "Time Contract fails to reject time updates "
         "with incorrect signatures.";

  Timestamp t3 = TimeUtil::SecondsToTimestamp(21);
  tc.Update("A", t3, a_signature);
  tc.Update("B", t3, b_signature);
  a_signature = fake_a_signer.Sign(t3);
  tc.Update("A", t3, a_signature);
  b_signature = fake_b_signer.Sign(t3);
  tc.Update("B", t3, b_signature);

  EXPECT_EQ(tc.GetTime(), TimeUtil::SecondsToTimestamp(17))
      << "Time Contract fails to reject time updates with invalid signatures.";
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
