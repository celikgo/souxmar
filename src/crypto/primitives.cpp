// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-crypto — primitives implementation. Wraps libsodium.
// See include/souxmar/crypto/primitives.h for the contract.

#include "souxmar/crypto/primitives.h"

#include <sodium.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::crypto {

namespace {

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

constexpr std::uint8_t hex_nibble(char c) noexcept {
  if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
  return 16;
}

}  // namespace

std::string_view to_string(SignatureStatus s) noexcept {
  switch (s) {
    case SignatureStatus::Ok:                       return "ok";
    case SignatureStatus::BadSignature:             return "bad-signature";
    case SignatureStatus::MalformedSignature:       return "malformed-signature";
    case SignatureStatus::MalformedPublicKey:       return "malformed-public-key";
    case SignatureStatus::EmptyMessage:             return "empty-message";
    case SignatureStatus::CryptoLibraryUnavailable: return "crypto-library-unavailable";
  }
  return "unknown";
}

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

SignatureStatus ed25519_verify(std::span<const std::uint8_t> message,
                                std::span<const std::uint8_t> signature,
                                std::span<const std::uint8_t> public_key) {
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

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
  std::array<std::uint8_t, crypto_hash_sha256_BYTES> digest{};
  // sha256 doesn't require sodium_init per libsodium docs, but doing
  // it once anyway is cheap insurance against future libsodium
  // versions reorganising the init contract.
  (void)sodium_singleton().ensure();
  crypto_hash_sha256(digest.data(),
                     bytes.data(),
                     static_cast<unsigned long long>(bytes.size()));
  return hex_encode({digest.data(), digest.size()});
}

}  // namespace souxmar::crypto
