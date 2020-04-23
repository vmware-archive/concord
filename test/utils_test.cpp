// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <nlohmann/json.hpp>
#include "gtest/gtest.h"
#include "utils/concord_utils.hpp"
#include "utils/rlp.hpp"

using namespace std;
using json = nlohmann::json;
using boost::multiprecision::uint256_t;

using concord::utils::from_evmc_uint256be;
using concord::utils::from_uint256_t;
using concord::utils::RLPBuilder;
using concord::utils::to_evmc_uint256be;
using concord::utils::to_uint256_t;

namespace {
TEST(utils_test, parses_genesis_block) {
  // string genesis_test_file = "resources/genesis.json";
  // json pj = parse_genesis_block(genesis_test_file);
  // int chainID = pj["config"]["chainId"];
  // ASSERT_EQ(chainID, 1);
}

// examples from https://github.com/ethereum/wiki/wiki/RLP
TEST(rlp_test, example_dog) {
  RLPBuilder rlpb;
  std::vector<uint8_t> input{'d', 'o', 'g'};
  rlpb.add(input);
  std::vector<uint8_t> expect{0x83, 'd', 'o', 'g'};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_cat_dog) {
  RLPBuilder rlpb;
  rlpb.start_list();
  std::vector<uint8_t> input1{'d', 'o', 'g'};
  rlpb.add(input1);
  std::vector<uint8_t> input2{'c', 'a', 't'};
  rlpb.add(input2);
  std::vector<uint8_t> expect{0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g'};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_empty_string) {
  RLPBuilder rlpb;
  std::vector<uint8_t> input;
  rlpb.add(input);
  std::vector<uint8_t> expect{0x80};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_empty_list) {
  RLPBuilder rlpb;
  rlpb.start_list();
  std::vector<uint8_t> expect{0xc0};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_integer_0) {
  RLPBuilder rlpb;
  rlpb.add(0);
  std::vector<uint8_t> expect{0x80};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_integer_15) {
  RLPBuilder rlpb;
  rlpb.add(15);
  std::vector<uint8_t> expect{0x0f};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_integer_1024) {
  RLPBuilder rlpb;
  rlpb.add(1024);
  std::vector<uint8_t> expect{0x82, 0x04, 0x00};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_nexted_list) {
  RLPBuilder rlpb;

  // remember that RLPBuilder expects additions in reverse order, so read the
  // test case backward to understand the code: [ [], [[]], [ [], [[]] ] ]
  rlpb.start_list();
  {
    rlpb.start_list();
    {
      rlpb.start_list();
      {
        rlpb.start_list();
        rlpb.end_list();
      }
      rlpb.end_list();

      rlpb.start_list();
      rlpb.end_list();
    }
    rlpb.end_list();

    rlpb.start_list();
    {
      rlpb.start_list();
      rlpb.end_list();
    }
    rlpb.end_list();

    rlpb.start_list();
    rlpb.end_list();
  }  // implicit end
  std::vector<uint8_t> expect{0xc7, 0xc0, 0xc1, 0xc0, 0xc3, 0xc0, 0xc1, 0xc0};
  EXPECT_EQ(expect, rlpb.build());
}

TEST(rlp_test, example_lipsum) {
  RLPBuilder rlpb;
  std::string str("Lorem ipsum dolor sit amet, consectetur adipisicing elit");
  std::vector<uint8_t> input(str.begin(), str.end());
  rlpb.add(input);
  std::vector<uint8_t> expect{0xb8, 0x38};
  std::copy(input.begin(), input.end(), std::back_inserter(expect));
  EXPECT_EQ(expect, rlpb.build());
}

TEST(utils_test, to_evmc_uint256be_test) {
  uint64_t val = 0xabcd1234;
  evmc_uint256be expected;
  to_evmc_uint256be(val, &expected);
  EXPECT_EQ(expected.bytes[31], 0x34);
  EXPECT_EQ(expected.bytes[30], 0x12);
  EXPECT_EQ(expected.bytes[29], 0xcd);
  EXPECT_EQ(expected.bytes[28], 0xab);
  for (int i = 0; i < 28; i++) {
    EXPECT_EQ(expected.bytes[i], 0x00);
  }
}

TEST(utils_test, from_evmc_uint256be_test) {
  uint64_t expected = 0x12121212abcd1234;
  evmc_uint256be val;
  for (int i = 0; i < 28; i++) {
    val.bytes[i] = 0x12;
  }
  val.bytes[28] = 0xab;
  val.bytes[29] = 0xcd;
  val.bytes[30] = 0x12;
  val.bytes[31] = 0x34;
  EXPECT_EQ(expected, from_evmc_uint256be(&val));
}

TEST(utils_test, to_uint256_t_test) {
  evmc_uint256be input{0};
  input.bytes[31] = 0xef;
  input.bytes[30] = 0xbe;
  input.bytes[29] = 0xad;
  input.bytes[28] = 0xde;

  uint256_t expected{"0xdeadbeef"};
  EXPECT_EQ(expected, to_uint256_t(&input));
}

TEST(utils_test, from_uint256_t_test) {
  uint256_t input{"0xdeadbeef"};

  evmc_uint256be expected{0};
  expected.bytes[31] = 0xef;
  expected.bytes[30] = 0xbe;
  expected.bytes[29] = 0xad;
  expected.bytes[28] = 0xde;
  evmc_uint256be out = from_uint256_t(&input);
  EXPECT_EQ(0, memcmp(expected.bytes, out.bytes, sizeof(evmc_uint256be)));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
