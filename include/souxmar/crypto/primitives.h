// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-crypto — shared cryptographic primitives.
//
// Sprint 11 push 1. ADR-0015 ratifies the module split: the
// auto-updater (Sprint 10 push 5) built the first user of ed25519
// verification + sha256; the plugin marketplace's authenticated-
// download path (Sprint 16) + the Pro-tier cloud-sync E2E
// encryption (Sprint 15) will each need the same primitives. Rather
// than have each grow its own copy or each depend on souxmar_update,
// we extract the primitives here.
//
// What's in this module:
//   * ed25519 detached-signature verify (libsodium wrapper).
//   * sha256 hex digest.
//   * hex encode + decode helpers (lowercase ASCII canonical).
//
// What's NOT here:
//   * Trust stores (the auto-updater's TrustStore is policy-tied to
//     the signed-manifest format; the plugin marketplace will need a
//     differently-shaped one). Each consumer owns its own trust-policy
//     layer over these primitives.
//   * Manifest parsing, file-format handling, on-disk state. Those
//     stay in their respective domain modules.
//
// The auto-updater's `souxmar::update::verifier.h` surface (which
// predates this extraction) keeps its existing entry points — they
// now forward to `souxmar::crypto::*`. Callers can migrate
// incrementally; nothing in libsouxmar-update needs to be re-cut.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::crypto {

// ed25519 fixed sizes (RFC 8032).
inline constexpr std::size_t kEd25519PublicKeyBytes = 32;
inline constexpr std::size_t kEd25519SignatureBytes = 64;
inline constexpr std::size_t kSha256DigestBytes     = 32;

// ---- Signature status ---------------------------------------------------
//
// Same value-set the auto-updater uses today. Keeping the names + the
// integer ordering identical lets the existing `souxmar::update::
// SignatureStatus` become an alias (or vice versa) without rewriting
// any audit log or test.

enum class SignatureStatus : std::uint8_t {
  Ok                        = 0,
  BadSignature              = 1,
  // Note: UnknownKeyId is *not* a primitive concern — the primitive
  // takes (message, signature, public_key) directly. The caller's
  // trust-policy layer maps a key-id to a public-key first, then
  // calls the primitive; if the lookup fails, the caller raises its
  // own UnknownKeyId.
  MalformedSignature        = 3,
  MalformedPublicKey        = 4,
  EmptyMessage              = 5,
  CryptoLibraryUnavailable  = 6,
};

[[nodiscard]] std::string_view to_string(SignatureStatus) noexcept;

// ---- Hex helpers --------------------------------------------------------

// Decode lowercase-canonical hex into bytes. `out` is cleared on
// failure. Returns false on odd length or non-[0-9a-f] characters.
[[nodiscard]] bool
hex_decode(std::string_view hex, std::vector<std::uint8_t>& out);

[[nodiscard]] std::string
hex_encode(std::span<const std::uint8_t> bytes);

// ---- ed25519 verify -----------------------------------------------------

[[nodiscard]] SignatureStatus
ed25519_verify(std::span<const std::uint8_t> message,
               std::span<const std::uint8_t> signature,
               std::span<const std::uint8_t> public_key);

// ---- sha256 -------------------------------------------------------------

[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);

}  // namespace souxmar::crypto
