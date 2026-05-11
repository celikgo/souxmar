---
name: publishing-plugin-marketplace
description: Use when publishing a souxmar plugin to either the open plugin index (free) or the paid marketplace. Covers manifest validation, conformance badge process, signing, listing PR, and (for paid) Stripe integration. Triggers on "plugin marketplace", "publish plugin", "plugin index", "list a plugin", "sell a plugin".
---

# Publishing a souxmar plugin

souxmar maintains two distribution channels for plugins:

1. **Open plugin index** — free, OSI-licensed plugins. Listed in `docs/plugin-index.md`. Source links only; we do not host binaries.
2. **Paid marketplace** — commercial plugins. We host binaries, handle Stripe billing, issue licenses, run conformance in CI. Author keeps 90 % of revenue. Detailed in `docs/BUSINESS_MODEL.md`.

This skill covers both channels.

## When to use this skill

- A plugin author wants their plugin discoverable.
- A commercial author wants to sell a plugin.
- A maintainer is reviewing a plugin index PR.
- A plugin author needs to refresh the conformance badge after a release.

## When NOT to use this skill

- The plugin is internal-only (private use; no public listing intended).
- The plugin is in-tree in the souxmar repo (those are listed automatically; no marketplace PR needed).

## Open plugin index publishing

### Eligibility

- Plugin must be open-source under an OSI-approved license.
- Source must be publicly hosted (GitHub, GitLab, Codeberg, self-hosted Gitea — all fine).
- Manifest must be valid and `abi` field must match a supported souxmar major version.

### Process

1. **Run conformance locally** to catch issues before review:
   ```bash
   souxmar-conformance --plugin ./libmy_mesher.so --capability mesher.tetra.example
   ```
   Aim for the conformance badge but it is not required for listing.

2. **Open a PR against `docs/plugin-index.md`** with an entry:
   ```markdown
   ### Example Tetrahedral Mesher
   - **id:** `com.example.my-mesher`
   - **capabilities:** `mesher.tetra.example`
   - **license:** Apache-2.0
   - **source:** https://github.com/example/my-mesher
   - **author:** Example Inc. (https://example.com)
   - **souxmar versions:** 1.0+
   - **conformance:** ✓ (run 2026-05-11) | not yet | failed (link)
   - **status:** active | maintained | unmaintained | archived
   ```

3. **DX team reviews** for completeness. We do not vet code or behaviour. The badge is the only quality signal.

4. **Merged PR** triggers a regen of the searchable index (`souxmar plugin search` queries this).

### Conformance badge

For the badge:

1. CI in the plugin's own repo runs `souxmar-conformance` on every release.
2. The CI run uploads a signed conformance attestation.
3. The badge in the index links to the latest passing attestation.

A passing attestation older than 6 months auto-expires the badge until refreshed. This catches abandoned plugins that no longer compile against current souxmar.

## Paid marketplace publishing

The paid marketplace is launched at Sprint 16 per `docs/SPRINT_PLAN.md`. Pre-launch authors can pre-register via the form linked from `docs/BUSINESS_MODEL.md`.

### Eligibility

- Plugin must pass conformance.
- Author must agree to the Marketplace Terms (separate document; covers liability, refund policy, data handling).
- Author must complete Stripe Connect onboarding (we use Connect Express; takes 5–10 min).

### Process

1. **Apply via the marketplace dashboard** — submit plugin id, manifest, pricing model (per-seat / per-license / subscription), and the binary.
2. **Marketplace CI runs conformance** against your binary on Linux/macOS/Windows in the souxmar test rig. Pass = listing publishes.
3. **License key flow:** the marketplace issues per-purchase license keys. Your plugin binary calls `souxmar_license_check_v1(license_key, capability_id)` from libsouxmar to validate. The validation is offline-friendly (cryptographically signed JWT licenses).
4. **Pricing models supported:**
   - One-time purchase per seat.
   - Annual subscription (auto-renew, cancellable any time).
   - Floating concurrent-use license (Enterprise; manual setup).
5. **Revenue:** Stripe transfers monthly; 90 % to author, 10 % to souxmar Inc. for hosting + payment infrastructure.

### Updates

- New plugin versions are uploaded via the dashboard.
- Conformance re-runs on upload.
- Existing license-key holders auto-receive the update (subject to your subscription/version policy).
- You can deprecate older versions; we keep them downloadable to existing licensees indefinitely (Apache-2.0 ethos extends to paid plugins: customers do not lose access to what they paid for).

## What we will not do

- Vet plugin behaviour beyond conformance. The badge is the only quality signal. Authors are responsible for what they ship.
- Exclude plugins that compete with our managed services. A third-party "managed-AI alternative" plugin is fair game.
- Apply DRM beyond the license-key check. Authors who want stronger anti-piracy can implement it; we will not.
- Resell or sublicense plugins. We are a distributor, not a reseller.
- Mediate refund disputes beyond our standard 14-day refund policy.

## Common mistakes

- Submitting a plugin without conformance. Even for the open index, run it first to catch the obvious issues.
- Manifest `abi` field set to a version that does not exist. Check `souxmar version --abi`.
- Pricing without a clear value proposition. The marketplace shows similar plugins side-by-side; price like a product, not a side gig.
- Forgetting to re-list after a major souxmar version. ABI v2 plugins must be a separate listing from ABI v1; the index distinguishes them.
- Using the open index to advertise a paid plugin. The open index is for OSS plugins; paid plugins go in the marketplace channel.
- Hiding the source location for an open-index plugin. The source link is the audit trail; without it, the listing is rejected.

## Reference

- `docs/PLUGIN_SDK.md` — plugin contract.
- `docs/BUSINESS_MODEL.md` — marketplace economics, revenue split, what we will and won't do.
- `docs/GOVERNANCE.md` — plugin index governance.
- `docs/plugin-index.md` (when present) — current listings.
