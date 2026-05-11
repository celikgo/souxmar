// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 1 — unit tests for the extracted crypto primitives.
// The full coverage of the verifier behaviour lives in
// test_update_verifier.cpp (which now exercises the auto-updater
// forwarder + the primitive together end-to-end); this file pins the
// primitive's contract independently so a future consumer (plugin
// marketplace S16, cloud sync S15) can trust the same surface.

#include "souxmar/crypto/primitives.h"

#include <sodium.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace souxmar::crypto;

namespace {

std::vector<std::uint8_t> bytes_of(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data() + s.size())};
}

}  // namespace

TEST(CryptoPrimitives, StatusStringRoundtrip) {
  EXPECT_EQ(to_string(SignatureStatus::Ok),                "ok");
  EXPECT_EQ(to_string(SignatureStatus::BadSignature),      "bad-signature");
  EXPECT_EQ(to_string(SignatureStatus::MalformedSignature), "malformed-signature");
}

TEST(CryptoPrimitives, HexRoundtrip) {
  const auto src = bytes_of("hello");
  const auto hex = hex_encode(src);
  std::vector<std::uint8_t> back;
  ASSERT_TRUE(hex_decode(hex, back));
  EXPECT_EQ(back, src);
}

TEST(CryptoPrimitives, Sha256MatchesNistEmpty) {
  const std::array<std::uint8_t, 0> empty{};
  EXPECT_EQ(sha256_hex({empty.data(), 0u}),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(CryptoPrimitives, Sha256MatchesNistAbc) {
  EXPECT_EQ(sha256_hex(bytes_of("abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(CryptoPrimitives, Ed25519VerifyEndToEnd) {
  if (sodium_init() < 0) ADD_FAILURE() << "sodium_init failed";
  std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
  for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(i);
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sk{};
  crypto_sign_seed_keypair(pk.data(), sk.data(), seed.data());

  const auto msg = bytes_of("hello souxmar crypto");
  std::array<std::uint8_t, crypto_sign_BYTES> sig{};
  unsigned long long sig_len = 0;
  crypto_sign_detached(sig.data(), &sig_len, msg.data(), msg.size(), sk.data());

  EXPECT_EQ(ed25519_verify(msg, {sig.data(), sig.size()}, {pk.data(), pk.size()}),
            SignatureStatus::Ok);

  // Tamper.
  auto bad_msg = msg;
  bad_msg[0] ^= 0x01;
  EXPECT_EQ(ed25519_verify(bad_msg, {sig.data(), sig.size()}, {pk.data(), pk.size()}),
            SignatureStatus::BadSignature);
}

TEST(CryptoPrimitives, Ed25519RejectsMalformedInputs) {
  std::vector<std::uint8_t> short_sig(63);
  std::vector<std::uint8_t> good_pk(32);
  const auto msg = bytes_of("hi");
  EXPECT_EQ(ed25519_verify(msg, short_sig, good_pk),
            SignatureStatus::MalformedSignature);

  std::vector<std::uint8_t> good_sig(64);
  std::vector<std::uint8_t> short_pk(31);
  EXPECT_EQ(ed25519_verify(msg, good_sig, short_pk),
            SignatureStatus::MalformedPublicKey);

  EXPECT_EQ(ed25519_verify({}, good_sig, good_pk),
            SignatureStatus::EmptyMessage);
}
