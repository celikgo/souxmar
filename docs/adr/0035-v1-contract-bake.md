# ADR-0035: v1.0 contract bake — what's final-final at v1.0.0

- **Status:** Accepted
- **Date:** 2026-05-21 (Sprint 23 push 1)
- **Tier:** 1 (release engineering)
- **Affects:** every Tier-2 contract in the project — the plugin
  C ABI, the agent tool contract, the on-disk pipeline format,
  the BridgeFeatureSet shape, the service OpenAPI shapes, the
  CLI `--json` schemas.

## Context

Sprint 22's v0.99-beta1 opened the public beta. Sprint 24 cuts
v1.0.0. The v1.0 freeze is the last opportunity to ratchet any
of the project's Tier-2 contracts before the project is
committed to backward compatibility for the v1.x line.

ADR-0035 (this push) names what is final-final at v1.0.0 +
what's allowed to evolve under additive-Tier-0 rules from then on.

## Decision

### Final-final at v1.0.0

- **Plugin C ABI v1.3** — frozen FINAL since ADR-0008 (Sprint 5)
  + ADR-0012 (Sprint 10 face-tag ratchet). No further changes
  pre-v1.0. Post-v1.0 additions require a v2 ABI (separate
  surface).
- **Agent tool contract v1** at 18 tools — frozen FINAL since
  ADR-0011 (Sprint 7). No tool additions; no tool input/output
  schema bumps. Post-v1.0 additions require a v2 contract.
- **On-disk pipeline format v1** (the `version: 1` discriminator
  in pipeline YAML) — frozen FINAL. Additive Tier-0 fields stay
  allowed; renaming/removing requires v2.
- **Update manifest format v1** (schema=1) — frozen FINAL.
  Additive only.

### Tier-0 evolution allowed for v1.x

- **BridgeFeatureSet** — adding a field stays Tier-0 (ADR-0016).
  The protocol-version bump alongside.
- **Service OpenAPI shapes** (proxy, marketplace, billing,
  cloud-sync, account-portal, compute-offload) — additive
  fields Tier-0; new endpoints Tier-0; renaming/removing
  requires v2.
- **CLI `--json` output shapes** — additive fields Tier-0 (per
  ADR-0025 schema=1 + kind discriminator); the
  `souxmar-bridge::cli_shapes` Rust mirror absorbs them with
  `#[serde(default)]`.
- **`project.ai.toml` schema** — additive fields Tier-0;
  renaming requires schema=2.

### What gets one last ratchet pre-v1.0

Sprint 23's RC hardening reviews:

- Eval suite — does it still pass 90% on the v1.0 candidate
  binary? (Today it does; the RC build re-confirms.)
- Performance — perf-nightly's tracked benchmarks at v0.99 vs.
  v1.0-candidate. >5% regression blocks the cut per the
  cross-cutting "perf gate" commitment.
- Determinism — the cantilever golden runs on the RC binary
  before tag.

### Stripe billing flips live

This sprint also flips the billing service from test mode to
live mode (ADR-0024's mode/key consistency check enforced).
First real Pro-tier subscription transactions happen during
v0.99-rc1 → v1.0.0 window.

## Consequences

- ABI / tool / pipeline / manifest contracts entered "do not
  touch" status this sprint. Any change post-v1.0 requires
  a v2 surface.
- Service contracts can evolve through v1.x via additive-only.
- The RC cut (`v0.99-rc1`) is the bake; if any
  pen-test/bug-bounty critical finding requires a contract
  change, the v1.0.0 cut slides.

## Risks

- **R-044 (last-minute contract bug).** A pen-test finding in
  the final week of beta that requires a contract change
  forces a v2 surface immediately (instead of a clean
  v1.x → v2 break later). **Mitigation:** Sprint 24 push 1's
  v1.0 cut blocks on any open critical pen-test finding.

— Sprint 23 push 1.
