# ADR-0014: Release signing key rotation procedure

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 10 push 8)
- **Author:** souxmar platform team
- **Deciders:** platform, security review, release engineering
- **Tier:** 2 (procedural — ratifies the rotation cadence the
  auto-updater's threat model assumes; touches `docs/SECURITY.md`,
  `scripts/release/*`, and the release CI workflow)
- **Affects:** `src/updater/embedded_trust.cpp`, the release CI
  workflow, every release built after this ADR's date.

## Context

ADR-0013 § "Why ed25519, detached signature" picked ed25519 for
manifest signing and § "Signing key compromise" enumerated the
residual risk: an attacker who gets a manifest signed during a
compromise window can serve it until that manifest's
`channel.expires_at`. The mitigation listed in 0013 was "keys live
in an HSM offline from the build infrastructure" and "rotation =
separate coordinated event". This ADR makes both concrete.

The rotation procedure is the load-bearing detail. A botched
rotation breaks the auto-updater for every user, because clients
trust the manifest only when its `signing.public_key_id` is in their
embedded trust store. A new key id with no client knowing about it
is a denial-of-service.

The procedural choices below are the ones that need to be locked in
once and never re-litigated per rotation:

1. The cadence.
2. The trust-store update window (how long between embedding the
   new key in clients and using it to sign).
3. The emergency-rotation collapse rule (compromise detected — what
   collapses, what stays).
4. Old-key destruction policy.

## Decision

### 1. Cadence

**One year per key.** Key id is the calendar year (`release-2026`,
`release-2027`, etc.). Yearly rotation balances:
* Long enough that the procedure is rehearsed often enough to stay
  smooth (vs. quarterly which becomes routine but invites mistakes).
* Short enough that any single key's blast radius is bounded.
* Matches the lifetime of the EV Authenticode cert (yearly renewal)
  and the Apple Developer ID enrollment (yearly).

A single-year window means at any given time the trust store
contains the *current* key plus an *overlap window* covering recent
clients still on the previous key — typically a two-week overlap.

### 2. Trust-store update window (T-60 / T-30 / T+0)

The window comes from ADR-0013's apply-time freshness rule. Every
manifest carries an `expires_at` (typically 90 days from issue).
Clients refuse a manifest past expiry. So:

- **T-90:** generate the new keypair in the HSM. **Do not** sign
  anything with it. Open a tracking issue.
- **T-60:** merge a PR that bumps
  `SOUXMAR_RELEASE_PUBKEY_HEX` / `SOUXMAR_RELEASE_PUBKEY_ID` to the
  next year's values. The release built from this PR still signs
  with the *current* key — only its embedded trust store now
  recognises the future key in addition. This is the
  "trust-flag day".
- **T-30:** every user who has run `souxmar update check` since
  T-60 has now received the trust-flag release. We are clear to
  start signing future manifests with the new key.
- **T+0:** old key destroyed in the HSM. Public key stays archived
  under `docs/releases/release-YYYY.toml` for auditor reproducibility.

A user who hasn't run an update check in 60 days does NOT silently
fall off the upgrade path — their client refuses the new-key
manifest with `SignatureStatus::UnknownKeyId`, the apply gate
records this in the rollback log, and the desktop app surfaces "your
trust store is out of date; reinstall from souxmar.dev to receive
future updates."

### 3. Emergency rotation

If the current key is compromised, the T-60 → T+0 window collapses:

1. **Within 4 hours of detection.** A hotfix release is cut.
   This release:
   * Embeds the new key id + pubkey hex.
   * Carries a `revoked_key_ids = [...]` field listing the
     compromised key. (The manifest format gains this field
     additively — schema discriminator means older clients
     ignore it cleanly.)
   * Is itself signed by the new key.
2. **Within 24 hours.** The compromised key's public_key_id is
   added to a stop-list distributed via the next manifest's
   `revoked_key_ids`. Clients update their local stop-list on
   every successful `update check`. A future manifest signed by a
   stopped key is refused with a new SignatureStatus value
   (`RevokedKey`, added with this emergency procedure).
3. **Within 7 days.** A public post-mortem at
   `docs/incidents/<date>-signing-key-rotation.md` per § "Reporting"
   of `docs/SECURITY.md`.

The emergency procedure deliberately requires shipping a new client
build — there is no in-band "trust this new key now" path. The
attacker's window is therefore bounded by:
* `channel.expires_at` of the most recent manifest signed by the
  attacker (≤ 90 days).
* The hotfix release's uptake rate (the auto-updater's normal
  feedback loop, expected to be ≥ 80% within 7 days based on
  comparable open-source projects).

### 4. Old-key destruction

T+0: HSM operator zeroises the old private key under
four-eyes principle (two release engineers + a recorded audit
event). The public half stays archived for the lifetime of the
project so a future auditor can verify historical signatures from
the project's release-NNNN era.

## Alternatives considered

### In-band trust-store updates (TUF root-rotation style)

Pro: clients adopt new keys without taking a fresh release. Con:
adds a substantial protocol surface (signed root manifest, multi-
sig threshold for the root role) and a recovery scenario when the
root itself is compromised. The user populations for which this
matters (high-frequency-update infrastructure) don't match souxmar's
usage (engineering desktop tool, monthly-ish update cadence).
ADR-0013 already noted TUF as defensible-but-deferred; this ADR
inherits that decision.

### Quarterly rotation

Pro: tighter blast radius per key. Con: four times the procedural
overhead, four times the chance of an off-by-month mistake.
Yearly + an emergency-collapse procedure gives most of the benefit
at a fraction of the cost.

### Sigstore / OIDC-bound signatures

Pro: zero key management for the project. Con: introduces a
Rekor-transparency-log network dependency at update-check time;
adds an external trust root we can't audit. The schema deliberately
keeps `signing.algorithm = "ed25519"` as a forward-compat field
(ADR-0013); a future ADR can change it to `"sigstore"` without
re-cutting the manifest version.

## Consequences

### Positive

- Release engineers have an unambiguous yearly procedure with
  pre/post dates pinned to ADR-0013's freshness window. No
  per-rotation re-design.
- The emergency-collapse procedure is documented before the first
  incident, not during one.
- The four-eyes destruction step is auditable; the archived
  public keys are reproducible.

### Negative

- A user who lets their client go > 60 days without an update
  check will fall off the upgrade path until they reinstall.
  Mitigation: the desktop app surfaces a banner once `last_check_at`
  is ≥ 30 days old; the CLI prints a warning at each successful
  check that `last_check_at` is approaching the trust-store-stale
  window.

### Risks

- **HSM operator error during the four-eyes destruction.** Mitigated
  by requiring the archive of the public key first (so an audit
  can still chain back to the destroyed key); and by the
  emergency-rotation procedure being viable even if the old key's
  destruction is botched (the new key is the source of truth from
  T+0 onward).

## References

- ADR-0013 § "Pinned key id, separate rotation event" + § "Signing
  key compromise" — the threat model this procedure operationalises.
- `docs/SECURITY.md` § "Release signing" — operational counterpart.
- `scripts/release/sign-manifest.sh` — the script the release
  engineer runs during a rotation event.
- TUF spec § 6 ("Repository setup") — the root-key handling we
  cribbed from when designing the four-eyes destruction step.

## History

- 2026-05-11 (Sprint 10 push 8): **this ADR** — yearly rotation
  cadence, T-90/T-60/T-30/T+0 procedure, emergency collapse rule,
  four-eyes destruction step.
