// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Tests of the transaction signing and address recovery functions.

#include <nlohmann/json.hpp>
#include "gtest/gtest.h"
#include "utils/concord_eth_hash.hpp"
#include "utils/concord_eth_sign.hpp"
#include "utils/concord_utils.hpp"

using namespace std;
using json = nlohmann::json;

using concord::utils::dehex;
using concord::utils::EthSign;

namespace {

evmc_uint256be uint256_from_string(const string &str) {
  std::vector<uint8_t> vec = dehex(str);
  evmc_uint256be hash;
  std::copy(vec.begin(), vec.end(), hash.bytes);
  return hash;
}

evmc_address addr_from_string(const string &str) {
  std::vector<uint8_t> vec = dehex(str);
  evmc_address addr;
  std::copy(vec.begin(), vec.end(), addr.bytes);
  return addr;
}

std::string addr_to_string(evmc_address a) {
  static const char hexes[] = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < sizeof(evmc_address); i++) {
    out.append(hexes + (a.bytes[i] >> 4), 1)
        .append(hexes + (a.bytes[i] & 0x0f), 1);
  }
  return out;
}

std::string hash_to_string(evmc_uint256be a) {
  static const char hexes[] = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < sizeof(evmc_uint256be); i++) {
    out.append(hexes + (a.bytes[i] >> 4), 1)
        .append(hexes + (a.bytes[i] & 0x0f), 1);
  }
  return out;
}

void expect_match(const evmc_address e, const evmc_address t,
                  bool should_match) {
  bool matches = true;
  for (int i = 0; i < sizeof(evmc_address); i++) {
    matches = matches & (e.bytes[i] == t.bytes[i]);
  }

  EXPECT_TRUE(matches == should_match)
      << "Expected: " << addr_to_string(e) << std::endl
      << "   Found: " << addr_to_string(t) << std::endl;
}

void expect_match(const evmc_uint256be e, const evmc_uint256be t,
                  bool should_match) {
  bool matches = true;
  for (int i = 0; i < sizeof(evmc_uint256be); i++) {
    matches = matches & (e.bytes[i] == t.bytes[i]);
  }

  EXPECT_TRUE(matches == should_match)
      << "Expected: " << hash_to_string(e) << std::endl
      << "   Found: " << hash_to_string(t) << std::endl;
}

/**
 * The next two tests operate on a known transaction on the public Ethereum
 * mainnet.
 *
 * https://etherscan.io/tx/0x6ab11d26df13bc3b2cb1c09c4d274bfce325906c617d2bc744b45fa39b7f8c68
 *
 * Raw transaction:
 * 0xf86b19847735940082520894f6c3fff0b77efe806fcc10176b8cbf71c6dfe3be
 *   880429d069189e00008025a0141c8487e4db65457266978a7f8d856b777a51dd
 *   9863d31637ccdec8dea74397a07fd0e14d0e3e891882f13acbe68740f1c5bd82
 *   a1a254f898cdbec5e9cfa8cf38
 */

// this is derived from the raw transaction above: as per EIP-155, replace v,r,s
// with CHAIN_ID,0,0, and that changes the list-length bytes on the front
const string unsignedRLP_s(
    "0xeb"                                        // list length = 235
    "19"                                          // nonce
    "8477359400"                                  // gas price
    "825208"                                      // gas limit
    "94f6c3fff0b77efe806fcc10176b8cbf71c6dfe3be"  // to
    "880429d069189e0000"                          // value
    "80"                                          // data
    "01"                                          // chain ID
    "80"                                          // r
    "80");                                        // s
const string expectedFrom_s("0x42c4f19a097955ff2a013ef8f014977f4e8516c3");
const string givenSigR_s(
    "0x141c8487e4db65457266978a7f8d856b777a51dd9863d31637ccdec8dea74397");
const string givenSigS_s(
    "0x7fd0e14d0e3e891882f13acbe68740f1c5bd82a1a254f898cdbec5e9cfa8cf38");
const int8_t givenSigV = 37;

/**
 * Test that the address recovered from a known transaction on the public
 * Ethereum blockchain matches the address that Etherscan displays.
 */
TEST(sign_text, known_ecrecover_matches) {
  evmc_uint256be testSigR = uint256_from_string(givenSigR_s);
  evmc_uint256be testSigS = uint256_from_string(givenSigS_s);
  uint8_t testSigV = (givenSigV % 2) ? 0 : 1;

  vector<uint8_t> unsigned_rlp = dehex(unsignedRLP_s);
  evmc_uint256be unsigned_rlp_hash =
      concord::utils::eth_hash::keccak_hash(unsigned_rlp);

  EthSign verifier;
  evmc_address calc_addr =
      verifier.ecrecover(unsigned_rlp_hash, testSigV, testSigR, testSigS);

  evmc_address expected_addr = addr_from_string(expectedFrom_s);
  expect_match(expected_addr, calc_addr, true);
}

/**
 * Using the same transaction from known_ecrecover_matches, make sure that the
 * recovered address does *not* match if a byte in the transaction is
 * altered. (Basic assurance that the match above wasn't trivially accidental.)
 */
TEST(sign_test, known_modified_ecrecover_no_matches) {
  evmc_uint256be testSigR = uint256_from_string(givenSigR_s);
  evmc_uint256be testSigS = uint256_from_string(givenSigS_s);
  uint8_t testSigV = (givenSigV % 2) ? 0 : 1;

  vector<uint8_t> unsigned_rlp = dehex(unsignedRLP_s);
  // modify first byte of transaction
  unsigned_rlp[0] = unsigned_rlp[0] + 1;
  evmc_uint256be unsigned_rlp_hash =
      concord::utils::eth_hash::keccak_hash(unsigned_rlp);

  EthSign verifier;
  evmc_address calc_addr =
      verifier.ecrecover(unsigned_rlp_hash, testSigV, testSigR, testSigS);

  evmc_address expected_addr = addr_from_string(expectedFrom_s);
  expect_match(expected_addr, calc_addr, false);
}

/**
 * This mimics one of Ethereum's own tests: forget transactions and RLP
 * encoding, and just sign some known data with some known key, then check that
 * the signature matches, and recovery also matches.
 */
TEST(sign_test, simple_data_sign_and_recover) {
  // The known key is just the keccak hash for the string "sec"
  const string secret_s("sec");
  std::vector<uint8_t> secret;
  std::copy(secret_s.begin(), secret_s.end(), std::back_inserter(secret));
  const evmc_uint256be key = concord::utils::eth_hash::keccak_hash(secret);

  // The known message is just the keccak hash for the string "msg"
  const string msg_s("msg");
  std::vector<uint8_t> msg;
  std::copy(msg_s.begin(), msg_s.end(), std::back_inserter(msg));
  const evmc_uint256be hash = concord::utils::eth_hash::keccak_hash(msg);

  // Compute the signature
  EthSign verifier;
  std::vector<uint8_t> signature = verifier.sign(hash, key);

  // These are the signatures we expect for the known key and message (they
  // were precomputed and copied here).
  const string sigR_s(
      "b826808a8c41e00b7c5d71f211f005a84a7b97949d5e765831e1da4e34c9b829");
  const evmc_uint256be sigR = uint256_from_string(sigR_s);
  const string sigS_s(
      "5d2a622eee50f25af78241c1cb7cfff11bcf2a13fe65dee1e3b86fd79a4e3ed0");
  const evmc_uint256be sigS = uint256_from_string(sigS_s);
  const uint8_t sigV = 0;

  // Extract the R, S, and V components of the signature we computed.
  uint8_t calc_v = signature[0];
  evmc_uint256be calc_r;
  std::copy(signature.begin() + 1, signature.begin() + 33, calc_r.bytes);
  evmc_uint256be calc_s;
  std::copy(signature.begin() + 33, signature.end(), calc_s.bytes);

  // Check the signature matches
  std::cout << "Checking signature:" << std::endl;
  expect_match(sigR, calc_r, true);
  expect_match(sigS, calc_s, true);
  EXPECT_EQ(sigV, calc_v);
  std::cout << "Done checking signature" << std::endl;

  // This is the public key that matches our known private key (it was
  // precomputed and copied here).
  const string expectedPub_s(
      "e40930c838d6cca526795596e368d16083f0672f4ab61788277abfa23c3740e1"
      "cc84453b0b24f49086feba0bd978bb4446bae8dff1e79fcc1e9cf482ec2d07c3");
  std::vector<uint8_t> expectedPub = dehex(expectedPub_s);
  const evmc_uint256be expectedHash =
      concord::utils::eth_hash::keccak_hash(expectedPub);
  evmc_address expectedAddr;
  std::copy(
      expectedHash.bytes + (sizeof(evmc_uint256be) - sizeof(evmc_address)),
      expectedHash.bytes + sizeof(evmc_uint256be), expectedAddr.bytes);

  // Recover the public key from our signed message.
  evmc_address calc_addr = verifier.ecrecover(hash, sigV, sigR, sigS);

  // Expect that the recovered address matches the precomputed address.
  expect_match(expectedAddr, calc_addr, true);
}

/**
 * This test recreates the example walkthrough found on
 * https://medium.com/@codetractio/inside-an-ethereum-transaction-fa94ffca912f
 * Note that the transaction is pre-EIP155, so V does not include the chain ID.
 */
TEST(sign_test, known_hash_recover) {
  // First stage: make sure we hash a message the same.
  const string given_message_s(
      "0xe6808504a817c800830186a0"
      "94687422eea2cb73b5d3e242ba5456b782919afc858203e882c0de");
  const string given_rlpHash_s(
      "0x6a74f15f29c3227c5d1d2e27894da58d417a484ef53bc7aa57ee323b42ded656");

  vector<uint8_t> given_message = dehex(given_message_s);
  evmc_uint256be given_rlpHash = uint256_from_string(given_rlpHash_s);
  evmc_uint256be message_hash =
      concord::utils::eth_hash::keccak_hash(given_message);
  std::cout << "Checking RLP Hash" << std::endl;
  expect_match(given_rlpHash, message_hash, true);

  // Second stage: check the recovered address matches
  const string given_v_s("0x1c");
  const string given_r_s(
      "0x668ed6500efd75df7cb9c9b9d8152292a75453ec2d11030b0eec42f6a7ace602");
  const string given_s_s(
      "0x3efcbbf4d53e0dfa4fde5c6d9a73221418652abc66dff7fddd78b81cc28b9fbf");
  const string given_from_s("0x53ae893e4b22d707943299a8d0c844df0e3d5557");

  // 27 == pre-EIP155 offset
  uint8_t given_v = dehex(given_v_s)[0] - 27;
  evmc_uint256be given_r = uint256_from_string(given_r_s);
  evmc_uint256be given_s = uint256_from_string(given_s_s);
  evmc_address given_from = addr_from_string(given_from_s);

  EthSign verifier;
  evmc_address from =
      verifier.ecrecover(given_rlpHash, given_v, given_r, given_s);

  expect_match(given_from, from, true);
}

/**
 * Similar to known_ec_recover, but using a transaction and address I created
 * myself, instead of finding on the public blockchain.
 */
TEST(sign_test, personal_ecrecover) {
  const string given_unsignedTx_s(
      "0xf8cb"    // list length (cb = 203)
      "01"        // nonce
      "64"        // gas price
      "831e8480"  // gas limit
      "80"        // to (empty = creation)
      "80"        // value
      "b8bb"      // data length (bb = 187)
      "60606040523415600e57600080fd5b609f8061001c6000396000f30060606040"
      "5260043610603f576000357c0100000000000000000000000000000000000000"
      "000000000000000000900463ffffffff16806320965255146044575b600080fd"
      "5b3415604e57600080fd5b6054606a565b604051808281526020019150506040"
      "5180910390f35b6000600c9050905600a165627a7a72305820b68d14c31445a2"
      "b927e84f3ffa433ef0535b98ef5e84d0385d52022b172fd0990029"  // data
      "835734b5"  // v/chainID (decimal 5715125)
      "80"        // r
      "80");      // s
  const string given_v_s("0xae698d");
  const string given_r_s(
      "0798ffdc7dedd9332dd1db991ed59e016120a06b041e888b43152d1374550cc4");
  const string given_s_s(
      "0a6b6cb66fdce8df289a4284cb980dd61dae4a588b978d679790efc63f7450e5");
  const string given_from_s("0x59825ed03aa4f65e2b52e1c17dca5eb875196c9b");

  vector<uint8_t> given_unsignedTx = dehex(given_unsignedTx_s);
  evmc_uint256be given_r = uint256_from_string(given_r_s);
  evmc_uint256be given_s = uint256_from_string(given_s_s);
  // EIP155 V == chainID * 2 + 35 (or 36);
  vector<uint8_t> given_v_v = dehex(given_v_s);
  uint8_t given_v = (given_v_v[given_v_v.size() - 1] % 2) ? 0 : 1;

  evmc_uint256be unsignedRlpHash =
      concord::utils::eth_hash::keccak_hash(given_unsignedTx);
  EthSign verifier;
  evmc_address from =
      verifier.ecrecover(unsignedRlpHash, given_v, given_r, given_s);

  evmc_address given_from = addr_from_string(given_from_s);
  expect_match(given_from, from, true);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
