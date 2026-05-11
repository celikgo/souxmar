// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — ed25519 detached-signature verifier.
//
// Sprint 10 push 5 of Platform's "Auto-updater across all 3 OSes;
// signed manifest pipeline; rollback protocol" XL story. Push 4 landed
// the manifest data model + parser + structural validator; this header
// adds the cryptographic surface that the state machine (push 6) calls
// after fetching the manifest + its detached signature from a release
// CDN.
//
// Design constraints, all from docs/adr/0013-signed-update-manifest.md:
//
//   * ed25519 only at v1. Manifest.signing.algorithm must read
//     "ed25519"; the validator (push 4) already rejected anything else
//     as an error. We pin libsodium for the implementation — its
//     reference is well-vetted, the surface is small, and the curve
//     has no agility footguns.
//   * Detached signature, byte-stream-faithful. The verifier hashes
//     the manifest *bytes as they came off the wire*. No
//     canonicalisation. Callers pass the raw TOML buffer through,
//     untouched. (ADR-0013 § "Why ed25519, detached signature".)
//   * No libsodium types in the public surface. Callers see byte
//     spans and a SignatureStatus enum; the .cpp owns the
//     crypto_sign_verify_detached call. Lets the verifier swap to
//     a future Sigstore implementation (ADR-0013 § "Sigstore /
//     cosign / OIDC-bound signatures") without breaking the
//     header.
//   * No "verifier returns a string" path. The state machine in push 6
//     branches on a SignatureStatus value; the rollback log in push 7
//     records the SignatureStatus value verbatim so a refused-update
//     audit entry is queryable without parsing text. Diagnostic
//     strings come from to_string() and are for humans only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::update {

// ed25519 fixed sizes (RFC 8032). The verifier rejects any input that
// disagrees with these — a wrong-length signature or public key is a
// build-pipeline bug, never a runtime corruption we should try to
// recover from.
inline constexpr std::size_t kEd25519PublicKeyBytes = 32;
inline constexpr std::size_t kEd25519SignatureBytes = 64;

// Outcome of a verification attempt. Every libsodium error path the
// implementation can produce lands in exactly one of these values; the
// state machine (push 6) branches on the enum, and the rollback log
// (push 7) records it. Strings are diagnostics-only — *never* parse
// to_string() output.
enum class SignatureStatus : std::uint8_t {
  Ok                        = 0,
  // The signature did not verify against the public key. Could be a
  // wrong key, a tampered manifest, or a corrupted signature — the
  // verifier deliberately collapses all three (cryptographic
  // indistinguishability) so the apply gate treats them identically.
  BadSignature              = 1,
  // The manifest's public_key_id is not in the trust store. Either
  // the manifest came from a different signer than this client knows
  // about, or the client is too old to recognise the key (signing-key
  // rotation = separate coordinated event, ADR-0013 § "Pinned key id,
  // separate rotation event").
  UnknownKeyId              = 2,
  // Signature length is not exactly kEd25519SignatureBytes.
  MalformedSignature        = 3,
  // Public key length is not exactly kEd25519PublicKeyBytes.
  MalformedPublicKey        = 4,
  // Empty message — we refuse to even attempt verification of a
  // zero-byte manifest. A real release manifest is ~50 lines of TOML;
  // empty is always wrong.
  EmptyMessage              = 5,
  // libsodium failed to initialise — the platform's CSPRNG is
  // unavailable or the library was built broken. Unrecoverable; the
  // state machine should surface this as "your install is broken,
  // reinstall" rather than retrying.
  CryptoLibraryUnavailable  = 6,
};

[[nodiscard]] std::string_view to_string(SignatureStatus) noexcept;

// One signer the client recognises. The id matches the manifest's
// `[signing].public_key_id`; the bytes are the raw 32-byte ed25519
// public key. The build embeds the project's release-2026 key (and
// any prior keys still in the rotation window) into the binary;
// rotation = ship a new client. See docs/SECURITY.md (lands with
// push 7).
struct TrustedKey {
  std::string                public_key_id;
  std::vector<std::uint8_t>  public_key;   // exactly kEd25519PublicKeyBytes
};

// In-memory trust store: an ordered set of TrustedKey, looked up by
// public_key_id. Small by construction (typically one current key
// plus a small overlap window during a rotation), so a linear scan is
// the right data structure. Order of insertion is preserved so a
// `souxmar update --print-trust-store` dump (push 6) prints keys in a
// stable order.
class TrustStore {
 public:
  TrustStore() = default;

  // Add a key. Returns false (and leaves the store unchanged) if
  // `public_key` is the wrong length or `id` is empty. A failed add
  // is a build-time configuration bug — the caller's caller (the
  // application that embeds the release key list) should treat it as
  // a hard error during startup, not propagate it as a runtime
  // condition.
  [[nodiscard]] bool add(std::string                   id,
                         std::span<const std::uint8_t> public_key);

  // Convenience for the common embedding case: keys are typically
  // compiled in as 64-character lowercase-hex literals. Returns false
  // on malformed hex or wrong decoded length.
  [[nodiscard]] bool add_hex(std::string      id,
                             std::string_view hex_pubkey);

  // Lookup by id. Returns nullptr if the id is not present; never
  // throws.
  [[nodiscard]] const TrustedKey* find(std::string_view id) const noexcept;

  [[nodiscard]] bool         empty() const noexcept { return keys_.empty(); }
  [[nodiscard]] std::size_t  size()  const noexcept { return keys_.size(); }

  // For iteration in tests + the `--print-trust-store` CLI dump.
  [[nodiscard]] const std::vector<TrustedKey>& keys() const noexcept {
    return keys_;
  }

 private:
  std::vector<TrustedKey> keys_;
};

// Hex helpers — lowercase, ASCII-canonical. The detached signature
// file shipped to the release CDN is hex-encoded (matches the
// manifest's `sha256` convention, diffable via `curl + diff`); the
// trust store's embedded keys are hex literals. Both rejections must
// be visible to callers without exception throwing, hence the bool
// return + out-param shape.
//
// `out` is cleared on failure. Empty input decodes to an empty vector
// (returns true) — callers that need a non-empty result should length-
// check after decoding.
[[nodiscard]] bool
hex_decode(std::string_view hex, std::vector<std::uint8_t>& out);

// Encode bytes to lowercase hex. Used by the `--print-trust-store`
// dump and by the audit-log writer in push 7.
[[nodiscard]] std::string hex_encode(std::span<const std::uint8_t> bytes);

// ---- Verification --------------------------------------------------------
//
// Two entry points: a low-level one that takes raw byte spans, and a
// high-level one that takes a trust store + a key id (typically pulled
// from Manifest::signing::public_key_id by the state machine in
// push 6). Splitting them lets a future TUF-style snapshot verifier
// (or a Sigstore migration) reuse the low-level call while substituting
// its own key-resolution policy.
//
// Neither entry point throws. Crypto operations are constant-time
// against the message + key bytes per libsodium guarantees; the
// length checks above happen before any constant-time work, so a
// wrong-length input is rejected in well-known short paths. We
// deliberately do not return rich diagnostics on failure — the verifier
// is hostile-input-facing and avoidance of timing leaks beats verbose
// error reporting here.

// Low-level: verify a detached ed25519 signature directly. Returns
// Ok only when libsodium's crypto_sign_verify_detached returns 0
// for the given (message, signature, public_key) triple.
[[nodiscard]] SignatureStatus
verify_detached_ed25519(std::span<const std::uint8_t> message,
                        std::span<const std::uint8_t> signature,
                        std::span<const std::uint8_t> public_key);

// High-level: look up `key_id` in `trust`, then verify. Returns
// UnknownKeyId if the lookup fails (before any crypto work happens),
// otherwise forwards to verify_detached_ed25519.
[[nodiscard]] SignatureStatus
verify_manifest_signature(std::span<const std::uint8_t> manifest_bytes,
                          std::span<const std::uint8_t> signature_bytes,
                          std::string_view              key_id,
                          const TrustStore&             trust);

}  // namespace souxmar::update
