# ADR-0023: Plugin marketplace v1 — paid-plugin hosting

- **Status:** Accepted
- **Date:** 2026-05-14 (Sprint 16 push 2)
- **Author:** souxmar platform team
- **Deciders:** platform, AI, billing, security review
- **Tier:** 1 (architecture)
- **Affects:** new `services/plugin-marketplace/`; ratchet of the
  plugin-index format from Sprint 10 push 2; new
  `license_key_required` field in plugin entries; Sprint 16 push 3's
  Stripe POC integration; Sprint 16 push 4's `souxmar plugin install`
  ratchet.

## Context

Sprint 10 push 2 stood up the plugin index (`plugin-index.json`,
free plugins only). Sprint 10 push 3 wired the publication
workflow. Sprint 16 (this sprint) ratchets to v1: paid plugins
join the index, the marketplace hosts the storefront, and the
desktop's `souxmar plugin install` runs through a
license-verification step before download.

## Decision

### Index entries gain optional fields

```json
{
  "id": "com.acme.surface-mesher-pro",
  "version": "1.2.3",
  "manifest_url": "https://marketplace.souxmar.dev/plugins/<id>/manifest.toml",
  "signature_url": "https://marketplace.souxmar.dev/plugins/<id>/manifest.toml.sig",
  "license_key_required": true,
  "price_usd_one_time": 99.00,
  "price_usd_monthly":   12.00,
  "publisher": "com.acme",
  "publisher_verified_at": "2026-05-14T12:00:00Z"
}
```

- `license_key_required` defaults to `false` (free plugin). When
  `true`, `souxmar plugin install` requires a valid
  `sxm_lic_<...>`-prefixed license key.
- Both `price_usd_one_time` and `price_usd_monthly` may be
  present; the storefront shows both options.
- `publisher_verified_at` is the timestamp at which the
  publisher's identity was verified (DUNS, GitHub org, or
  manual). Absent → unverified publisher; the storefront shows
  a warning.

### License keys

- Format: `sxm_lic_<32-hex-bytes>`. Different namespace from
  `sxm_pro_*` AI tokens (ADR-0019).
- Issued at purchase by the Stripe webhook + the marketplace
  service. Stored in the user's OS keychain alongside BYOK
  keys, namespaced as `souxmar.license.<plugin-id>`.
- Validated by the marketplace service's `POST /v1/license/check`
  endpoint at install time. Returns 200 + a signed entitlement
  receipt (ed25519 over plugin-id + user-id + expiry); the CLI
  caches the receipt so re-installs don't require online check.

### Plugin signature still mandatory

Paid plugins follow the same signing requirement as free
plugins (ADR-0008 / ADR-0013 — ed25519 detached signature
over the manifest). The marketplace stores the signature; the
client verifies before extracting. Paid != skipping the
security gate.

### Takedown procedure

A plugin can be removed from the index for:

- Publisher request (legitimate sunsetting).
- Verified security incident (the plugin's manifest is
  pulled; the signature stays valid for already-installed
  copies but new installs refuse).
- Legal / DMCA takedown (handled by the platform team via the
  account portal's admin surface — Sprint 17+).

In all three cases, the index publication CI emits an `404
removed` for the plugin's manifest URL + a `tombstone` entry
in `plugin-index.json` for one full release cycle (so clients
that cache the index see an explicit "this is gone" rather
than a silent 404).

### Revenue split

- **70/30 publisher/platform** for paid plugins. The 30%
  covers Stripe fees, signing infrastructure, hosting
  bandwidth, takedown adjudication.
- Stripe takes its cut from the platform's 30%, not the
  publisher's 70%.
- Free plugins: no revenue, no platform cut.

The split itself is a business-model decision codified in
`BUSINESS_MODEL.md` § Pricing; the ADR notes it because it's
load-bearing for Sprint 16 push 3's Stripe integration shape.

## Consequences

- New `services/plugin-marketplace/` directory (axum binary,
  v1 API at `marketplace.souxmar.dev`).
- The existing `plugin-index.json` schema gains optional
  fields (additive Tier-0 — old clients ignore them and see
  only free plugins).
- Sprint 16 push 4 ratchets `souxmar plugin install` with
  `--license <key>` + the entitlement-receipt cache.
- Sprint 17's account portal surfaces the user's purchased
  plugin keys.

## Risks

- **R-025 (paid-plugin signing key custody).** Publisher
  keys are not in the platform's HSM — publishers manage their
  own. A compromised publisher key can sign a malicious update.
  **Mitigation:** the marketplace records the publisher's
  *last-known* signing key fingerprint; an unexpected key
  rotation triggers a manual review before publication. **Likelihood:
  Med (over a year); Impact: High.**
- **R-026 (license-check unavailability).** If
  `marketplace.souxmar.dev` is down, paid plugin re-installs
  can't validate. **Mitigation:** entitlement receipts cache
  in the OS keychain with a 90-day expiry; re-validation only
  blocks after expiry. **Likelihood: Low; Impact: Low.**
- **R-027 (chargeback abuse).** A user purchases a paid
  plugin, downloads it, files a chargeback. The license is
  revoked but the .so is on their disk.
  **Mitigation:** the entitlement-receipt-with-expiry model
  means after 90 days the plugin refuses to load. The
  publisher is paid out only after a 14-day chargeback window.

## Related ADRs

- ADR-0008 (plugin ABI v1 final freeze) — paid plugins
  follow the same ABI.
- ADR-0013 (signed update manifest) — paid plugin manifests
  follow the same signing surface.
- ADR-0019 (managed-AI proxy) — shares the Stripe customer
  record across AI + marketplace billing per Sprint 16 push 3.

— Sprint 16 push 2.
