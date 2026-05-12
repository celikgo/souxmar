# ADR-0032: End-to-end encrypted cloud sync (Enterprise tier)

- **Status:** Accepted
- **Date:** 2026-05-18 (Sprint 20 push 1)
- **Tier:** 1 (architecture)
- **Affects:** `services/cloud-sync/` (Enterprise tier branch);
  the desktop client's "synced projects" UI (Sprint 22+
  surface).

## Decision

Enterprise-tier cloud sync ships **end-to-end encrypted**:

- The desktop client generates a per-org **master key** in the
  user's OS keychain at the first sync setup. The master key
  never leaves the device.
- Per-project file uploads are AES-256-GCM-encrypted **client-
  side** with a per-project key derived via HKDF from the
  master key. The server stores ciphertext only.
- Per-org master keys are wrapped via the org admin's
  recovery key (configured during Enterprise onboarding). Lost
  device → admin re-encrypts via the recovery key; no other
  recovery path.

### Compared with Pro/Team rest-only encryption

Pro / Team tier encryption (ADR-0021): the server holds keys
in AWS/GCP KMS; the server can read plaintext. Trust model
includes the operator.

Enterprise tier E2E: trust model excludes the operator. The
server holds ciphertext; the operator cannot read plaintext
even with full database access.

### Why per-project derived keys (not one key per org)

- Per-org would let one compromised project propagate to all
  others.
- Per-project keys cost zero storage (derived on demand);
  the master is the only persisted secret.

### Recovery model

- **Single device + lost.** The user signs in on a new device
  with their SSO/SCIM identity; the org admin re-encrypts
  the master key with the new device's public key. One-time
  manual step.
- **Org admin's recovery key lost.** The org's data is
  unrecoverable. Documented as such in the onboarding flow.

## Consequences

- `services/cloud-sync/` gains a `tier == "enterprise"` branch
  that refuses any operation requiring plaintext (search,
  diff). Sprint 22+ wires.
- The Sprint 17 push 4 retro's "synced project history is the
  audit trail" assumption breaks for Enterprise — the server
  can't read history. Per-project audit logs live on the
  client, signed by the client.

## Risks

- **R-040 (E2E + cross-platform key portability).** The user's
  Linux desktop, macOS laptop, and Windows desktop all need
  the master key. OS keychain export → manual paste on each
  device, or a recovery-key-wrapped export to the org admin's
  portal. **Mitigation:** the recovery-key-wrapped export
  path is documented at Enterprise onboarding.

— Sprint 20 push 1.
