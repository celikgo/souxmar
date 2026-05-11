# ADR-0016: BridgeFeatureSet contract for the desktop ⇄ C++ FFI boundary

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 12 push 2)
- **Author:** souxmar desktop + platform teams
- **Deciders:** desktop, platform, AI (the three layers that will
  consume this from React)
- **Tier:** 2 (the BridgeFeatureSet *struct shape* is on a stable-
  contract path; adding fields is non-breaking, renaming/removing
  is Tier-2)
- **Affects:** `src/desktop/src-tauri/souxmar-bridge/` (new crate);
  `src/desktop/src-tauri/src/commands.rs` (`bridge_feature_set`
  Tauri command); `src/desktop/src/tauri/bridge.ts` (mirror
  interface); every workbench panel's render path.

## Context

The Sprint 10 push 10 onboarding scaffold + Sprint 11 push 4
workbench scaffold landed three desktop panels (viewport / inspector
/ chat) that each depend on a libsouxmar-* surface not yet
callable from the Rust side. Each panel today renders an honest
"Sprint 12+" empty state behind an *implicit* boolean — the
component's `if` decides "scaffolding or real" by looking at the
panel's local data, not at a shared signal.

The Sprint 11 retro flagged this:

> Without a named contract for "what does Tauri-bridge-to-FFI
> return today and what does it return at v1.0?", each one will
> reinvent its own scaffolding-vs-real toggle.

That's already happening — three panels with three slightly
different "is this wired?" patterns. By Sprint 14 (managed-AI
proxy) there will be five or six. The maintenance cost of
divergent scaffolding-vs-real plumbing scales worse than the cost
of a one-time coordinated contract.

This ADR ratifies the contract: **`BridgeFeatureSet`** — a single
struct returned by the Rust bridge at app startup, naming which
surfaces are wired to real libsouxmar-* calls. Each panel queries
the matching flag and renders accordingly. New panels opt in by
adding a flag; old code never has to guess at availability.

## Decision

### The struct

Defined in `src/desktop/src-tauri/souxmar-bridge/src/lib.rs` as a
plain serde-serializable Rust struct:

```rust
pub struct BridgeFeatureSet {
    pub viewport_renderer:        bool,
    pub pipeline_introspection:   bool,
    pub provider_call:            bool,
    pub keychain_write:           bool,
    pub auto_updater_menu:        bool,
    pub bridge_protocol_version:  u32,
}
```

Mirrored on the React side at `src/desktop/src/tauri/bridge.ts` as
the matching TypeScript interface. The React mirror is the spelling
load-bearing for consumers; the Rust struct is the source of truth.
A `bridge_feature_set` Tauri command exposes the Rust value to the
React side; a `useBridgeFeatures()` hook caches the result for the
lifetime of the React tree.

### Field-naming conventions

Each field is **noun-shaped** (`viewport_renderer`, not
`use_viewport_renderer`) and names the *surface* the flag controls.
The Boolean default at build time is the conservative one: **off** if
the surface depends on libsouxmar-* FFI that isn't wired yet, **on**
only if the surface is fully implemented through Rust-only paths
(`keychain_write`, today's only example).

### Stability

| Change                                  | Tier |
| --------------------------------------- | ---- |
| Adding a field                          | 0 — non-breaking; React mirror gets `undefined` until both sides ship the new flag |
| Renaming a field                        | 2 — requires a deprecation cycle (add the new spelling alongside the old, switch consumers, remove the old) |
| Removing a field                        | 2 — same |
| Changing a field's semantics            | 2 — the contract is what the flag *means*; rebadging is a breaking change |
| Bumping `bridge_protocol_version`       | accompanies any Tier-2 change; reset documents the new wire format |

This is the same shape as the manifest format contract from
ADR-0013: schema-discriminated, additive-by-default, ratchets are
explicit.

### Default + override

The Rust `BridgeFeatureSet::default()` returns the conservative
all-off state (except `keychain_write`). Individual surfaces flip
their flag *only* in the same push that lands the wiring. The
release CI workflow asserts `provider_call && pipeline_introspection`
are true for stable release builds — a build that ships with both
flags off is unfit for stable use, regardless of how the rest of
the binary looks.

### React-side consumption

The workbench mounts `useBridgeFeatures()` once; each panel
receives the resulting struct as a prop. **No panel calls the
Tauri command directly.** This rule lets the future managed-AI-
proxy / cloud-sync scaffolds inject a different feature set from
unit tests + Playwright specs without each panel knowing it's
under test.

The fallback `BridgeFeatureSet` (when the Tauri runtime is absent,
e.g. `vite preview` / Playwright) lives at
`src/desktop/src/tauri/bridge.ts::fallbackFeatureSet` and mirrors
the Rust default verbatim. Renaming a field requires updating
**both** the Rust struct and the React fallback, in the same
commit; CI lint can enforce this once the desktop build runs in
CI (Sprint 13+).

## Alternatives considered

### Per-panel toggles

Status quo as of Sprint 11. Each panel decides "wired or not" from
its local state. Pros: zero up-front design; each panel evolves
independently. Cons: five panels duplicating the toggle pattern
slightly differently; no central audit of "what's wired"; a
managed-AI-proxy push that flips three panels' real-mode would
have to touch each panel's bespoke condition. Rejected as the
Sprint 11 retro explicitly flagged.

### A struct per feature

`{viewport: {wired: bool, version: u32}, pipeline: {wired: bool,
…}, …}`. Pros: room to grow per-feature flags (rate limits,
endpoint URLs). Cons: the React-side TS types become deeply
nested for what's still a "yes/no" question at v0.9.0; the
add-a-field migration becomes a per-feature schema bump. The
Sprint 13+ "managed-AI proxy" push *may* warrant a per-feature
struct (the proxy needs endpoint URLs + rate limits); when it
lands, we add a sibling `BridgeProviderConfig` struct, leaving
`BridgeFeatureSet` for the simple Boolean queries.

### Environment-variable-driven feature flags

E.g. `SOUXMAR_FEATURE_VIEWPORT=1`. Pros: zero code on the Rust
side. Cons: requires the user (or installer) to set env vars
correctly; CI runs and end-user builds diverge; impossible to
verify at compile time. Rejected.

### Git-tag-driven feature flags

The bridge protocol version comes from `git describe --tags`. Pros:
ties feature flags to releases automatically. Cons: dev builds
have ambiguous flags; the release pipeline's tag-to-flag mapping
is implicit. Rejected — explicit is better.

## Consequences

### Positive

- One source of truth for "what's wired in this build." Five
  panels stop reinventing the toggle.
- New surfaces opt in via a one-line field addition + a
  one-line `if` in the consuming panel. Linear scaling.
- The release CI gate can grep for `provider_call: true` in the
  built binary's startup log — a deterministic publishability
  check.
- Unit + Playwright tests can inject any BridgeFeatureSet without
  needing the Tauri runtime, so "what does the viewport look like
  with viewport_renderer on but pipeline_introspection off?" is
  one fixture line.

### Negative

- A new struct shape to maintain across two language boundaries.
  Mitigated: small (6 fields today; expected to grow ~1-2/sprint),
  obvious mirror requirement, CI lint enforceable.
- The struct can become a junk drawer of "remember to check this
  flag." Mitigated: the per-feature struct alternative (above) is
  the planned escape hatch when individual surfaces grow non-
  Boolean configuration.

### Risks

- **Flag-set rot.** A wired feature whose flag stays false in
  the default. Mitigated: each wiring push is required to flip
  the matching flag in the same commit; PR review catches the
  inverse case.
- **Mirror drift.** Rust struct gains a field; React mirror
  doesn't. Mitigated: Sprint 13+ adds a lint task that
  `cargo expand`s the BridgeFeatureSet declaration and grep-
  compares against the TS interface.

## Pre-mortem (one year from today)

It is 2027-05-11. The contract has been live for a year; what
went wrong?

**Most likely:** we add a non-Boolean feature (managed-AI proxy's
endpoint URL + rate-limit window) that doesn't fit. The escape
hatch is a sibling struct (`BridgeProviderConfig`, etc.) returned
by a parallel Tauri command. `BridgeFeatureSet` stays Boolean;
the contract isn't violated.

**Next-most likely:** the React-side fallback drifts from the
Rust default. A Playwright spec passes locally with the fallback
but fails against the real Tauri runtime in CI. The Sprint 13+
lint task catches this; until then a careful PR review covers it.

**Less likely:** the `bridge_protocol_version` field becomes
load-bearing for runtime negotiation between an updated bridge
crate and an older React build (e.g., during an in-place
auto-update). If that scenario materialises, the version field
gates the surface-level features by major-version bump; small
extension, doesn't break the contract.

## References

- Sprint 11 retro § "One ADR-worthy decision surfaced" — the
  decision this ADR ratifies.
- `docs/DESKTOP_APP.md` § "Async / IPC" — the broader IPC story;
  this ADR is one corner of it.
- ADR-0013 § "Why this channel structure" — the schema-
  discriminated additive-by-default pattern this ADR borrows.

## History

- 2026-05-11 (Sprint 12 push 2): **this ADR** — BridgeFeatureSet
  contract accepted; `souxmar-bridge` skeleton crate lands with
  `viewport_renderer`, `pipeline_introspection`, `provider_call`,
  `keychain_write`, `auto_updater_menu`, `bridge_protocol_version`
  fields. Default is all-off except `keychain_write`. React
  fallback in `bridge.ts::fallbackFeatureSet` mirrors verbatim.
