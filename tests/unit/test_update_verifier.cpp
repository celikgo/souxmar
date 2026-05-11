// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 5 — unit tests for the ed25519 detached-signature
// verifier. Surface in include/souxmar/update/verifier.h; implementation
// in src/updater/verifier.cpp; design lock-in in ADR-0013.
//
// The test fixture uses libsodium directly (crypto_sign_seed_keypair +
// crypto_sign_detached) to generate keypairs + signatures at test time.
// Seeded so the assertions are byte-deterministic; no on-disk fixture
// files. This pairs with the test_update_manifest.cpp suite which
// exercises the parser/validator *without* any crypto — the
// split-surface design from ADR-0013 means the parser is testable
// without keys and the verifier is testable without manifests.

#include "souxmar/update/verifier.h"

#include <sodium.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace souxmar::update;

namespace {

// Lower-case-hex literal of a 32-byte seed used to derive a stable
// keypair across every test. Any 32 bytes work; this one was chosen
// to be visually distinct from the all-zeros / all-ones seeds that
// libsodium docs use in their examples, so a stray test dump is
// recognisable as "ours".
constexpr const char* kSeedHex =
    "00112233445566778899aabbccddeeff"
    "ffeeddccbbaa99887766554433221100";

constexpr const char* kSecondSeedHex =
    "deadbeefcafebabefacefeedfeedface"
    "0011223344556677aabbccddeeff0011";

// A short message that stands in for a manifest. The verifier doesn't
// care that it's not actually TOML — it hashes raw bytes.
constexpr std::string_view kMessage =
    "schema = 1\n[release]\nversion = \"0.9.0\"\n";

// Decode a hex literal to bytes; ADD_FAILURE() on bad input so a
// typo in a test seed surfaces as a clear assertion rather than an
// opaque downstream failure.
std::vector<std::uint8_t> must_hex(std::string_view hex) {
  std::vector<std::uint8_t> out;
  if (!hex_decode(hex, out)) {
    ADD_FAILURE() << "must_hex: hex_decode rejected: " << hex;
  }
  return out;
}

// Test-side keypair helper. Returns (public_key, secret_key) generated
// from a hex-encoded 32-byte seed via crypto_sign_seed_keypair, which
// gives a byte-deterministic key pair.
struct Keypair {
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sk{};
};

Keypair make_keypair(std::string_view seed_hex) {
  if (sodium_init() < 0) {
    // sodium_init returns 1 if the library has already been initialised;
    // 0 on success; -1 on failure. Negative is the only thing that
    // matters here.
    ADD_FAILURE() << "sodium_init failed; test environment is broken";
  }
  const auto seed = must_hex(seed_hex);
  if (seed.size() != crypto_sign_SEEDBYTES) {
    ADD_FAILURE() << "make_keypair: wrong seed length " << seed.size();
  }
  Keypair k;
  const int rc = crypto_sign_seed_keypair(k.pk.data(), k.sk.data(),
                                          seed.data());
  if (rc != 0) {
    ADD_FAILURE() << "crypto_sign_seed_keypair returned " << rc;
  }
  return k;
}

// Sign `msg` with `sk`, return a 64-byte detached signature.
std::array<std::uint8_t, crypto_sign_BYTES>
sign_detached(std::span<const std::uint8_t>                              msg,
              const std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES>& sk) {
  std::array<std::uint8_t, crypto_sign_BYTES> sig{};
  unsigned long long sig_len = 0;
  const int rc = crypto_sign_detached(sig.data(), &sig_len,
                                      msg.data(),
                                      static_cast<unsigned long long>(msg.size()),
                                      sk.data());
  if (rc != 0 || sig_len != crypto_sign_BYTES) {
    ADD_FAILURE() << "crypto_sign_detached failed rc=" << rc
                  << " sig_len=" << sig_len;
  }
  return sig;
}

std::span<const std::uint8_t> as_bytes(std::string_view s) noexcept {
  return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

}  // namespace

// ===========================================================================
// SignatureStatus stringification — every enum value has a stable name.
// ===========================================================================

TEST(UpdateVerifier, StatusStringRoundtrip) {
  EXPECT_EQ(to_string(SignatureStatus::Ok),                       "ok");
  EXPECT_EQ(to_string(SignatureStatus::BadSignature),             "bad-signature");
  EXPECT_EQ(to_string(SignatureStatus::UnknownKeyId),             "unknown-key-id");
  EXPECT_EQ(to_string(SignatureStatus::MalformedSignature),       "malformed-signature");
  EXPECT_EQ(to_string(SignatureStatus::MalformedPublicKey),       "malformed-public-key");
  EXPECT_EQ(to_string(SignatureStatus::EmptyMessage),             "empty-message");
  EXPECT_EQ(to_string(SignatureStatus::CryptoLibraryUnavailable), "crypto-library-unavailable");
}

// ===========================================================================
// hex_decode — happy paths
// ===========================================================================

TEST(UpdateVerifierHex, DecodesEmptyInputAsEmptyVector) {
  std::vector<std::uint8_t> out{0x42};  // start non-empty to catch missing clear
  EXPECT_TRUE(hex_decode("", out));
  EXPECT_TRUE(out.empty());
}

TEST(UpdateVerifierHex, DecodesCanonical32BytePubkey) {
  std::vector<std::uint8_t> out;
  EXPECT_TRUE(hex_decode(kSeedHex, out));
  ASSERT_EQ(out.size(), 32u);
  EXPECT_EQ(out[0],  0x00);
  EXPECT_EQ(out[1],  0x11);
  EXPECT_EQ(out[15], 0xFF);
  EXPECT_EQ(out[16], 0xFF);
  EXPECT_EQ(out[31], 0x00);
}

TEST(UpdateVerifierHex, RoundtripsThroughHexEncode) {
  const auto bytes = must_hex(kSeedHex);
  EXPECT_EQ(hex_encode(bytes), kSeedHex);
}

// ===========================================================================
// hex_decode — error paths
// ===========================================================================

TEST(UpdateVerifierHex, RejectsOddLength) {
  std::vector<std::uint8_t> out;
  EXPECT_FALSE(hex_decode("abc", out));
  EXPECT_TRUE(out.empty());
}

TEST(UpdateVerifierHex, RejectsNonHexChars) {
  std::vector<std::uint8_t> out{0x42};
  EXPECT_FALSE(hex_decode("zz", out));
  EXPECT_TRUE(out.empty());  // verifier clears `out` on failure
}

TEST(UpdateVerifierHex, RejectsUppercaseHex) {
  // The decoder is intentionally case-sensitive: the manifest's sha256
  // is already required to be lowercase canonical (the validator's
  // is_lowercase_hex_64 check). Trust-store hex literals follow the
  // same rule so a mismatched-case grep across the repo is a single
  // diff.
  std::vector<std::uint8_t> out;
  EXPECT_FALSE(hex_decode("AB", out));
}

// ===========================================================================
// TrustStore
// ===========================================================================

TEST(UpdateVerifierTrustStore, AddHexRoundtrip) {
  const auto kp = make_keypair(kSeedHex);
  TrustStore ts;
  EXPECT_TRUE(ts.empty());

  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp.pk.data(), kp.pk.size()})));
  EXPECT_EQ(ts.size(), 1u);

  const auto* k = ts.find("release-2026");
  ASSERT_NE(k, nullptr);
  EXPECT_EQ(k->public_key_id, "release-2026");
  ASSERT_EQ(k->public_key.size(), kp.pk.size());
  for (std::size_t i = 0; i < kp.pk.size(); ++i) {
    EXPECT_EQ(k->public_key[i], kp.pk[i]) << "byte " << i;
  }
}

TEST(UpdateVerifierTrustStore, FindReturnsNullForUnknownId) {
  TrustStore ts;
  EXPECT_EQ(ts.find("anything"), nullptr);

  const auto kp = make_keypair(kSeedHex);
  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp.pk.data(), kp.pk.size()})));
  EXPECT_EQ(ts.find("release-2027"), nullptr);
  EXPECT_NE(ts.find("release-2026"), nullptr);
}

TEST(UpdateVerifierTrustStore, RejectsEmptyId) {
  const auto kp = make_keypair(kSeedHex);
  TrustStore ts;
  EXPECT_FALSE(ts.add("", {kp.pk.data(), kp.pk.size()}));
  EXPECT_TRUE(ts.empty());
}

TEST(UpdateVerifierTrustStore, RejectsWrongLengthRawPublicKey) {
  TrustStore ts;
  // 31 bytes — one short.
  const std::vector<std::uint8_t> too_short(31, 0xAA);
  EXPECT_FALSE(ts.add("k", too_short));
  // 33 bytes — one long.
  const std::vector<std::uint8_t> too_long(33, 0xBB);
  EXPECT_FALSE(ts.add("k", too_long));
  EXPECT_TRUE(ts.empty());
}

TEST(UpdateVerifierTrustStore, RejectsMalformedHexPubkey) {
  TrustStore ts;
  EXPECT_FALSE(ts.add_hex("k", "not-hex"));
  EXPECT_FALSE(ts.add_hex("k", "abc"));               // odd length
  EXPECT_FALSE(ts.add_hex("k", std::string(60, 'a')));// hex-valid but too short
  EXPECT_TRUE(ts.empty());
}

TEST(UpdateVerifierTrustStore, KeysIterationPreservesInsertionOrder) {
  const auto kp1 = make_keypair(kSeedHex);
  const auto kp2 = make_keypair(kSecondSeedHex);
  TrustStore ts;
  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp1.pk.data(), kp1.pk.size()})));
  ASSERT_TRUE(ts.add_hex("release-2027",
                         hex_encode({kp2.pk.data(), kp2.pk.size()})));
  ASSERT_EQ(ts.keys().size(), 2u);
  EXPECT_EQ(ts.keys()[0].public_key_id, "release-2026");
  EXPECT_EQ(ts.keys()[1].public_key_id, "release-2027");
}

// ===========================================================================
// verify_detached_ed25519 — happy path
// ===========================================================================

TEST(UpdateVerifier, HappyPathVerifies) {
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig.data(),    sig.size()},
                                    {kp.pk.data(),  kp.pk.size()}),
            SignatureStatus::Ok);
}

TEST(UpdateVerifier, DeterministicSignatureRoundtrip) {
  // Ed25519 is deterministic — signing the same message with the same
  // key produces the same 64 bytes. This locks in that the test-side
  // libsodium and the production-side libsodium are the same
  // implementation. If this test ever starts flaking, libsodium has
  // either been patched to nondeterministic mode or the test fixture
  // has been mis-wired.
  const auto kp   = make_keypair(kSeedHex);
  const auto sig1 = sign_detached(as_bytes(kMessage), kp.sk);
  const auto sig2 = sign_detached(as_bytes(kMessage), kp.sk);
  EXPECT_EQ(sig1, sig2);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig1.data(), sig1.size()},
                                    {kp.pk.data(), kp.pk.size()}),
            SignatureStatus::Ok);
}

// ===========================================================================
// verify_detached_ed25519 — BadSignature paths
// ===========================================================================

TEST(UpdateVerifier, RejectsSignatureFromWrongKey) {
  const auto kp1 = make_keypair(kSeedHex);
  const auto kp2 = make_keypair(kSecondSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp1.sk);
  // Verify against kp2.pk — must fail.
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig.data(),    sig.size()},
                                    {kp2.pk.data(), kp2.pk.size()}),
            SignatureStatus::BadSignature);
}

TEST(UpdateVerifier, RejectsTamperedMessage) {
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  std::string tampered(kMessage);
  // Flip the version-string from "0.9.0" to "0.9.1".
  auto pos = tampered.find("0.9.0");
  ASSERT_NE(pos, std::string::npos);
  tampered[pos + 4] = '1';
  EXPECT_EQ(verify_detached_ed25519(as_bytes(tampered),
                                    {sig.data(),    sig.size()},
                                    {kp.pk.data(),  kp.pk.size()}),
            SignatureStatus::BadSignature);
}

TEST(UpdateVerifier, RejectsTamperedSignature) {
  const auto kp  = make_keypair(kSeedHex);
  auto       sig = sign_detached(as_bytes(kMessage), kp.sk);
  sig[0] ^= 0x01;  // flip a single bit
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig.data(),   sig.size()},
                                    {kp.pk.data(), kp.pk.size()}),
            SignatureStatus::BadSignature);
}

// ===========================================================================
// verify_detached_ed25519 — Malformed* + EmptyMessage paths
// ===========================================================================

TEST(UpdateVerifier, RejectsShortSignature) {
  const auto kp = make_keypair(kSeedHex);
  const std::vector<std::uint8_t> short_sig(63, 0xAB);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    short_sig,
                                    {kp.pk.data(), kp.pk.size()}),
            SignatureStatus::MalformedSignature);
}

TEST(UpdateVerifier, RejectsLongSignature) {
  const auto kp = make_keypair(kSeedHex);
  const std::vector<std::uint8_t> long_sig(65, 0xCD);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    long_sig,
                                    {kp.pk.data(), kp.pk.size()}),
            SignatureStatus::MalformedSignature);
}

TEST(UpdateVerifier, RejectsShortPublicKey) {
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  const std::vector<std::uint8_t> short_pk(31, 0x00);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig.data(), sig.size()},
                                    short_pk),
            SignatureStatus::MalformedPublicKey);
}

TEST(UpdateVerifier, RejectsLongPublicKey) {
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  const std::vector<std::uint8_t> long_pk(33, 0x00);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    {sig.data(), sig.size()},
                                    long_pk),
            SignatureStatus::MalformedPublicKey);
}

TEST(UpdateVerifier, RejectsEmptyMessage) {
  const auto kp  = make_keypair(kSeedHex);
  // Sign a real message so the signature is structurally valid; we
  // want to be sure the empty-message rejection comes from our
  // length gate, not from libsodium's verify path.
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  const std::span<const std::uint8_t> empty{};
  EXPECT_EQ(verify_detached_ed25519(empty,
                                    {sig.data(),   sig.size()},
                                    {kp.pk.data(), kp.pk.size()}),
            SignatureStatus::EmptyMessage);
}

// ===========================================================================
// Length-check ordering — wrong sig length wins over wrong pk length wins
// over empty message. The state machine cares about this ordering only
// indirectly (it'd retry on UnknownKeyId / EmptyMessage but not on
// Malformed*), but locking the precedence lets the rollback log stay
// stable across libsodium updates.
// ===========================================================================

TEST(UpdateVerifier, MalformedSignatureBeatsMalformedPublicKey) {
  const std::vector<std::uint8_t> short_sig(63, 0xAB);
  const std::vector<std::uint8_t> short_pk(31,  0xCD);
  EXPECT_EQ(verify_detached_ed25519(as_bytes(kMessage),
                                    short_sig,
                                    short_pk),
            SignatureStatus::MalformedSignature);
}

TEST(UpdateVerifier, MalformedPublicKeyBeatsEmptyMessage) {
  // Correct-length sig, wrong-length pk, empty message — the
  // pk-length gate fires before the message-empty gate.
  const std::vector<std::uint8_t> sig(64, 0xAB);
  const std::vector<std::uint8_t> short_pk(31, 0xCD);
  const std::span<const std::uint8_t> empty{};
  EXPECT_EQ(verify_detached_ed25519(empty, sig, short_pk),
            SignatureStatus::MalformedPublicKey);
}

// ===========================================================================
// verify_manifest_signature — high-level entry point
// ===========================================================================

TEST(UpdateVerifierHighLevel, HappyPathFromTrustStoreLookup) {
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  TrustStore ts;
  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp.pk.data(), kp.pk.size()})));
  EXPECT_EQ(verify_manifest_signature(as_bytes(kMessage),
                                      {sig.data(), sig.size()},
                                      "release-2026",
                                      ts),
            SignatureStatus::Ok);
}

TEST(UpdateVerifierHighLevel, RejectsUnknownKeyIdBeforeCrypto) {
  // The trust store has *no* keys. We pass a fully-valid signature.
  // The lookup-by-id fires before any crypto work — this is what
  // gives the state machine in push 6 a fast path when a manifest
  // names a key id we don't recognise.
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  TrustStore ts;  // empty
  EXPECT_EQ(verify_manifest_signature(as_bytes(kMessage),
                                      {sig.data(), sig.size()},
                                      "release-2026",
                                      ts),
            SignatureStatus::UnknownKeyId);
}

TEST(UpdateVerifierHighLevel, RejectsKeyIdMismatch) {
  // The store has key release-2026; the manifest names release-2027.
  // Verification path picks the store's keys, not the manifest's
  // claim — so the wrong id is UnknownKeyId, not BadSignature.
  const auto kp  = make_keypair(kSeedHex);
  const auto sig = sign_detached(as_bytes(kMessage), kp.sk);
  TrustStore ts;
  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp.pk.data(), kp.pk.size()})));
  EXPECT_EQ(verify_manifest_signature(as_bytes(kMessage),
                                      {sig.data(), sig.size()},
                                      "release-2027",
                                      ts),
            SignatureStatus::UnknownKeyId);
}

TEST(UpdateVerifierHighLevel, BadSignaturePropagatesThroughHighLevelCall) {
  const auto kp1 = make_keypair(kSeedHex);
  const auto kp2 = make_keypair(kSecondSeedHex);
  // Sign with kp1.sk but list kp2.pk under the same id in the store.
  const auto sig = sign_detached(as_bytes(kMessage), kp1.sk);
  TrustStore ts;
  ASSERT_TRUE(ts.add_hex("release-2026",
                         hex_encode({kp2.pk.data(), kp2.pk.size()})));
  EXPECT_EQ(verify_manifest_signature(as_bytes(kMessage),
                                      {sig.data(), sig.size()},
                                      "release-2026",
                                      ts),
            SignatureStatus::BadSignature);
}
