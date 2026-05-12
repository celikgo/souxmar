# Sprint 13 retro — First post-v0.9.0 sprint: ADR-0017 + ADR-0018 + first real FFI + carry-overs cleared

**Closed:** 2026-05-12. **Pushes:** 5. **Theme:** "the first sprint
*after* the public alpha — ratify the model, land the first real
FFI, drain the carry-over list."

Sprint 12 cut v0.9.0 by getting the public surface stood up — docs
site, triage workflow, souxmar-bridge skeleton. Sprint 13 is the
first sprint where the day-to-day work shifted from "stand up new
infrastructure" to "exercise the infrastructure with real
deliverables." This shift is the meaningful one — the velocity-
model question Sprint 12 left open gets a concrete data point this
sprint.

## What landed

| Push | Deliverable                                                                                              | Lines | Frozen-header impact |
| ---- | -------------------------------------------------------------------------------------------------------- | ----- | -------------------- |
| 1    | **ADR-0017 (public-alpha bug-discovery model) + synthetic-load harness.** Picks Option B (external-first + synthetic-load-augmented); `scripts/synth-load/run.sh` runs every example + the deterministic eval subset and diffs against a golden-fingerprint corpus. Wired into eval-nightly as non-blocking until the first --refresh-golden lands. | ~1,000 | none |
| 2    | **Auto-generated `/agents/tools` page + Sprint 11/12 carry-overs.** `souxmar agent list --json` (new flag) → `scripts/docs-site/gen-agent-tools.py` → `docs-site/agents/tools.md`. eval-nightly verifies with `--check-only`. Plus VTU consumer-conformance test + visual-regression baselines policy doc. | ~760 | none |
| 3    | **First real FFI: `pipeline_introspection` (ADR-0018).** New `libsouxmar-c-bridge.a` static library exposes a six-function C ABI; Rust `souxmar-bridge` crate's `ffi` module + `build.rs` + `real-ffi` cargo feature wire it through; `BridgeFeatureSet::pipeline_introspection` flips on; Inspector panel renders real stage list. ABI-version byte cross-checked on every FFI call. | ~1,100 | none — new public header in `include/souxmar-c-bridge/`, Tier-2 contract; new ADR-0018 |
| 4    | **cfd-stub per-patch BC routing + first bug-report record.** Closes the Sprint 10 carry-over. cfd-stub now reads `patches: [{ name, tag, bc }, ...]`, walks per-face tags, applies the BC with `wall > inlet > outlet > bulk` precedence. First entry in `docs/bug-reports/` codifies the ADR-0017 triage record format. | ~480 | none |
| 5    | **Sprint 13 retro + v0.9.1 cut.** This commit.                                                          | this  | none — `v0.9.1` is the first dot-release after the public alpha |

**Total LOC: ~3,400.** Larger than Sprint 12 (~1,800) because the
first real FFI took ~1,100 lines on its own (C bridge library +
Rust FFI bindings + ADR-0018 + Inspector wiring + integration
test). The shape we expect: per-sprint LOC is now demand-driven
by what the public-alpha + carry-over backlog surfaces, not
push-driven by what was planned in SPRINT_PLAN.md.

## What to keep

- **The "every fix gets a `docs/bug-reports/` record" habit.**
  Sprint 13 push 4 seeded the first record for the cfd-stub fix
  the Sprint 10 retro queued. The format is named (date +
  sequential digit + brief symptom + root cause + what's
  covered + what isn't) and machine-readable enough that the
  triage workflow can cross-reference. The first external bug
  reports that come in during Sprint 14 will use the same
  format; the cost of the convention is low, the value to
  Sprint 18's metric review is high.
- **The non-blocking-on-first-run harness pattern.** ADR-0017's
  synthetic-load harness ships with placeholder fingerprints; CI
  runs it `continue-on-error: true` until the first
  `--refresh-golden` lands. This pattern (ship the gate
  *before* the gate's data is ready) keeps the rollout safe and
  visible without holding up infrastructure work behind a
  chicken-and-egg problem. Sprint 14 + 15's larger harnesses
  (per-platform VR baselines, viewport rendering golden frames)
  will use the same approach.
- **The Cargo-feature-gate-the-FFI pattern.** ADR-0018 § 4
  records this — `real-ffi` is off by default so `cargo check`
  / clippy / IDE intellisense across the Rust workspace works
  without a C++ build available. Future FFI surfaces
  (`viewport_renderer`, `provider_call`, `auto_updater_menu`)
  follow the same template; the cost of the conditional
  compilation is one `#[cfg(feature = "real-ffi")]` per FFI
  function declaration.
- **Typed-enum returns, again.** Push 3 added `FfiOutcome<T>`
  with four named variants (SkeletonNoFfi / FfiOk / FfiError /
  AbiMismatch); push 4 added `NodeRouting` with four named
  variants (Bulk / Outlet / Inlet / Wall) — both follow the
  Sprint 6+ pattern of "no string parsing in callers." The
  pattern is now reflexive in every new module.
- **Auto-generated docs as the source of truth.** Push 2's
  `gen-agent-tools.py` makes drift between the binary and the
  public docs structurally impossible. The pattern generalises
  — `souxmar plugin list --json` (Sprint 14) →
  `/plugins/registry.md`, `souxmar update channel-info --json`
  (Sprint 15) → `/release/channels.md`. Same shape every time.

## What to fix

- **The synthetic-load corpus is still empty.** Sprint 13 push 1
  shipped the harness + placeholder fingerprints; the first
  `--refresh-golden` run requires a maintainer eye on the CI
  artefact (per ADR-0017's "the first batch is load-bearing"
  argument). **Action: Sprint 14 push 1 commits the first
  golden corpus from the first green eval-nightly post-v0.9.1
  + flips `continue-on-error: false`.**
- **The release CI doesn't yet build `libsouxmar-c-bridge` + flip
  `real-ffi` on.** Today the C bridge compiles in the
  integration test suite (push 3's tests link it) but the
  desktop release path doesn't yet pick it up. **Action: Sprint
  14 push 2 extends `.github/workflows/release.yml` (and the
  matching CI matrix) to build the C bridge first, export
  `SOUXMAR_C_BRIDGE_LIB_DIR`, build the Tauri shell with
  `--features real-ffi`.**
- **The Sprint 12 retro's operational follow-ups are still open.**
  DNS CNAME for docs.souxmar.dev, Discord server + redirect,
  on-call rotation table population — all unmoved this sprint
  because they're not coding tasks. **Action: separate
  "launch-comms" punch list lives outside the sprint cadence;
  not a Sprint 14 blocker, but Sprint 14's retro reports
  status.**
- **No external bug reports landed this sprint.** Volume = 0 in
  the first week of v0.9.0. The metric model in ADR-0017
  targets ≥ 20 in the first *month*, so one zero-volume week is
  not yet a signal. **Action: continue measuring; Sprint 14
  retro is the first checkpoint where a zero-volume *two-week*
  number triggers ADR-0017 § "Alternatives reconsidered later"
  (the launch-comms amendment, not a strategy pivot).**
- **The agent-tool docs page in `docs-site/agents/tools.md` is
  still a placeholder.** The generator is wired; the first
  green build with `souxmar agent list --json` available has to
  run to replace it. **Action: Sprint 14 push 1 (same CI run as
  the synth-load corpus initialisation) lands the first real
  rendering.**
- **The bug-report record format isn't yet referenced from
  COMMUNITY.md.** Push 4 codified the convention but didn't
  link it from the triage SLA matrix. **Action: Sprint 14 push 2
  doc fix — one-line addition to COMMUNITY.md pointing at
  `docs/bug-reports/`.**

## One ADR-worthy decision surfaced

**Sprint 13 push 3's first real FFI raised the question: when a
`BridgeFeatureSet` flag flips on at build time (via cargo feature
+ link-time linkage), what's the *runtime* contract for partial-
upgrade scenarios?** A user might:

1. Run a desktop build that was linked against a *future*
   `libsouxmar-c-bridge.a` (impossible today; possible once
   we ship dot-releases of the bridge alongside dot-releases
   of the desktop).
2. Run a desktop build that was linked against a *stale*
   bridge (a build that happened before the C ABI bump).
3. Run a desktop build with the cargo feature off altogether
   (developer build).

ADR-0018 covers (3) via `is_real_ffi_compiled_in()` + Skeleton
fallback, and (1)/(2) via `souxmar_bridge_abi_version()` cross-
check on every call.

The decision worth surfacing for a future ADR (Sprint 14 or
later): **what's the version-compat policy across BridgeFeatureSet
flags as we add more?** Today the protocol version is a single
`u32` — there's no per-flag versioning. When a flag is added,
the protocol version bumps; an old client refuses everything
from a new server.

The alternative — per-flag protocol versions, like
`viewport_renderer: ProtocolVersion(3)` — would let an old
client successfully call the half of the bridge it understands
while declining the half it doesn't. The cost is per-flag
version bookkeeping in every push that adds a flag; the value is
forwards-compat across rolling upgrades.

Queued for Sprint 16+ once the second + third real FFI surfaces
land and we can see whether the single-version monolithic
approach starts to bite.

## Risk register diff

- **R-013 (souxmar-bridge FFI crate doesn't exist)** — already
  closed in Sprint 12. Sprint 13 turned the first contract field
  structural.
- **R-014 (public-alpha bug-triage workflow doesn't exist)** —
  already closed in Sprint 12.
- **R-015 (first-week external feedback signal could overwhelm
  or starve the triage rotation)** — Sprint 13 result: **starves
  (volume = 0)**. Likelihood at Sprint 14 entry: Med-High
  (continued zero would be a signal). Impact: still Med.
  **Mitigation status:** the synthetic-load harness landed this
  sprint provides the breadth signal ADR-0017 promised would
  cover this case. Sprint 14 retro is the checkpoint.
- **R-010 (hiring + velocity)** — Sprint 13 ran ~30 pts
  measured, inside the rolling-median model Sprint 12 adopted
  (35 ± 15). First sprint *inside* the variance band in four
  sprints. **Status: closes the immediate "model is broken"
  concern; opens the longer-term hiring concern which is
  unchanged.**
- **R-003 / R-001 / R-006 / R-009 / R-011 / R-012** — no change.
- **New risk R-016 — release CI doesn't yet build the C bridge.**
  The desktop app's `pipeline_introspection` flag is on at
  build time only when the build was real-ffi; the release
  pipeline today builds without that feature. **Likelihood at
  Sprint 14: High (will be addressed in push 2); Impact: Low
  (only affects whether the inspector renders real or
  scaffolded content; no data loss).** **Mitigation:** Sprint 14
  push 2's release-workflow update.

## Capacity for Sprint 14

Sprint 13 ran ~30 pts measured:

| Push | Effort (pts) | Note                                                            |
| ---- | ------------ | --------------------------------------------------------------- |
| 1    | 6            | ADR-0017 + synthetic-load harness + CI wire                      |
| 2    | 5            | Auto-gen agent docs + VTU conformance + VR baselines doc        |
| 3    | 12           | First real FFI: ADR-0018 + C bridge lib + Rust FFI + Inspector  |
| 4    | 5            | cfd-stub per-patch BC routing + bug-report record               |
| 5    | 2            | Retro + release cut (this commit)                               |

**~30 pts, inside the 35 ± 15 band.** Sprint 14 target: **35 pts
± 15**. Same band; the rolling-median model adjusts after
another two sprints of data.

Sprint 14 themes from SPRINT_PLAN.md § Sprint 14 ("Managed-AI
proxy MVP (Pro tier); billing integration POC"):

Likely pushes:
- Synth-load corpus initialisation + `continue-on-error: false`
  flip (~3 pts; carry-over from this sprint).
- Release-workflow update to build C bridge + real-ffi (~5 pts;
  R-016 mitigation).
- Per-platform VR baseline matrix (~6 pts; tests/visual/
  BASELINES.md "Sprint 14 push 1" promise).
- Managed-AI proxy MVP scaffold — Pro tier infrastructure, no
  paid tier live yet, ADR-0019 candidate for the proxy
  architecture (~10 pts).
- Stripe-billing integration POC alongside the proxy (~5 pts).
- COMMUNITY.md → docs/bug-reports/ link (~1 pt doc fix).
- Sprint 14 retro + v0.9.2 (~2 pts).

External-feedback volume (R-015) is the wildcard; if the first
public-alpha bug reports arrive, they preempt feature work per
the SLA matrix.

## Outcome

souxmar is at **v0.9.1** as of this commit. **First dot-release
after the public alpha.** Five days after the v0.9.0 cut.

Significant in scope:

- The **first real FFI surface** is wired end-to-end —
  `libsouxmar-c-bridge.a` static library, Rust bindings,
  ABI-version cross-check on every call, Inspector panel
  rendering real pipeline state. The `pipeline_introspection`
  flag flipped from "always false" to "structural" — three more
  flags (`viewport_renderer`, `provider_call`,
  `auto_updater_menu`) follow the same template through Sprint
  17.
- The **synthetic-load harness** and the **auto-generated docs
  page** make drift between the binary and the public surface
  structurally impossible going forward. Two infrastructure
  artefacts that ratchet the quality bar without per-push
  effort.
- The **first carry-over from Sprint 10** (cfd-stub per-patch
  BCs) finally landed, along with the convention for tracking
  every subsequent fix in `docs/bug-reports/`.

The ABI stays at v1.3 frozen final. The tool contract stays at
v1 frozen final with 18 tools. The same C++ engine powers the
CLI, the Python library, and (now also via FFI) the desktop app's
inspector panel — four peer surfaces, no privileged path, as
designed.

What's **not** in v0.9.1: viewport rendering (Sprint 14), real
provider chat through the bridge (Sprint 14), managed-AI proxy
(Sprint 14), cloud sync (Sprint 15), paid plugin marketplace
(Sprint 16). The honest framing — Sprint 13's exit criterion was
"first real FFI flag flipped + ADR-0017 ratified + carry-overs
drained" — is met *modulo* the Sprint 14 push 1/2 follow-ups
(synth-load corpus + release-CI integration) named in "what to
fix" above.

Sprint 14 starts measuring whether the external-first half of
ADR-0017's model is generating signal, and lands the next FFI
surface alongside the managed-AI proxy MVP.
