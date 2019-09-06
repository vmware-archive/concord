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

std::string vec_to_string(std::vector<uint8_t> v) {
  static const char hexes[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    out.append(hexes + (v[i] >> 4), 1).append(hexes + (v[i] & 0x0f), 1);
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
  if (argc != 3) {
    cerr << "Usage: ecsign <transaction hex> <private key hex>" << endl;
    return -1;
  }

  string tx_s(argv[1]);
  vector<uint8_t> tx = dehex(tx_s);

  string key_s(argv[2]);
  vector<uint8_t> key_v = dehex(key_s);
  if (key_v.size() != sizeof(evm_uint256be)) {
    cerr << "Key hex not long enough (is " << key_v.size() << " bytes, must be "
         << sizeof(evm_uint256be) << " bytes)" << endl;
    return -1;
  }
  evm_uint256be key;
  std::copy(key_v.begin(), key_v.end(), key.bytes);

  // Decode RLP
  // It's kind of annoying that we have to decode and recode, but it's
  // necessary to get the RLP list length correct.

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
  std::vector<uint8_t> v_in = next_part(tx_parts_p, "signature V");
  std::vector<uint8_t> r_in_v = next_part(tx_parts_p, "signature R");
  std::vector<uint8_t> s_in_v = next_part(tx_parts_p, "signature S");

  uint64_t nonce = uint_from_vector(nonce_v, "nonce");
  uint64_t gasPrice = uint_from_vector(gasPrice_v, "gas price");
  uint64_t gas = uint_from_vector(gas_v, "start gas");
  uint64_t value = uint_from_vector(value_v, "value");

  if (r_in_v.size() != 0) {
    cerr << "R should be empty before signing" << endl;
    return -1;
  }

  if (s_in_v.size() != 0) {
    cerr << "S should be empty before signing" << endl;
    return -1;
  }

  // Figure out non-signed V

  if (v_in.size() < 1) {
    cerr << "Signature V is empty\n" << endl;
    return -1;
  }

  uint64_t chainID = uint_from_vector(v_in, "chain ID");

  // Sanity: reencode parts in RLP, to make sure the signed transaction we
  // encode is exactly the same as the unsigned transaction we decoded. The
  // signature won't match if this isn't true.

  RLPBuilder sanity_b;
  sanity_b.start_list();

  std::vector<uint8_t> empty;
  sanity_b.add(empty);    // S
  sanity_b.add(empty);    // R
  sanity_b.add(chainID);  // V
  sanity_b.add(data);
  if (value == 0) {
    // signing hash expects 0x80 here, not 0x00
    sanity_b.add(empty);
  } else {
    sanity_b.add(value);
  }
  sanity_b.add(to);
  if (gas == 0) {
    sanity_b.add(empty);
  } else {
    sanity_b.add(gas);
  }
  if (gasPrice == 0) {
    sanity_b.add(empty);
  } else {
    sanity_b.add(gasPrice);
  }
  if (nonce == 0) {
    sanity_b.add(empty);
  } else {
    sanity_b.add(nonce);
  }

  std::vector<uint8_t> sanity = sanity_b.build();
  if (sanity.size() != tx.size()) {
    cerr << "Re-encode pre-signing did not match input. Signature will not "
            "match."
         << endl;
    cerr << "Encoded: 0x" << vec_to_string(sanity) << endl;
    return -1;
  }
  for (size_t i = 0; i < sanity.size(); i++) {
    if (sanity[i] != tx[i]) {
      cerr << "Re-encode pre-signing did not match input. Signature will not "
              "match."
           << endl;
      cerr << "Encoded: 0x" << vec_to_string(sanity) << endl;
      return -1;
    }
  }

  // Sign

  evm_uint256be txhash = concord::utils::eth_hash::keccak_hash(tx);
  EthSign verifier;
  std::vector<uint8_t> signature = verifier.sign(txhash, key);

  if (signature.size() != 1 + 2 * sizeof(evm_uint256be)) {
    cerr << "Signature is not expected length (was " << signature.size()
         << " bytes, expected " << (1 + 2 * sizeof(evm_uint256be)) << ")"
         << endl;
    return -1;
  }

  uint64_t v = signature[0];
  std::vector<uint8_t> r;
  std::copy(signature.begin() + 1,
            signature.begin() + 1 + sizeof(evm_uint256be),
            std::back_inserter(r));
  std::vector<uint8_t> s;
  std::copy(signature.begin() + 1 + sizeof(evm_uint256be), signature.end(),
            std::back_inserter(s));

  if (v > 1) {
    cerr << "Signature v is greater than 1 (" << v << ")" << endl;
    return -1;
  }

  v = v + 35 + (2 * chainID);

  // Re-encode RLP

  RLPBuilder signedTX_b;
  signedTX_b.start_list();

  signedTX_b.add(s);
  signedTX_b.add(r);
  signedTX_b.add(v);
  signedTX_b.add(data);
  if (value == 0) {
    // signing hash expects 0x80 here, not 0x00
    signedTX_b.add(empty);
  } else {
    signedTX_b.add(value);
  }
  signedTX_b.add(to);
  if (gas == 0) {
    signedTX_b.add(empty);
  } else {
    signedTX_b.add(gas);
  }
  if (gasPrice == 0) {
    signedTX_b.add(empty);
  } else {
    signedTX_b.add(gasPrice);
  }
  if (nonce == 0) {
    signedTX_b.add(empty);
  } else {
    signedTX_b.add(nonce);
  }

  std::vector<uint8_t> signedTX = signedTX_b.build();

  cout << "Signed TX: 0x" << vec_to_string(signedTX) << endl;
  return 0;
}
