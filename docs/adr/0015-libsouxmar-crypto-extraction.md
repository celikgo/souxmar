# ADR-0015: Extract libsouxmar-crypto for shared cryptographic primitives

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 11 push 1)
- **Author:** souxmar platform team
- **Deciders:** platform, security review, plugin team
- **Tier:** 2 (structural — touches every consumer of crypto in the
  tree, but the consumers' public surfaces are unchanged because the
  extraction goes through non-breaking forwarders)
- **Affects:** new `src/crypto/` + `include/souxmar/crypto/`;
  `src/updater/{verifier,install_layout}.cpp` become thin forwarders;
  Sprint 15 (cloud sync) + Sprint 16 (plugin marketplace) build on
  the new library from day one.

## Context

The auto-updater (Sprint 10 pushes 4–8) was the first consumer of
ed25519 verification + sha256 + lowercase-hex coding in souxmar.
Its `souxmar::update::verifier.h` carries:

- `ed25519_verify(message, signature, public_key) → SignatureStatus`
- `sha256_hex(bytes)`
- `hex_encode/hex_decode`

These are *primitives* — the same functions a future
`souxmar::marketplace` verifier (Sprint 16) or a
`souxmar::cloudsync::E2EEncryptor` (Sprint 15) will need. Two
options when the second consumer lands:

1. Add a dependency on `souxmar::update` from each new consumer.
   Pulls in the whole auto-updater (manifest parser, state machine,
   install-layout, …) just to reach `verify_detached_ed25519`.
2. Copy/paste the four functions into each new consumer's source
   tree. Three forks of the same crypto wrapper drift apart over
   time; each independently risks a security-relevant bug.
3. Extract the primitives into a sibling module, depend on that.

Option 3 is the right call. This ADR ratifies the extraction now —
before the second and third consumers depend on `souxmar::update` —
so the migration cost is paid once on the auto-updater (which we
know intimately) instead of three times on consumers that don't
exist yet.

The Sprint 10 retro flagged this decision specifically (one
ADR-worthy decision: "**factor the verifier + trust store into a
`libsouxmar-crypto` shared module** before the second and third
users land"). This ADR is the deliverable.

## Decision

Create a new CMake target `souxmar_crypto` (`souxmar::crypto`) under
`src/crypto/` with the public header
`include/souxmar/crypto/primitives.h`. Move the primitives into the
new namespace `souxmar::crypto`:

```cpp
namespace souxmar::crypto {
  enum class SignatureStatus { Ok, BadSignature, MalformedSignature,
                                MalformedPublicKey, EmptyMessage,
                                CryptoLibraryUnavailable };
  bool         hex_decode(std::string_view, std::vector<uint8_t>&);
  std::string  hex_encode(std::span<const uint8_t>);
  std::string  sha256_hex(std::span<const uint8_t>);
  SignatureStatus ed25519_verify(message, signature, public_key);
}
```

**Non-breaking migration.** The existing `souxmar::update::*` entry
points (which a future caller might still be wired to) stay in
place; their bodies become 1–3 line forwarders to `souxmar::crypto`.
The `souxmar::update::SignatureStatus` enum keeps its extra value
`UnknownKeyId` (which is a *policy* concern, not a primitive
concern) and continues to be the surface the
`verify_manifest_signature(manifest_bytes, signature_bytes, key_id,
trust_store)` policy entry point returns.

**What stays in `souxmar::update`:**

- `TrustStore` — the manifest-format-specific key-id-to-pubkey map.
  A future `souxmar::marketplace::PluginAuthorTrustStore` will have
  a different shape (per-author keys + signed-by relationships),
  so a generic primitive-layer TrustStore would be either
  premature-abstraction or under-fit.
- `verify_manifest_signature` — combines crypto + key lookup; the
  policy boundary.
- `embedded_trust.h.in` — release-pipeline-specific key injection.

**What moves out:**

- `ed25519_verify` — pure crypto.
- `hex_encode/decode` — pure encoding.
- `sha256_hex` — pure crypto.

**Migration approach.** Forwarders. The cost is one extra
indirection per call site (negligible), the benefit is callers
already wired to `souxmar::update::*` keep working unchanged.
Three sprints from now (Sprint 14, before public beta) the
forwarders can be removed if we want to force the namespace
migration; until then they're free.

## Alternatives considered

### Option A: Direct dependency on `souxmar::update`

Pulls 8 .cpp files + 5 headers + libsodium + tomlplusplus + the
whole manifest-format machinery into every consumer. The plugin
marketplace doesn't want a TOML manifest parser; the cloud-sync
service doesn't want install-root layout code. Hard no.

### Option B: Header-only crypto utilities

We could put the primitives in a header-only library to avoid the
extra link target. Tempting, but libsodium's init pattern needs
exactly one TU instantiating the `std::once_flag` — header-only
would either inline the once_flag (one per TU, defeats the purpose)
or require a single TU to define a single instance (which is just a
.cpp file, i.e. the current proposal). Rejected.

### Option C: Punt to Sprint 16

Wait for the plugin marketplace to land and *then* extract. Two
problems: (a) marketplace authors write against the API we publish,
so what's there at the time the marketplace ships is what they'll
use forever; (b) the cloud-sync work (Sprint 15) lands *before* the
marketplace and would have to either depend on
`souxmar::update` (option A) or fork the crypto (option B-without-
discipline). The longer we wait, the more entrenched the wrong
shape becomes.

## Consequences

### Positive

- One canonical crypto wrapper across the whole tree.
- A consumer can depend on `souxmar::crypto` (small, sodium-only)
  without pulling in any auto-updater surface area.
- Security review has a single target: any cryptographic concern
  reviews `src/crypto/primitives.cpp` first; everything else is
  policy.
- Sprint 14/15/16 work avoids a refactor that would have been
  *three times* the cost of doing it now.

### Negative

- One additional CMake target. Build-config noise. (Mitigated:
  the new target is a tiny 1-source-file library; vcpkg-cached
  builds barely notice.)
- One additional namespace boundary callers must learn. Mitigated
  by the forwarders — existing `souxmar::update` callers don't
  need to migrate.

### Risks

- **Forwarder drift.** If a future ABI change to
  `souxmar::crypto::SignatureStatus` adds a value, the
  `souxmar::update::verify_detached_ed25519` forwarder's switch
  statement must grow a matching arm. Mitigated: the forwarder
  switch is `case`-exhaustive without a `default:`, so a missed
  arm is a `-Wswitch` warning (and `souxmar::warnings` upgrades
  these to errors). The drift is compile-time-visible.

## Pre-mortem (one year from today)

It is 2027-05-11. The extraction has been live for a year; what
went wrong?

**Most likely:** we discover a third primitive worth sharing
(probably HKDF or AES-GCM for cloud-sync). The extraction's
namespace + module shape generalises cleanly — add one more file
to `src/crypto/`, one more declaration to
`include/souxmar/crypto/primitives.h`. The shape was the right
call.

**Next-most likely:** the auto-updater's `SignatureStatus` value
set diverges from the primitive's — say, a new `RevokedKey` value
the trust store needs but the primitive doesn't. The forwarder
becomes a 6-arm switch with the new value in the auto-updater
enum but nowhere in the primitive. This is fine; that's exactly
what the policy-vs-primitive split was for.

**Less likely:** the forwarder indirection ever shows up in a
flame graph. ed25519 verification dominates by 6 orders of
magnitude over a function-call indirection.

## References

- Sprint 10 retro § "One ADR-worthy decision surfaced" — the
  decision this ADR ratifies.
- ADR-0013 § "Why ed25519, detached signature" — the original
  algorithm pick; unchanged by this extraction.
- `docs/SECURITY.md` § "Release signing" — operational surface;
  references `souxmar::crypto` from this push onward.

## History

- 2026-05-11 (Sprint 11 push 1): **this ADR** — extract
  `souxmar::crypto::{ed25519_verify, hex_encode, hex_decode,
  sha256_hex}` into `src/crypto/`. `souxmar::update::*`
  forwarders stay in place; non-breaking migration.
