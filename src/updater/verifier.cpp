// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — ed25519 detached-signature verifier. See
// include/souxmar/update/verifier.h for the public surface and
// docs/adr/0013-signed-update-manifest.md § "Why ed25519, detached
// signature" for the design.
//
// The wrapper is thin on purpose. libsodium's
// crypto_sign_verify_detached is the load-bearing call; everything
// else here is (a) initialise-once bookkeeping, (b) length checks
// (so the state machine in push 6 gets a typed SignatureStatus
// instead of having to interpret a -1 return code), and (c) the
// hex helpers shared with trust-store loading.

#include "souxmar/update/verifier.h"

#include <sodium.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace souxmar::update {

namespace {

// libsodium requires sodium_init() before any other library call. The
// docs guarantee it is safe to call concurrently after the first
// successful init returns, so we wrap it in std::call_once and cache
// the success bit. A failed init (extremely rare — would mean the
// platform CSPRNG is broken) is permanent for the process lifetime;
// the verifier returns CryptoLibraryUnavailable to every call after
// such a failure, which the state machine surfaces as "reinstall".
struct SodiumInit {
  std::once_flag       once;
  std::atomic<bool>    ok{false};

  bool ensure() {
    std::call_once(once, [this] {
      ok.store(sodium_init() >= 0, std::memory_order_release);
    });
    return ok.load(std::memory_order_acquire);
  }
};

SodiumInit& sodium_singleton() {
  static SodiumInit s;
  return s;
}

// Lowercase-hex char => 0..15, or 16 on rejection. Branchless-ish
// helper kept here rather than in core/ because nothing else in the
// tree wants a hex decoder that rejects uppercase.
constexpr std::uint8_t hex_nibble(char c) noexcept {
  if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
  return 16;
}

}  // namespace

// ============================================================================
// SignatureStatus stringification
// ============================================================================

std::string_view to_string(SignatureStatus s) noexcept {
  switch (s) {
    case SignatureStatus::Ok:                       return "ok";
    case SignatureStatus::BadSignature:             return "bad-signature";
    case SignatureStatus::UnknownKeyId:             return "unknown-key-id";
    case SignatureStatus::MalformedSignature:       return "malformed-signature";
    case SignatureStatus::MalformedPublicKey:       return "malformed-public-key";
    case SignatureStatus::EmptyMessage:             return "empty-message";
    case SignatureStatus::CryptoLibraryUnavailable: return "crypto-library-unavailable";
  }
  return "unknown";
}

// ============================================================================
// Hex helpers
// ============================================================================

bool hex_decode(std::string_view hex, std::vector<std::uint8_t>& out) {
  out.clear();
  if (hex.size() % 2 != 0) return false;
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const std::uint8_t hi = hex_nibble(hex[i]);
    const std::uint8_t lo = hex_nibble(hex[i + 1]);
    if (hi == 16 || lo == 16) {
      out.clear();
      return false;
    }
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return true;
}

std::string hex_encode(std::span<const std::uint8_t> bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(bytes.size() * 2);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[2 * i + 0] = kHex[(bytes[i] >> 4) & 0x0F];
    out[2 * i + 1] = kHex[bytes[i]        & 0x0F];
  }
  return out;
}

// ============================================================================
// TrustStore
// ============================================================================

bool TrustStore::add(std::string                   id,
                     std::span<const std::uint8_t> public_key) {
  if (id.empty()) return false;
  if (public_key.size() != kEd25519PublicKeyBytes) return false;
  TrustedKey k;
  k.public_key_id = std::move(id);
  k.public_key.assign(public_key.begin(), public_key.end());
  keys_.push_back(std::move(k));
  return true;
}

bool TrustStore::add_hex(std::string id, std::string_view hex_pubkey) {
  std::vector<std::uint8_t> decoded;
  if (!hex_decode(hex_pubkey, decoded)) return false;
  if (decoded.size() != kEd25519PublicKeyBytes) return false;
  return add(std::move(id), decoded);
}

const TrustedKey* TrustStore::find(std::string_view id) const noexcept {
  for (const auto& k : keys_) {
    if (k.public_key_id == id) return &k;
  }
  return nullptr;
}

// ============================================================================
// Verification
// ============================================================================

SignatureStatus
verify_detached_ed25519(std::span<const std::uint8_t> message,
                        std::span<const std::uint8_t> signature,
                        std::span<const std::uint8_t> public_key) {
  // Length gates first. These reject build-pipeline misconfigurations
  // (wrong-curve key shipped in the trust store, truncated .sig file
  // on the CDN) without touching the crypto path.
  if (signature.size()  != kEd25519SignatureBytes) {
    return SignatureStatus::MalformedSignature;
  }
  if (public_key.size() != kEd25519PublicKeyBytes) {
    return SignatureStatus::MalformedPublicKey;
  }
  if (message.empty()) {
    return SignatureStatus::EmptyMessage;
  }

  if (!sodium_singleton().ensure()) {
    return SignatureStatus::CryptoLibraryUnavailable;
  }

  const int rc = crypto_sign_verify_detached(
      signature.data(),
      message.data(),
      static_cast<unsigned long long>(message.size()),
      public_key.data());
  return rc == 0 ? SignatureStatus::Ok : SignatureStatus::BadSignature;
}

SignatureStatus
verify_manifest_signature(std::span<const std::uint8_t> manifest_bytes,
                          std::span<const std::uint8_t> signature_bytes,
                          std::string_view              key_id,
                          const TrustStore&             trust) {
  const TrustedKey* key = trust.find(key_id);
  if (key == nullptr) {
    return SignatureStatus::UnknownKeyId;
  }
  return verify_detached_ed25519(manifest_bytes,
                                 signature_bytes,
                                 key->public_key);
}

}  // namespace souxmar::update
