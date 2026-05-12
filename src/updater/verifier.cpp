// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — ed25519 detached-signature verifier. See
// include/souxmar/update/verifier.h for the public surface and
// docs/adr/0013-signed-update-manifest.md § "Why ed25519, detached
// signature" for the design.
//
// Sprint 11 push 1 (ADR-0015) extracted the crypto primitives into
// libsouxmar-crypto. The functions below are now thin forwarders;
// the TrustStore type + the verify_manifest_signature() entry point
// (which mixes crypto with key-id lookup) remain here because they
// are auto-updater-specific policy.

#include "souxmar/update/verifier.h"

#include "souxmar/crypto/primitives.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace souxmar::update {

// ============================================================================
// SignatureStatus stringification
// ============================================================================
//
// The auto-updater carries one extra value (UnknownKeyId) that the
// primitive doesn't surface — it's a policy concern. So this
// to_string keeps its full switch rather than forwarding to
// souxmar::crypto::to_string.

std::string_view to_string(SignatureStatus s) noexcept {
  switch (s) {
    case SignatureStatus::Ok:
      return "ok";
    case SignatureStatus::BadSignature:
      return "bad-signature";
    case SignatureStatus::UnknownKeyId:
      return "unknown-key-id";
    case SignatureStatus::MalformedSignature:
      return "malformed-signature";
    case SignatureStatus::MalformedPublicKey:
      return "malformed-public-key";
    case SignatureStatus::EmptyMessage:
      return "empty-message";
    case SignatureStatus::CryptoLibraryUnavailable:
      return "crypto-library-unavailable";
  }
  return "unknown";
}

// ============================================================================
// Hex helpers — forwarders to souxmar::crypto (ADR-0015).
// ============================================================================

bool hex_decode(std::string_view hex, std::vector<std::uint8_t>& out) {
  return crypto::hex_decode(hex, out);
}

std::string hex_encode(std::span<const std::uint8_t> bytes) {
  return crypto::hex_encode(bytes);
}

// ============================================================================
// TrustStore
// ============================================================================

bool TrustStore::add(std::string id, std::span<const std::uint8_t> public_key) {
  if (id.empty())
    return false;
  if (public_key.size() != kEd25519PublicKeyBytes)
    return false;
  TrustedKey k;
  k.public_key_id = std::move(id);
  k.public_key.assign(public_key.begin(), public_key.end());
  keys_.push_back(std::move(k));
  return true;
}

bool TrustStore::add_hex(std::string id, std::string_view hex_pubkey) {
  std::vector<std::uint8_t> decoded;
  if (!hex_decode(hex_pubkey, decoded))
    return false;
  if (decoded.size() != kEd25519PublicKeyBytes)
    return false;
  return add(std::move(id), decoded);
}

const TrustedKey* TrustStore::find(std::string_view id) const noexcept {
  for (const auto& k : keys_) {
    if (k.public_key_id == id)
      return &k;
  }
  return nullptr;
}

// ============================================================================
// Verification
// ============================================================================

SignatureStatus verify_detached_ed25519(std::span<const std::uint8_t> message,
                                        std::span<const std::uint8_t> signature,
                                        std::span<const std::uint8_t> public_key) {
  // Forwarder to libsouxmar-crypto. The enum value-set + ordering
  // match the primitive's (lookup-policy values like UnknownKeyId
  // never come up here — those only surface in
  // verify_manifest_signature).
  const auto s = crypto::ed25519_verify(message, signature, public_key);
  switch (s) {
    case crypto::SignatureStatus::Ok:
      return SignatureStatus::Ok;
    case crypto::SignatureStatus::BadSignature:
      return SignatureStatus::BadSignature;
    case crypto::SignatureStatus::MalformedSignature:
      return SignatureStatus::MalformedSignature;
    case crypto::SignatureStatus::MalformedPublicKey:
      return SignatureStatus::MalformedPublicKey;
    case crypto::SignatureStatus::EmptyMessage:
      return SignatureStatus::EmptyMessage;
    case crypto::SignatureStatus::CryptoLibraryUnavailable:
      return SignatureStatus::CryptoLibraryUnavailable;
  }
  return SignatureStatus::BadSignature;
}

SignatureStatus verify_manifest_signature(std::span<const std::uint8_t> manifest_bytes,
                                          std::span<const std::uint8_t> signature_bytes,
                                          std::string_view key_id,
                                          const TrustStore& trust) {
  const TrustedKey* key = trust.find(key_id);
  if (key == nullptr) {
    return SignatureStatus::UnknownKeyId;
  }
  return verify_detached_ed25519(manifest_bytes, signature_bytes, key->public_key);
}

}  // namespace souxmar::update
