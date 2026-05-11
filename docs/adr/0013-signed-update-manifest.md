# ADR-0013: Signed update manifest format + auto-updater pipeline

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 10 push 4)
- **Author:** souxmar platform team
- **Deciders:** platform, security review, DX, desktop
- **Tier:** 2 (design — touches a frozen-ish on-disk format and a
  trust-root decision; not a frozen-ABI ratchet)
- **Affects:** new `include/souxmar/update/manifest.h`,
  `src/updater/*` (new module), the release pipeline
  (`scripts/release/*` — landing across pushes 4–8), the desktop
  app's update flow (`docs/DESKTOP_APP.md` § Update protocol),
  CI signing-key handling (`docs/SECURITY.md` § Release signing).

## Context

Sprint 10's Platform XL story is "Auto-updater across all 3 OSes;
signed manifest pipeline; rollback protocol" (`docs/SPRINT_PLAN.md`).
A user installing souxmar must be able to:

1. **Discover** that a newer release exists, on their channel.
2. **Verify** the new release is authentic — the binary they're
   about to run was signed by a key the project controls.
3. **Apply** the update atomically, with the running process able
   to relaunch into the new version without leaving the install
   half-overwritten if power is yanked mid-swap.
4. **Roll back** to the previously-installed version if the new
   one is broken — under the user's manual control, plus a few
   automated conditions (the new version crashes on first launch).

Every part of that depends on a **signed update manifest** — a
small, structured file that names the artefacts for each
(channel × OS × arch) tuple, the cryptographic hashes that pin
those artefacts, and the trust signature the client must verify
before doing anything else. The manifest is the substrate; the
state machine, the rollback log, the kill-switch behaviour all key
off fields in it.

This ADR locks in the manifest format and the signing scheme. It
deliberately does *not* design the state machine, the file-system
layout of the install root, or the desktop-app UI for "update
available" — those land in Sprint 10 pushes 5–8 and inherit the
shape this ADR decides.

The decisions below were the contentious ones, in pre-mortem order:
TOML vs. JSON; embedded vs. detached signatures; ed25519 vs.
Ed448 / Sigstore / X.509 PKI; channel structure; rollback window
storage; kill-switch surface.

## Decision

The update manifest is a TOML document with a **fixed top-level
schema** the parser refuses to load on unrecognised major versions
(`schema = 1` is the only currently-accepted value). The full
shape and the signing scheme are below.

### Manifest TOML schema (v1)

```toml
# Header — always at the top, always exactly these two fields.
schema       = 1                                  # parser refuses unknown values
generated_at = "2026-05-11T14:00:00Z"             # RFC 3339 UTC

# Distribution channel. Clients self-select their channel; the
# release pipeline emits a separate manifest per channel.
[channel]
name        = "stable"                            # stable | beta | nightly
expires_at  = "2026-08-11T14:00:00Z"              # hard kill date; refuse to apply after this

# The release this manifest offers.
[release]
version              = "0.9.0"                    # SemVer of the offered build
released_at          = "2026-05-10T10:00:00Z"
min_previous_version = "0.8.0"                    # refuse to apply unless caller is >= this
rollback_target      = "0.8.4"                    # `souxmar update rollback` lands here; empty disables
notes_url            = "https://souxmar.dev/releases/0.9.0"
mandatory            = false                      # true => clients SHOULD apply before expires_at

# One [[artifact]] per (os, arch) pair. No duplicates allowed.
[[artifact]]
os     = "linux"                                  # linux | macos | windows
arch   = "x86_64"                                 # x86_64 | aarch64
url    = "https://dl.souxmar.dev/0.9.0/souxmar-0.9.0-linux-x86_64.tar.zst"
sha256 = "<64 hex chars>"                         # binds the manifest to a specific artefact
size   = 48217600                                 # bytes; download-progress + pre-flight disk check

# Signing block. The signature itself is detached — hosted as
# `<manifest_url>.sig`, fetched separately, never embedded.
[signing]
algorithm     = "ed25519"
public_key_id = "release-2026"                    # pinned key id; rotation = separate coordinated event
```

The parser's job is to read the file and reject anything
structurally wrong (missing `schema`, unknown channel name, bad
sha256 length, duplicate (os, arch) pair). The verifier's job —
**a separate component, landing in push 5** — is to take the raw
manifest bytes, the detached signature bytes, and the pinned
public key, and decide if the signature is valid. Splitting the
two surfaces lets the parser be unit-tested without crypto.

### Why TOML, not JSON

The plugin manifest, the plugin index, and the pipeline file are
already TOML (`docs/CODING_STYLE.md` § "On-disk formats are TOML
when humans write them, JSON when machines do"). The update
manifest is on the boundary — humans don't *write* it (the
release pipeline generates it), but humans *review* it during a
release sign-off, and TOML's section structure makes that review
considerably easier than reading a JSON blob. Reusing
tomlplusplus also avoids adding a JSON parser dep we don't
otherwise need at this layer. JSON would be defensible; TOML is
consistent.

### Why ed25519, detached signature

- **ed25519** is the conservative modern choice: small keys (32
  bytes), small signatures (64 bytes), well-vetted reference
  implementation in libsodium, no agility footguns. Ed448 buys
  marginally more theoretical security at significant
  implementation-complexity cost; we don't process million-doc
  workloads here. X.509 / RSA would let us use platform-native
  validation but locks us into PKI we'd have to operate.
- **Detached signature** (the signature is a separate file
  alongside the manifest, not embedded in it) preserves the
  property that the *bytes the verifier hashes* are byte-for-byte
  what an auditor or a developer can diff against the git
  history. If the signature were embedded, the verifier would
  have to canonicalise the TOML on the fly (strip the signature
  field, re-serialise) — an entire class of canonicalisation
  bugs we sidestep.
- **Pinned key id, separate rotation event.** The `public_key_id`
  field names which key signed *this* manifest; the client's
  trust store maps key ids to public keys. Rotation is a
  documented coordinated event (`docs/SECURITY.md` § Release
  signing key rotation), not an in-band manifest field. This
  follows the same logic as Notary / TUF root-of-trust handling:
  the *signing* key can rotate without re-signing every artefact;
  the *root* (the embedded list of recognised key ids) updates
  via a separate signed root manifest.

### Why this channel structure

Three channels — **stable**, **beta**, **nightly** — match the
existing release-cadence vocabulary in `docs/SPRINT_PLAN.md`
(public alpha at S12, beta at S22, v1.0 at S24; nightly is
intra-sprint dogfood). A `channel.expires_at` field forces every
manifest to declare its own expiry, so the client can refuse a
manifest from 18 months ago even if a malicious mirror replays
it. This is the **freshness check** TUF documents as essential.

### Why these per-release fields

- **`min_previous_version`** — the client refuses to apply the
  manifest unless it's already at >= this version. Protects
  against skipping a version that had a destructive on-disk
  migration. (We do not yet have any such migration; the field
  is the abstraction so we can introduce one without re-cutting
  the manifest schema.)
- **`rollback_target`** — names the version the rollback command
  reverts to. Usually the previous patch in the same minor; empty
  means "rollback is disabled for this release" (e.g., a security
  fix that explicitly invalidates earlier versions).
- **`mandatory`** — a soft signal the desktop UI uses to nag.
  Mandatory updates don't auto-apply; the user still consents.
  (We do not break the user's running session.)

### Why sha256 + size per artefact

`sha256` binds the manifest to a specific artefact byte-stream;
even if a mirror serves a different file, the verifier rejects it
before unpacking. `size` is for two cheap pre-checks: (a)
disk-space pre-flight before downloading, (b) early abort if a
mirror serves a wildly wrong-length file (cuts off attacker
length-extension attempts at the network layer). Neither
substitutes for the signature; both are belt-and-braces.

### What the parser refuses, what the validator warns about

**Parser errors** (the file is structurally broken — not the
release pipeline's fault, the file isn't a manifest at all):

- `schema` missing or != 1.
- Bad TOML syntax.
- `[release].version` missing or not three dot-separated tokens.
- An `[[artifact]]` table missing `os`, `arch`, `url`, `sha256`,
  or `size`.

**Validator errors** (the file is parseable but unpublishable —
the release pipeline did something wrong):

- `[channel].name` is empty or not one of the three known values.
- Duplicate (os, arch) pair across `[[artifact]]` tables.
- `sha256` is not exactly 64 hex characters.
- `url` is empty or doesn't look like an http(s):// URL.
- `size` <= 0.
- `[signing].algorithm` is not `ed25519` (forward-compat: when
  we add another algorithm, the field tells the verifier which
  curve, but this ADR's v1 manifest is ed25519-only).

**Validator warnings** (the file is publishable but a reviewer
should weigh in):

- `rollback_target` is empty (rollback disabled — usually
  deliberate; warn so it's an explicit choice).
- `min_previous_version` is empty (no floor — fine for a
  greenfield install, but the release pipeline normally sets it).
- `channel.expires_at` is missing (we still publish, but flag —
  the freshness check loses its grip).

The "is this expired *right now*" check is **not** the parser's
or the validator's job; it's the *apply* gate, which runs at
update time when the current wall-clock is meaningful. Pushing
expiry-vs-now into the parser would make unit tests time-bomb
themselves; pushing it into the validator would couple the CI
gate to the CI clock. Both wrong.

## Alternatives considered

### JSON manifest

Pro: smaller deps if a future client (e.g., the desktop app's
Rust side) wants to parse without a TOML library. Con: JSON
manifests are review-hostile at this size (~50 lines per
release), and the desktop app already loads pipeline files via a
Rust TOML parser per `docs/DESKTOP_APP.md` § "On-disk
interoperability". Defer.

### Embedded signature (JWS-style)

Pro: one file to host; the existing JWT-handling library could
verify it. Con: the verifier would canonicalise the TOML on the
fly, which is exactly the surface area that has historically
produced verifier bugs (e.g., the npm 2021 install scripts CVE,
the 2018 SOAP-WS-Security canonicalisation issue). Detached is
boring and right.

### TUF (The Update Framework)

Pro: defence-in-depth against compromised keys, replay attacks,
slow rollback. Con: TUF is a metadata-soup operation requiring
multiple signed roles (root, snapshot, timestamp, targets) and
on-disk caching of each. The full TUF surface is ~6× the LoC of
the design above. We adopt **the parts of TUF that buy concrete
defence here** (per-manifest freshness via `expires_at`; sha256 +
size binding) and skip the parts whose threat model doesn't
match our distribution surface (one downloads domain, one
signing key, no separate snapshot service). If we later operate
multiple mirrors with weaker trust, the TUF migration path is
clean — add the snapshot + timestamp roles, keep this manifest
as the `targets` role.

### Sigstore / cosign / OIDC-bound signatures

Pro: zero-key-management — every signature is bound to a GitHub
Actions OIDC identity at signing time. Con: requires the
verifier to hit Sigstore's Rekor transparency log on every
update check, which is an additional network dependency for the
update path. Defensible long-term direction; we leave the door
open by making the `signing.algorithm` field a string so a future
`sigstore` value can replace `ed25519` without re-cutting the
schema. Not adopted now.

### Platform-native signing only (codesign / signtool / dpkg-sig)

Pro: zero new crypto; OS verifies on launch. Con: only validates
that the file was signed by *our* key — it does not validate
*which version we intended you to install*. A mirror could serve
an older signed binary as a downgrade attack. The manifest's
job is precisely "this version, with this hash, on this date";
platform signing is complementary (we still ship platform-signed
installers — see Sprint 10 push 8's notarisation work) but not
sufficient.

## Consequences

### Positive

- The remaining auto-updater pushes (5–8) all key off the
  shapes in this ADR, so reviewer attention can focus on the
  *behaviour* (state machine, rollback log, kill-switch) rather
  than re-relitigating the data model.
- The format is small and reviewable. A release sign-off looks at
  ~50 lines and can mentally compare them against the build
  artefacts in CI.
- The parser is testable without crypto. CI exercises every
  parser/validator rule without needing key material.

### Negative

- We are committing to a TOML manifest format we'll need to keep
  parseable across the v1 lifetime (S12 public alpha → S24
  v1.0). Schema bumps require a coordinated client-side update.
  Mitigated by the `schema = 1` discriminator: a future
  `schema = 2` is a known unknown, not a breaking change.
- The detached-signature design means update clients fetch *two*
  files per check (manifest + signature) instead of one. The
  bandwidth cost is negligible (64 bytes per check), but the
  latency cost is one extra HTTP request. Acceptable.

### Risks

- **Signing key compromise.** This is the catastrophic failure
  mode. Mitigated by (a) keys live in an HSM offline from the
  build infrastructure (per `docs/SECURITY.md` § Release
  signing); (b) `public_key_id` lets a compromised key be
  rotated without re-cutting the schema; (c) the `expires_at`
  freshness window caps the blast radius of any single
  compromised manifest. Residual risk: an attacker who gets a
  manifest signed *during* the compromise window still has up
  to `expires_at` to serve it. This is the standard
  signed-manifest risk and matches every other auto-updater on
  the market.
- **Sigstore migration costs surface area.** If we adopt
  Sigstore at some future point, the verifier gains a network
  dependency. The schema is flexible enough to express it; the
  *implementation* impact is in the verifier module that
  doesn't yet exist. Cheap to defer.
- **Replay-after-rollback attack.** An attacker who can MITM the
  download could serve an old (but still-in-its-expiry-window)
  manifest to prevent the user from upgrading past a known-
  vulnerable version. Mitigated by the client recording the
  highest version it has ever seen and refusing to "upgrade"
  downward; this lives in the state-machine push (5), not here.

## Pre-mortem (one year from today)

It is 2027-05-11. The auto-updater has shipped a year of
releases; what went wrong?

**Most likely:** schema v1 turned out too narrow for some
release-pipeline detail we didn't anticipate (most plausibly:
multiple artefacts per (os, arch) — say, a slim vs. fat binary,
or a separate symbol-bundle). The fix is additive: a new
optional field `variant = "default"` on `[[artifact]]`, and the
parser ignores it when reading older clients. No schema bump
needed.

**Next-most likely:** we underestimated the operational cost of
running our own key, and we migrate to Sigstore. The
`signing.algorithm = "sigstore"` value slots into the existing
field; the verifier picks the right path. Manifest schema
unaffected.

**Less likely:** a real downgrade-attack incident forces us to
add TUF snapshot + timestamp roles. The migration is
constructive — add two new TOML files (`snapshot.toml`,
`timestamp.toml`) signed by separate roles; this manifest stays
as-is and becomes the `targets` role. The migration path was
the deciding factor in not adopting full TUF on day one.

## References

- `docs/SPRINT_PLAN.md` § Sprint 10 — the auto-updater XL story.
- `docs/DESKTOP_APP.md` § Update protocol — the desktop app's
  consumer of this manifest (push 6+).
- `docs/SECURITY.md` § Release signing — key storage and
  rotation procedure (separate document, evolves with this).
- The Update Framework — https://theupdateframework.io (read
  for the parts we adopted and the parts we deferred).
- TUF spec § 5 — the threat model we cribbed from when listing
  the freshness check, rollback protection, and per-artefact
  hash binding.
- libsodium ed25519 reference —
  https://libsodium.gitbook.io/doc/public-key_cryptography/public-key_signatures
  (verifier implementation in push 5 wraps this).

## History

- 2026-05-11 (Sprint 10 push 4): **this ADR** — manifest format
  + signing scheme accepted; parser + validator land in this
  push; verifier implementation lands in push 5; state machine
  in push 6; rollback in push 7; notarisation in push 8.
