// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "utils/concord_eth_hash.hpp"
#include "utils/concord_eth_sign.hpp"
#include "utils/concord_utils.hpp"
#include "utils/rlp.hpp"

using namespace std;

using concord::utils::dehex;
using concord::utils::EthSign;
using concord::utils::RLPBuilder;
using concord::utils::RLPParser;

std::vector<uint8_t> next_part(RLPParser &parser, const char *label) {
  if (parser.at_end()) {
    cerr << "Transaction too short: missing " << label << endl;
    exit(-1);
  }
  return parser.next();
}

std::string addr_to_string(evm_address a) {
  static const char hexes[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < sizeof(evm_address); i++) {
    out.append(hexes + (a.bytes[i] >> 4), 1)
        .append(hexes + (a.bytes[i] & 0x0f), 1);
  }
  return out;
}

uint64_t uint_from_vector(std::vector<uint8_t> v, const char *label) {
  if (v.size() > 8) {
    cerr << label << " > uint64_t\n" << endl;
    exit(-1);
  }

  uint64_t u = 0;
  for (size_t i = 0; i < v.size(); i++) {
    u = u << 8;
    u += v[i];
  }

  return u;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    cerr << "Usage: ecrecover <signed transaction hex>" << endl;
    return -1;
  }

  string tx_s(argv[1]);
  vector<uint8_t> tx = dehex(tx_s);

  // Decode RLP

  RLPParser tx_envelope_p = RLPParser(tx);
  std::vector<uint8_t> tx_envelope = tx_envelope_p.next();

  if (!tx_envelope_p.at_end()) {
    cerr << "Warning: There are more bytes here than one transaction\n";
  }

  RLPParser tx_parts_p = RLPParser(tx_envelope);

  std::vector<uint8_t> nonce_v = next_part(tx_parts_p, "nonce");
  std::vector<uint8_t> gasPrice_v = next_part(tx_parts_p, "gas price");
  std::vector<uint8_t> gas_v = next_part(tx_parts_p, "start gas");
  std::vector<uint8_t> to = next_part(tx_parts_p, "to address");
  std::vector<uint8_t> value_v = next_part(tx_parts_p, "value");
  std::vector<uint8_t> data = next_part(tx_parts_p, "data");
  std::vector<uint8_t> v = next_part(tx_parts_p, "signature V");
  std::vector<uint8_t> r_v = next_part(tx_parts_p, "signature R");
  std::vector<uint8_t> s_v = next_part(tx_parts_p, "signature S");

  uint64_t nonce = uint_from_vector(nonce_v, "nonce");
  uint64_t gasPrice = uint_from_vector(gasPrice_v, "gas price");
  uint64_t gas = uint_from_vector(gas_v, "start gas");
  uint64_t value = uint_from_vector(value_v, "value");

  if (r_v.size() != sizeof(evm_uint256be)) {
    cout << "Signature R is too short (" << r_v.size() << ")" << endl;
    return -1;
  }
  evm_uint256be r;
  std::copy(r_v.begin(), r_v.end(), r.bytes);

  if (s_v.size() != sizeof(evm_uint256be)) {
    cout << "Signature S is too short (" << s_v.size() << ")" << endl;
    return -1;
  }
  evm_uint256be s;
  std::copy(s_v.begin(), s_v.end(), s.bytes);

  // Figure out non-signed V

  if (v.size() < 1) {
    cerr << "Signature V is empty\n" << endl;
    return -1;
  }

  uint64_t chainID = uint_from_vector(v, "chain ID");

  uint8_t actualV;
  if (chainID < 37) {
    cerr << "Non-EIP-155 signature V value" << endl;
    return -1;
  }

  if (chainID % 2) {
    actualV = 0;
    chainID = (chainID - 35) / 2;
  } else {
    actualV = 1;
    chainID = (chainID - 36) / 2;
  }

  // Re-encode RLP

  RLPBuilder unsignedTX_b;
  unsignedTX_b.start_list();

  std::vector<uint8_t> empty;
  unsignedTX_b.add(empty);    // S
  unsignedTX_b.add(empty);    // R
  unsignedTX_b.add(chainID);  // V
  unsignedTX_b.add(data);
  if (value == 0) {
    // signing hash expects 0x80 here, not 0x00
    unsignedTX_b.add(empty);
  } else {
    unsignedTX_b.add(value);
  }
  unsignedTX_b.add(to);
  if (gas == 0) {
    unsignedTX_b.add(empty);
  } else {
    unsignedTX_b.add(gas);
  }
  if (gasPrice == 0) {
    unsignedTX_b.add(empty);
  } else {
    unsignedTX_b.add(gasPrice);
  }
  if (nonce == 0) {
    unsignedTX_b.add(empty);
  } else {
    unsignedTX_b.add(nonce);
  }

  std::vector<uint8_t> unsignedTX = unsignedTX_b.build();

  // Recover Address

  evm_uint256be unsignedTX_h =
      concord::utils::eth_hash::keccak_hash(unsignedTX);
  EthSign verifier;
  evm_address from = verifier.ecrecover(unsignedTX_h, actualV, r, s);

  cout << "Recovered: " << addr_to_string(from) << endl;
  return 0;
}
