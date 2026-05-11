# Changelog

All notable changes to souxmar are documented here. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The plugin C ABI version is tracked separately and is independent of the project version. **ABI v1 is frozen final at v1.3**; see [ADR-0008](docs/adr/0008-abi-v1-final-freeze.md) for the freeze + [ADR-0012](docs/adr/0012-per-face-tag-c-abi-ratchet.md) for the current minor ratchet. The **agent tool contract v1 is frozen final at 18 tools**; see [ADR-0011](docs/adr/0011-tool-contract-v1-final-freeze.md). The release-stamping ritual is described in [`docs/RELEASE_NOTES_TEMPLATE.md`](docs/RELEASE_NOTES_TEMPLATE.md).

## [Unreleased]

### Added

- (None this release — `[Unreleased]` reopens after the v0.9.0-beta4 cut below.)

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## [0.9.0-beta4] - 2026-05-11

Fourth public pre-release. Source + Linux CLI tarball + Python sdist published as a GitHub release; macOS notarised `.dmg` + Windows EV-signed `.zip` newly attached this release (push 8 wired the release workflow). **Tag:** `v0.9.0-beta4`. **ABI:** v1.3 frozen (unchanged). **Tool contract:** v1 frozen final at 18 tools (unchanged).

Sprint 10 closes here. Release notes link to `docs/retros/sprint-10.md` for the keep/fix/one-ADR-worthy-decision narrative + risk-register diff + Sprint 11 capacity forecast.

The auto-updater XL story (ADR-0013) closes across pushes 4-8 in this release. The plugin marketplace v0 surfaces (data model + search + publication workflow) close in pushes 2-3. The AI Provider abstraction + OllamaProvider land in push 9. The desktop onboarding wizard scaffolds in push 10. The fourth in-tree example lands in push 11.

The desktop app is still not user-facing beyond onboarding — the workbench shell is empty. Sprint 11 (internal alpha; dogfooding) is the next exit criterion.

### Added

#### Sprint 10 push 4 — auto-updater foundation: signed manifest format + parser + validator (ADR-0013)

Opens Platform's "Auto-updater across all 3 OSes; signed manifest pipeline; rollback protocol" XL story (`docs/SPRINT_PLAN.md` § Sprint 10). This is the foundation push: the on-disk format and the parser/validator that the verifier (push 5), state machine (push 6), and rollback log (push 7) will all key off. The whole-story design decisions are locked in by ADR-0013, so subsequent pushes can focus on behaviour rather than re-litigating the data model. **No frozen-header surface touched, no new third-party dependency** — re-uses tomlplusplus from the existing manifest/index parsers; libsodium is deferred to push 5 where the verifier actually needs it.

- **`docs/adr/0013-signed-update-manifest.md`** — tier-2 design ADR locking in: TOML manifest with `schema = 1` discriminator; three channels (`stable`/`beta`/`nightly`); per-(OS × arch) artifact metadata pinned by sha256 + size; ed25519 detached signature; pinned `public_key_id` with separate-event key rotation. Alternatives considered (JSON, embedded JWS-style, full TUF, Sigstore, platform-native-signing only) each get their pro/con and the rationale for deferring. Pre-mortem covers the three most plausible failure modes a year out (schema-too-narrow → additive field; key compromise → rotation already covered; downgrade-attack incident → constructive migration to TUF snapshot+timestamp roles, this manifest stays as `targets`).
- **`include/souxmar/update/manifest.h`** — new `souxmar::update` namespace, data model only:
  - `enum class Channel { Stable, Beta, Nightly }`, `enum class Os { Linux, Macos, Windows }`, `enum class Arch { X86_64, Aarch64 }`.
  - `struct Artifact { os, arch, url, sha256, size }` — one per platform tuple.
  - `struct ChannelBlock`, `struct ReleaseBlock` (with `min_previous_version` floor + `rollback_target` + `mandatory` UI-hint), `struct SigningBlock` (algorithm + pinned public-key id).
  - `struct Manifest` — the whole document, schema-stamped.
  - `parse_manifest_file(path)` and `parse_manifest_string(toml)` returning `std::variant<Manifest, ManifestParseError>`. Splitting parse from verify means the verifier can be added next push without re-cutting this surface.
  - `validate_manifest(m) → vector<ManifestValidationIssue>` — structural-publishability gate; `ManifestIssueSeverity { Error, Warning }` matches the plugin-index validator's two-tier pattern.
  - `kManifestSchemaV1 = 1` exposed for future schema-version comparison.
- **`src/updater/manifest.cpp`** — parser + validator implementation. Parser is strict on required fields (`schema`, `[channel].name`, `[release].version`, every `[[artifact]]` field, the entire `[signing]` block) and defensive on optional ones (missing/empty/wrong-type defaults to empty). Required-field failures throw a per-scope diagnostic ("[[artifact]] #2: missing required field 'sha256'") wrapped into `ManifestParseError` — release-pipeline engineers fixing a broken emitter see the offending field by name, not a generic "parse failed". Unknown values on enum-shaped fields (channel name, os, arch) reject the manifest outright. Negative `size` rejects with "must be non-negative".
- **`src/updater/manifest.cpp` validator — six error checks + three warning checks.**
  - **Error: `signing.algorithm != "ed25519"`.** The v1 manifest is ed25519-only; ADR-0013 keeps the field a string so a future `sigstore` value slots in without re-cutting, but rejects every other value today.
  - **Error: empty `signing.public_key_id`.** The verifier needs an id to look up the trust-store key.
  - **Error: `release.version` is not three dot-separated numeric tokens.** Lightweight grammar check ("MAJOR.MINOR.PATCH"); full SemVer comparison lives in the version-comparison module that lands with the state machine in push 6.
  - **Error: duplicate (`os`, `arch`) pair across `[[artifact]]` entries.** Catches the canonical release-pipeline bug where two `artifact` blocks claim the same platform tuple.
  - **Error: `artifact.url` is not `http(s)://`.** Same shape check as the plugin-index validator's source-URL check; catches a local path or an `ssh://` URL.
  - **Error: `artifact.sha256` is not exactly 64 lowercase hex characters.** Both length and character-set; the canonical sha256 representation is lowercase-hex per SHA-256/POSIX `sha256sum` convention.
  - **Error: `artifact.size == 0`.** A zero-byte declaration would defeat the pre-flight disk-space check.
  - **Warning: empty `release.rollback_target`.** Disables rollback; reviewer confirms it's intentional.
  - **Warning: empty `release.min_previous_version`.** No floor; reviewer flags so the release pipeline's omission is deliberate.
  - **Warning: empty `channel.expires_at`.** The freshness check loses its grip; reviewer confirms.
- **Validator deliberately does NOT check time-dependent properties.** The "is `channel.expires_at` in the past" check is the *apply gate*'s job (push 6) — wall-clock is meaningful there, not in CI or unit tests. Unit-test stability is paid for in design.
- **`src/updater/CMakeLists.txt`** — new `souxmar::update` target. Builds against tomlplusplus + the public-headers interface target; no other deps yet. Subsequent pushes append source files to this list (verifier → push 5; state machine → push 6; rollback log → push 7).
- **`tests/unit/test_update_manifest.cpp`** — 24 tests. Parser: minimal-valid roundtrip, full-five-artifact roundtrip, enum string roundtrip, beta + nightly channel parsing, missing-schema rejection, unknown-schema-version rejection, unknown-channel-name rejection, unknown-OS rejection, unknown-arch rejection, missing-required-artifact-field rejection, malformed-TOML rejection, empty-artifact-array rejection, negative-size rejection. Validator: full-manifest-validates-clean, minimal-validates-clean-with-no-warnings, non-ed25519-algorithm-error, empty-public-key-id-error, malformed-version-error (×3: short, prefixed, too-long), bad-sha-length-error, uppercase-sha-error, bad-URL-scheme-error (×2: ftp + local-path), zero-size-error, duplicate-os-arch-error, empty-rollback-target-warning, empty-min-previous-version-warning, empty-channel-expires-at-warning, multi-issue-accumulation-error (5 distinct errors + 1 warning reported on a single mangled manifest — the test that locks in "every issue reported, not just the first").

What the auto-updater push sequence looks like from here:

1. **Push 4 (this push):** signed-manifest format + parser + validator + ADR.
2. **Push 5:** ed25519 detached-signature verifier (introduces libsodium); maps `signing.public_key_id` to an embedded trust-store entry.
3. **Push 6:** updater state machine (discover → download → verify → stage → swap) + `souxmar update check` / `souxmar update apply` CLI subcommands; the time-dependent freshness check (`expires_at` vs. wall-clock) lives here.
4. **Push 7:** rollback log (records the previous install path + version so `souxmar update rollback` lands deterministically); first-launch crash → automatic rollback path.
5. **Push 8:** Apple notarisation automation (the second Platform Sprint 10 story; handles altool/notarytool queue stalls with retry/backoff). Closes the XL.

The downstream consumers (`docs/DESKTOP_APP.md` § Update protocol, `docs/SECURITY.md` § Release signing) reference this ADR for the data model + signing scheme; subsequent pushes annotate the same docs with the concrete behaviour as it lands.

#### Sprint 10 push 3 — plugin-index publication workflow (PR-gated, conformance surfaced)

Closes the second Plugin-team named SPRINT_PLAN.md story for Sprint 10 ("Index publication workflow: PR-based, with conformance status surfaced"). Builds directly on push 2's parser + canonical index. A new validator runs every check the parser deliberately doesn't (duplicate id detection, URL format, capability shape, license / version-range completeness, conformance status surfacing) and a new CI workflow runs that validator on every PR touching `docs/plugin-index.toml`. Third-party authors get a structured "what's wrong with my entry" check before review; reviewers focus on what reviewers should focus on. **No frozen-header surface touched** — extends the push 2 surface; ABI v1.3 stands.

- **`include/souxmar/plugin/index.h`** — three new types extending the push 2 schema:
  - `enum class IndexIssueSeverity { Error, Warning }` — the two-tier policy `BUSINESS_MODEL.md` § Plugin marketplace economics implies. Errors block the merge (structural); warnings are reviewable (judgement).
  - `struct IndexValidationIssue { severity, entry_index, field, message }` — one diagnostic. The `entry_index` points at the position the parser saw, so a reviewer can grep the PR diff by `[[plugin]]` block index.
  - `validate_index(entries) → vector<IndexValidationIssue>` — runs the full suite over a parsed batch and returns every issue (doesn't stop at the first). Empty result means publishable as-is.
- **`src/plugin-host/index.cpp`** — validator implementation. Six checks across two tiers:
  - **Error: duplicate id.** Cross-entry scan via a first-occurrence map; the diagnostic points at the *second* occurrence (matching the position a reviewer sees in a diff) and names the first occurrence's index.
  - **Error: malformed `source` URL.** Must start with `http://` or `https://` followed by at least one host character. Catches the common mistake of pasting a local path, an `git@…` SSH URL, or a bare domain.
  - **Error: malformed `homepage` URL.** Same shape check as `source`; only fires when the field is non-empty (homepage is optional).
  - **Error: invalid capability id.** Must be a dotted reverse-DNS-ish identifier (alphanumeric + `.` + `_` + `-`, at least one `.`). Catches the "spaces in capability name" mistake and capabilities that would collide with the souxmar top-level taxonomy (a missing-dot id like `mesh` is rejected).
  - **Warning: empty `license` on a free-channel entry.** Open index policy commits to OSI-licensed source for the free channel; the validator flags absence so reviewers confirm. Suppressed when `paid = true` (paid-marketplace entries may legitimately omit the field — the marketplace handles license-key flow separately).
  - **Warning: empty `souxmar_versions`.** Recommended value is `">=1.0,<2.0"` for any v1-ABI plugin; absence is technically valid but indicates the author hasn't thought about forward compatibility.
  - **Warning: `conformance = "failed"`.** Listing remains visible (sometimes an author needs visibility to drive bug reports) but the badge surfaces "failed" until reattested.
- **`souxmar plugin validate-index` CLI subcommand.** Three exit codes:
  - `0` — every check passed, or warnings-only. PR mergeable from the validator's perspective.
  - `10` (`kExitInputData`) — at least one error-severity issue. PR cannot merge until the author fixes the entry.
  - `2` (`kExitUsage`) / others — internal failure (index file missing, parse error). Distinct exit code lets CI route validation failures separately from harness failures.
  Output format: one line per issue (`<severity>: entry #<n> (id=<id>) <field>: <message>`) — errors to stderr (CI captures in the failure log), warnings to stdout (PR-comment renderers pick them up next to the diff). Trailing summary line gives counts + the index path. `--index <path>` reuses the push 2 resolution logic.
- **`.github/workflows/plugin-index.yml`** — new CI workflow. Triggers on PRs touching the index file or any of the validator's source paths (`include/souxmar/plugin/index.h`, `src/plugin-host/index.cpp`, `src/cli/main.cpp`) plus the workflow file itself. Builds the CLI in release mode (no benchmarks, no tests, no examples — the validator is exercised by unit tests in the main CI job; this workflow just needs the binary), runs `souxmar plugin validate-index` against the file, and surfaces the validator's text output in the workflow's job summary (the "conformance status surfaced" piece of the SPRINT_PLAN.md story). Reviewers see the validation summary inline without digging through the full build log.
- **`.claude/skills/publishing-plugin-marketplace/SKILL.md`** — process section updated. The "Open a PR against `docs/plugin-index.md`" Markdown form is replaced by the TOML schema from push 2. New step #3 names the `plugin-index` workflow + the local-validation command + the exit-code contract; new step #4 clarifies that the validator handles the schema gate, leaving reviewers to focus on completeness.
- **`tests/unit/test_plugin_index.cpp`** — 11 new validator tests. Clean-entry-produces-no-issues, duplicate-id-flagged-at-second-occurrence, malformed-source-URL-error, malformed-homepage-URL-error, empty-homepage-OK, invalid-capability-id-error, empty-license-on-free-entry-warning, empty-license-on-paid-entry-OK, empty-version-range-warning, failed-conformance-warning-not-error, multiple-issues-all-reported (3 errors + 1 warning on one entry), in-tree-index-validates-clean. The last one is a regression-prevention test: if a future push 2-style edit adds an entry that fails validation, push 3's tests fail before CI even runs the workflow.

Sanity check: the validator rules ran against the actual committed `docs/plugin-index.toml` (16 entries — 11 always-on + 5 opt-in) report zero issues. The in-tree listings were written to satisfy the schema from the start.

What the publication flow now looks like end-to-end:

1. Third-party author opens a PR adding their `[[plugin]]` entry to `docs/plugin-index.toml`.
2. The `plugin-index` CI workflow fires automatically (paths-filtered on the index file). Builds CLI, runs validator.
3. If the validator reports errors (the structural ones), the workflow exits non-zero and GitHub blocks the merge. The author iterates on their entry against the same `souxmar plugin validate-index` command they can run locally.
4. If warnings only (or clean), the workflow exits zero. Job summary surfaces the validator output inline. DX reviewer pulls up the PR, sees the green check + the inline validation summary, and reviews for completeness.
5. Merge triggers no further automation today; the next `souxmar plugin search` invocation against the merged file picks up the new entry. (The Sprint 10 push 4+ work — auto-updater + plugin-index regen — sits on top of this contract.)

The Plugin-team Sprint 10 stories are now both closed (data model + search in push 2; publication workflow + conformance surfacing in push 3). The two remaining Sprint 10 themed items — Platform's auto-updater (XL) and the Apple notarisation automation (M) — are the next pushes. The `paid` flag from push 2 + the validator's paid-aware license-check carve-out keep the marketplace v0 path ready for Sprint 16.

#### Sprint 10 push 2 — plugin index v0 + `souxmar plugin search`

Closes the first Plugin-team named SPRINT_PLAN.md story for Sprint 10 ("Plugin index data model; `souxmar plugin search` against the static index"). The schema, the parser, the canonical static index, and the CLI surface all land together so the marketplace work in pushes 3 + later has a working data model + query path to build on. **No frozen-header surface touched** — new host-side header + impl + new CLI subcommand + new TOML doc; ABI v1.3 stands.

- **`include/souxmar/plugin/index.h`** — new `souxmar::plugin::IndexEntry` data model. Captures the publishable metadata an open-index listing needs: `id`, `name`, `description`, `capabilities[]`, `license` (SPDX), `source` (URL), `homepage`, `author`, `souxmar_versions` (SemVer range), `conformance` + `conformance_date`, lifecycle `status` (`active` / `maintained` / `unmaintained` / `archived`), and a `paid` flag the Sprint 16+ paid marketplace will set on its listings. Two enum types (`ConformanceStatus`, `LifecycleStatus`) carry the canonical strings the skill `publishing-plugin-marketplace` documents; `to_string` overloads round-trip both. `load_index_file(path)` and `load_index_string(toml)` return a `std::variant<vector<IndexEntry>, IndexParseError>` so the caller can decide whether a malformed entry aborts or logs-and-continues. `search_index(entries, query, capability_prefix)` filters case-insensitively across `id` / `name` / `description` / `author` / `capabilities[]` and optionally restricts by a capability-prefix; returns matches in input order so a curated listing keeps its display position.
- **`src/plugin-host/index.cpp`** — implementation. tomlplusplus parser (already a host dependency from the plugin-manifest path; no new third-party). Defensive: unknown TOML keys are ignored, optional fields default to empty / `Active` / `NotRun`, capability arrays must be non-empty, three required fields (`id`, `name`, `source`) are enforced with per-field error messages so a bad PR surfaces "missing required field: source" rather than a generic parse failure. Per-entry parse exceptions get wrapped into an `IndexParseError` naming the failing entry index — partial-batch parsing is the right default for an index that will eventually hold hundreds of third-party entries.
- **`docs/plugin-index.toml`** — the canonical static index. Bootstrapped with all 16 in-tree reference plugins (the 11 always-on + the 5 opt-in adapters):
  - Always-on: `hello-mesher`, `grid-mesher`, `hello-writer`, `vtu-writer`, `heat-solver`, `elasticity-stub`, `cfd-stub`, `scalar-magnitude`, `mesh-quality`, `stl-reader`, `obj-reader`.
  - Opt-in: `occt-reader`, `gmsh-mesher`, `fenicsx-solver`, `openfoam-solver`, `blender-reader`.
  Each entry carries the full schema (license, source URL into the canonical repo, capabilities array matching what the manifest registers, `souxmar_versions = ">=1.0,<2.0"`, conformance `passed` with the 2026-05-11 attestation date). The DX team takes maintenance ownership of this file; the `publishing-plugin-marketplace` skill walks third-party authors through the PR-against-this-file flow.
- **`souxmar plugin search` CLI subcommand** in `src/cli/main.cpp`. Three new flags on the existing global parser:
  - `--index <path>` — override the index-file path. Priority order: this flag, then `$SOUXMAR_PLUGIN_INDEX`, then `./docs/plugin-index.toml` (the source-checkout default). Empty + missing surfaces a friendly "set $SOUXMAR_PLUGIN_INDEX or pass --index" error rather than a parse failure.
  - `--capability <prefix>` — restrict matches to entries that register at least one capability with this prefix. `--capability solver.cfd.` matches every CFD solver entry.
  - Positional query (zero or more tokens; joined with single spaces) — case-insensitive substring match.
  Output is tabular plain text: one block per match with id, description, capabilities, license, author, source, souxmar version range, conformance status + date, lifecycle status, and a `[paid]` flag when set. Trailing summary line gives the match count + the index path consulted. `--help` updated accordingly.
- **`tests/unit/test_plugin_index.cpp`** — 9 tests covering: minimal-entry-with-defaults parse, full-entry round-trip of every optional field (including `paid = true`), missing-required-field error message names the field, empty `capabilities` rejected with a named message, malformed TOML surfaces as `IndexParseError`, empty-query search returns input, substring search hits id / author / capability / description, capability-prefix restriction works for matched + unmatched prefixes, search preserves input order, status-string round-trips.
- **README banner** — `souxmar plugin list` → `souxmar plugin {list,search}` in the "Runnable today" CLI line, with a parenthetical naming the canonical index path.

Operationally:

- A souxmar user in a checkout can now run `souxmar plugin search openfoam` and see the matching entry. `souxmar plugin search --capability mesher.tetra.` lists every tetrahedral mesher in the index (gmsh-mesher + grid-mesher + hello-mesher). The output is identical regardless of which channel the plugin came from — in-tree, third-party-open, third-party-paid — so users learn one query interface for everything they can install.
- Third-party authors who want their plugin discoverable open a PR against `docs/plugin-index.toml` with a new `[[plugin]]` entry, following the format the skill `publishing-plugin-marketplace` documents. DX reviews for completeness only; we do not vet code or behaviour. The conformance badge is the sole quality signal — same policy `docs/BUSINESS_MODEL.md` § Plugin marketplace economics commits to.
- The `paid` flag is ready for the Sprint 16 paid-marketplace launch. No code change at that boundary — the existing search renders `[paid]` next to status, and the same TOML schema accepts both free and paid entries.

The remaining Plugin-team Sprint 10 story — "Index publication workflow: PR-based, with conformance status surfaced" (M) — naturally builds on this push. The publication workflow will validate new entries against the schema at PR time (a CI job running `load_index_file` + `souxmar-conformance` against any plugin whose entry was added or modified) and surface the conformance status check inline. Planned for Sprint 10 push 3 or 4.

#### Sprint 10 push 1 — first perf-baseline rotation (closes R-011 from the Sprint 9 retro)

First push of Sprint 10. Closes the carry-over named in the Sprint 9 retro: the perf-regression gate that landed pushes 6–10 of Sprint 9 was running on every relevant PR but with empty baselines, so each binary reported "(new — no baseline yet; skipping)" and the gate didn't actually fire. The Sprint 9 retro filed this as risk **R-011** with a one-push close-out plan. This push lands that plan. **No frozen-header surface touched** — committed JSON files only; ABI v1.3 stands.

- **`benchmarks/baselines/*.json`** — five new baseline files, one per benchmark binary, captured from the first nightly soak run on the reference hardware (`souxmar-perf-runner-01`, Ryzen 7 7700X / 16 threads / 5.4 GHz, scaling disabled, release build per `ENGINEERING_PRACTICES.md` § Performance budgets convention). Each file carries the standard Google-Benchmark `context` block + per-workload `iteration` entries with `real_time` / `cpu_time` / `time_unit` so both `compare.py` and `dashboard.py` ingest cleanly without code change.
  - `bench_mesh_construction.json` — 8 entries (BM_PerElement × 4 arg sizes + BM_Bulk × 4 arg sizes). The bulk path is ~4–5× faster than per-element across the scale range; pins the ADR-0006 v1 promise.
  - `bench_mmap_buffer.json` — 12 entries (3 workloads × 4 buffer sizes). The mmap-reopen path is ~50× faster than the heap roundtrip on the cold-read case — exactly the ADR-0006 v2 result we built the buffer for.
  - `bench_face_tag.json` — 12 entries (4 workloads × 3 cell counts). **Crucially: `BM_FaceTag_GetMiss` reads 32.1 / 32.3 / 32.5 ns across 1K / 10K / 100K cells.** Constant-time within noise floor — the ADR-0012 sparse-storage promise ("untagged faces cost zero time regardless of mesh size") is captured in the baseline. A future regression that ever makes the empty-map lookup grow with mesh size shows up as ratio-vs-baseline divergence across the three args, not as an absolute number, so the gate will catch it.
  - `bench_plugin_dispatch.json` — 3 entries. `BM_PluginDispatch_Warm` at **6.42 µs warm**, comfortably under the `ENGINEERING_PRACTICES.md` 20 µs target — the gate's 5 % threshold gives a hard regression bound of ~6.74 µs warm, and any change that ever crosses the absolute 20 µs line would have to first regress past the relative gate. Two layers of protection on the dispatcher hot path.
  - `bench_heap_accountant.json` — 3 entries. `BM_HeapAccountant_DeltaPair` at **587 ns**, comfortably under the < 1 µs target the audit-log accountant needs to stay safe always-on. `BM_HeapAccountant_IsSupported` at **1.84 ns** confirms the inlined-constant return path.
- **`benchmarks/baselines/README.md`** unchanged — its existing "file layout" / "update workflow" / "regression threshold" sections already document the rotation. The numbers in the JSON files match what the regenerate-locally loop in that README produces.

What changes operationally:

- Every PR touching a perf-gated path now runs against real numbers. The `compare.py` directory-mode comparison fires for all five binaries; a regression > 5 % on any workload fails the comparison step (push 6 wired the "fail if regressions" outcome routing); the dashboard renders the matching red badge.
- Improvements > 5 % show as green badges on the dashboard — celebrated, not gated. Reviewers should still call out deliberate improvements in the PR title so the next rotation doesn't lock in an accidental win.
- The Sprint 5 "baseline established" exit criterion finally closes (~4 sprints after it was first written). The Sprint 9 exit criterion "Perf regression gate live; the team has dealt with 1+ regression block in CI" is one regression-block-in-anger away from closing.

Risk register diff:

- **R-011 (perf gate has empty baselines)** — **closed.** Closes the open window flagged in the Sprint 9 retro between push 10 of Sprint 9 and this push.

#### Sprint 10 push 5 — ed25519 detached-signature verifier (libsodium)

Continues ADR-0013. Adds `souxmar::update::TrustStore` + `verify_manifest_signature()` returning a typed `SignatureStatus` (7 discrete values; the state machine in push 6 branches on the enum, the rollback log in push 7 records it verbatim). libsodium is scoped PRIVATE to `souxmar_update` — every other module stays sodium-free. 27 unit tests; the fixture generates keypairs via `crypto_sign_seed_keypair` so the suite is deterministic without a `/dev/urandom` dependency.

#### Sprint 10 push 6 — update state machine + `souxmar update check` / `apply --dry-run` CLI

The pre-flight decision layer. Pure logic; no I/O, no syscalls beyond `timegm`. `apply_gate(manifest, install, clock)` returns either `UpdateApply{artifact, version}` or `UpdateRefusal{reason}` — 9 discrete reasons, documented step-ordering locked in by 5 precedence tests. Per-user state file at `~/.local/state/souxmar/update-state.toml` (et al.) carrying `current_installed_version` + `max_version_ever_seen` (the replay-defence floor). `TimeSource` abstraction lets the test suite pin the clock. `--as-of` flag exposes the override to integration tests so the `expires_at` gate is testable hermetically. Closes the push-6-flagged replay-defence gap noted in push 6's commit message via state-file writes added to `check` itself.

#### Sprint 10 push 7 — install layout + atomic apply/rollback + `souxmar update apply` / `rollback` CLI

The filesystem half. `<target_root>/{current.txt, previous.txt, versions/<v>/payload, staging/, rollback.log}` — marker-file approach (vs. symlink) keeps the cross-OS code path identical on POSIX + NTFS. `apply_update(ctx)` orchestrator: re-runs gate (defense in depth), verifies sha256 + size against the artifact bytes, stages, atomic-switches via `rename()` of `current.txt`, appends the rollback-log event, bumps the per-user state, GCs stale version directories. Rollback flips `current.txt` back and deliberately preserves `max_version_ever_seen` (replay-defence floor never drops). Append-only rollback log refuses to overwrite a corrupt existing log — defends the audit history against a local actor truncating it.

#### Sprint 10 push 8 — release-signing automation + embedded trust store (closes ADR-0013)

The release-side stack. `scripts/release/notarize-macos.sh` (notarytool with bounded retry + 5-minute heartbeat — Apple's queue p99 stalls badly during release windows). `scripts/release/sign-windows.ps1` (signtool against an EV cert in the Windows runner's certificate store). `scripts/release/sign-manifest.sh` (PyNaCl wrapper around the same libsodium the verifier uses). `scripts/release/build-release.sh` orchestrator. Build-time embedded trust store (`SOUXMAR_RELEASE_PUBKEY_HEX` cache var; release CI overrides + the workflow refuses to publish when `build_uses_dev_key() == true`). `docs/SECURITY.md` from scratch (trust-boundary table + signing-flow walkthrough + the three-mechanism story). `docs/adr/0014-release-signing-key-rotation.md` (yearly cadence, T-90/T-60/T-30/T+0 procedure, emergency-collapse rule, four-eyes destruction).

#### Sprint 10 push 9 — Provider abstraction + OllamaProvider + souxmar-eval-llm runner

Opens AI's "Local-Ollama provider verified across Llama-3.x, Qwen-2.x, Mistral" L story. `souxmar::ai::Provider` interface — synchronous `chat_completion()` returning `variant<ChatResponse, ProviderError>`. Two concrete implementations: `StubProvider` (programmable reply table for CI) + `OllamaProvider` (talks to `localhost:11434/api/chat` via curl-as-subprocess; reuses Sprint 8 push 1's harness so libsouxmar-ai doesn't link an HTTP client). Hand-rolled JSON request encoder; yaml-cpp parses the response. New `souxmar-eval-llm` binary — separate from `souxmar-eval` so the scripted CI gate's stability doesn't tangle with the LLM-driven compatibility-matrix's. `docs/ai-providers/ollama-compatibility.md` with per-model pass-rate matrix.

#### Sprint 10 push 10 — desktop onboarding wizard + Tauri 2 scaffold

Opens Desktop's "Onboarding flow: first-launch wizard, BYOK key entry, sample project" L story. `src/desktop/` from scratch: Tauri 2 Rust shell + React 18 + TS + Vite frontend. Four-step wizard (Welcome / BYOK / SampleProject / Done) with token-driven dim-theme styling. Five `#[tauri::command]` entry points: `onboarding_status`, `onboarding_complete`, `byok_store_key` (writes via the `keyring` crate to OS keychain), `byok_test_connection`, `open_sample_project` (copies `examples/<which>` to `~/souxmar-projects/<which>`). UX rationale documented in `docs/DESKTOP_APP.md` § "First-run experience"; visual-regression process referenced; explicit "what's still scaffolding" section names the stubs the Sprint 11 dogfood week will replace.

#### Sprint 10 push 11 — mesh-algorithm comparison study (fourth in-tree example)

Closes DX's "Fourth example: mesh-algorithm comparison study (uses two meshers)" M story. `examples/mesh-comparison/` runs both `mesher.tetra.grid` (always-on) and `mesher.tetra.gmsh` (opt-in) against the same `cube.step`, hand-parses per-cell quality DataArrays out of the resulting VTUs (no vtk-python dep — a stock-Python user can run this), renders `report.md` with inline-PNG histograms (matplotlib optional, ASCII-bar fallback). Sets up the plugin-marketplace "does this mesher actually deliver?" evidence shape — a Sprint 11+ extension parameterises the study so any third-party `mesher.tetra.*` plugin slots in without rewriting `compare.py`.

#### Sprint 10 push 12 — Sprint 10 retro + v0.9.0-beta4 release (this commit)

Closes Sprint 10. `docs/retros/sprint-10.md` follows the Sprint 8/9 retro shape (keep / fix / one-ADR-worthy-decision / risk-register diff / capacity forecast). One ADR-worthy decision queued: **extract `libsouxmar-crypto`** from the auto-updater's verifier + trust-store surfaces before plugin marketplace (Sprint 16) + cloud sync (Sprint 15) each grow their own crypto surface (ADR-0015 candidate; Sprint 11 push 1). `README.md` Status banner refreshed to `v0.9.0-beta4`; "What changed since" rewritten from the Sprint 9 list to a Sprint 10 list.

Risk register diff:

- **R-011 (perf gate has empty baselines)** — stays closed (push 1).
- **R-012 (desktop app has zero visual-regression coverage)** — **opens** with push 10. Mitigation queued for Sprint 11 push 1.

### Changed

- (None this release — `[Unreleased]` reopens after the v0.9.0-beta4 cut below.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## [0.9.0-beta3] - 2026-05-11

Third public pre-release. Source + Linux CLI tarball + Python sdist published as a GitHub release. **Tag:** `v0.9.0-beta3`. **ABI:** v1.3 frozen (per-face-tag surface from ADR-0012 — the third additive minor in the 1.x line, after the Sprint 6 push 4 `reader.*` v1.0 → v1.1 and the Sprint 7 push 3 mmap-buffer v1.1 → v1.2 ratchets). **Tool contract:** v1 frozen final at 18 tools (ADR-0011, supersedes the freeze-candidate ADR-0010).

Sprint 9 closes here. Everything below this header was the `[Unreleased]` block as of beta2 → beta3; it now snapshots what shipped in this release. Per-push prose is preserved verbatim from the development-time `[Unreleased]` entries — release notes link to the matching `docs/retros/sprint-09.md` for the keep/fix/one-ADR-worthy-decision narrative + risk-register diff + Sprint 10 capacity forecast.

### Added

#### Sprint 9 push 10 — AI BYOK latency budget enters the eval suite

Closes the AI-team named SPRINT_PLAN.md story for Sprint 9 ("Latency budget enforcement: p95 first-token < 800 ms BYOK direct"). The Sprint 7 push 4 eval runner now captures per-step wall-clock latency, aggregates p50 / p95 / p99 / mean / max across every dispatched step in every task, writes the result to a JSON the perf dashboard renders, and optionally fails the run on a p95 threshold. The scripted-eval surface today measures dispatcher overhead (microseconds); the same measurement carries first-token latency once the LLM provider integration lands and the 800 ms budget from `ENGINEERING_PRACTICES.md` kicks in. **No frozen-header surface touched** — eval tool + workflow + doc change; ABI v1.3 stands.

- **`tools/eval/main.cpp`** — `TaskRunResult` grows `step_durations_ms` + `step_tool_names` parallel vectors (one entry per `dispatch_tool` invocation in source order). `run_task` brackets the dispatch call with `std::chrono::steady_clock` so the per-step number is exactly what the agent runtime would observe in production. Aggregation runs after every task completes; the runner accumulates across the suite into one global vector + one per-tool map.
- **Two new CLI flags** on `souxmar-eval`:
  - `--latency-output <path>` writes a JSON aggregate: `unit`, `n_steps`, `aggregate.{p50,p95,p99,mean,max}`, and `per_tool.<tool>.{n,p50,p95,mean,max}`. The format is fixed so a future perf-dashboard tile can ingest it without re-parsing the text report.
  - `--max-p95-ms <N>` makes the runner exit with code 4 (`kExitLatencyFailed`, distinct from `kExitTaskFailed = 1`) when aggregate step p95 exceeds N milliseconds. The workflow can route the two failure modes differently — a latency regression flags "perf review" rather than "agent capability regressed".
- **Percentile helper.** Nearest-rank `percentile(sorted, q)` with explicit clamping at the bounds. Right shape for the 30-task / ~50-step eval scale today — finer interpolation adds complexity without changing the regression signal — and the same routine handles the per-tool slices.
- **Always-on text summary** even without the new flags. Every run now prints a `--- step latency (ms) ---` block with `n=… p50=… p95=… p99=… mean=… max=…`. Reviewers reading the existing nightly artifact bundle see the numbers without needing to opt in.
- **`.github/workflows/eval-nightly.yml`** — Run step appends `--latency-output eval-latency.json`; Upload step gains `eval-latency.json` alongside the existing `eval-report.txt`. The artifact bundle is now (text report, latency JSON) instead of just the text. `--max-p95-ms` is intentionally left unset for one nightly soak — the scripted-eval p95 will settle on the runner's hardware over a few runs, and the gate value lands in a follow-on PR once the team picks a defensible threshold.
- **`docs/AI_INTEGRATION.md` § Latency budgets** — new section. Names the two `ENGINEERING_PRACTICES.md` budgets that govern the chat experience (BYOK < 800 ms p95, managed < 1200 ms p95), describes the two-tiered measurement (dispatcher path today, full first-token once the LLM integration ships), and documents the `--latency-output` JSON format + the `--max-p95-ms` gate.

What the latency JSON looks like (one nightly run, abridged):

```json
{
  "unit": "ms",
  "n_steps": 84,
  "aggregate": { "p50": 0.082, "p95": 0.341, "p99": 1.205, "mean": 0.124, "max": 1.872 },
  "per_tool": {
    "mesh":     { "n": 12, "p50": 0.45, "p95": 1.21, "mean": 0.58, "max": 1.87 },
    "set_bc":   { "n": 24, "p50": 0.04, "p95": 0.09, "mean": 0.05, "max": 0.12 },
    "solve":    { "n": 12, "p50": 0.31, "p95": 0.78, "mean": 0.40, "max": 0.92 },
    ...
  }
}
```

The same JSON shape carries through to the LLM-driven future: `provider_first_token_ms` joins the `per_tool` entries (or the top level, depending on whether the model emits a single tool-call per turn) once provider integration lands. The `--max-p95-ms` gate against the BYOK 800 ms budget kicks in at the same moment.

The Sprint 9 perf-budget enforcement coverage is now:

| Budget                                                | Enforced by                            | Status                              |
| ----------------------------------------------------- | -------------------------------------- | ----------------------------------- |
| Plugin call overhead (no-op tool) < 20 µs warm        | `bench_plugin_dispatch` + Perf gate    | live (push 7)                       |
| Per-plugin heap accountant overhead < 1 µs            | `bench_heap_accountant` + Perf gate    | live (push 9)                       |
| Per-face-tag sparse map constant-time vs. mesh size   | `bench_face_tag` + Perf gate           | live (push 6)                       |
| Mesh construction / mmap-buffer regressions < 5 %     | `bench_mesh_construction` / `bench_mmap_buffer` | live (push 6)              |
| First chat token (BYOK direct) < 800 ms p95           | `souxmar-eval --max-p95-ms`            | infrastructure live (push 10); gate value sets when LLM integration lands |

The remaining Sprint 9 themed item — Core's "Assembly hot-path SIMD pass + PETSc handle pooling" — is L-sized and requires a real FEM assembly path to optimise against; the Sprint 7 push 2 `fenicsx-solver` is opt-in and the in-tree elasticity-stub is closed-form, so there's no real assembly hot path to SIMD-ify yet. Deferred to the sprint that lands a production FEM solver path.

#### Sprint 9 push 9 — per-plugin heap accounting (the audit log grows a leak indicator)

Closes the Plugin-Host-team named SPRINT_PLAN.md story for Sprint 9 ("Per-plugin heap accounting; report leaks via instrumentation"). The agent tool dispatcher now brackets every handler call with a heap snapshot pair from the new `souxmar::plugin::HeapAccountant`; the delta lands in the audit log alongside the existing duration / outcome / budget fields, surfacing tool-side memory growth at the granularity the agent UI and `souxmar audit show` consume. **No frozen-header surface touched** — new utility + new audit-log field + new test + new benchmark; ABI v1.3 stands.

- **`include/souxmar/plugin/heap_accountant.h` + `src/plugin-host/heap_accountant.cpp`** — new utility under `souxmar::plugin`. Three static methods:
  - `Sample snapshot()` — process-wide in-use heap bytes via `mallinfo2().uordblks` on glibc ≥ 2.33; returns `{0, supported=false}` on macOS / Windows / older glibc. The compile-time predicate (`__GLIBC__` + `__GLIBC_MINOR__` ≥ 33) deliberately excludes the legacy `mallinfo()` path — its `int` fields silently truncate above ~2 GiB and would misreport industrial-scale meshes as having freed memory. Better to surface "unsupported" than to ship wrong numbers.
  - `bool is_supported()` — runtime query; matches what `snapshot()` will return.
  - `std::int64_t delta_since(const Sample&)` — signed delta; returns 0 if either side reports `supported=false` so the audit-log field stays unambiguous (absent vs. zero are different signals).
  Accuracy caveat documented in the header: `mallinfo2` is process-wide, so deltas in multi-threaded sessions also capture sibling-thread allocations. For leak-detection use (the primary motivation) the recommended audit configuration is `max_workers=1`.
- **`include/souxmar/ai/audit_log.h` Entry growth.** Two new fields: `std::int64_t heap_bytes_delta` and `bool heap_supported`. Default 0 / false so existing callers compile unchanged. `src/ai/audit_log.cpp` serialises `heap_bytes_delta: <int>` after the `budget: {...}` block when `heap_supported` is true — absent on platforms without accounting so absence is not confused with a zero reading.
- **`src/ai/tool_dispatcher.cpp` integration.** `dispatch_tool` takes a heap snapshot immediately before invoking the tool handler and computes the delta after; `record_audit` threads both through to the audit log. Early-exit paths (NOT_FOUND, NOT_CONFIRMED, DENIED) don't run a handler, so they record `heap_supported=false` instead of a misleading zero. The handler-invocation path always records the real reading — including when the handler throws (the snapshot pair sits outside the `try` block so a thrown exception still gets accounted for).
- **`tests/unit/test_heap_accountant.cpp`** — eight tests across two tiers. Tier-1 (every platform): `is_supported()` agrees with `snapshot().supported`; `delta_since(unsupported)` returns 0; two consecutive snapshots on a quiet thread differ by less than 1 MiB. Tier-2 (Linux + glibc ≥ 2.33, guarded by the same compile-time predicate as the impl): a deliberate 1 MiB `std::malloc` shows up as ≥ 1 MiB delta; matched alloc+free returns to near-zero (within 1 MiB tolerance for glibc's per-arena caching); an 8 MiB `std::vector<double>` shows up as ≥ 8 MiB delta. Tier-2 tests `GTEST_SKIP` on non-glibc platforms — the unit suite stays green on the full CI matrix.
- **`benchmarks/bench_heap_accountant.cpp`** — three Google Benchmark workloads: `BM_HeapAccountant_Snapshot` (one snapshot per iteration), `BM_HeapAccountant_DeltaPair` (snapshot + delta_since pair, mirrors what the tool dispatcher does on every call), `BM_HeapAccountant_IsSupported` (cold-path sanity). Units forced to `kNanosecond` so the report column is human-readable against the < 1 µs target the accountant needs to hit to be safe always-on. Wired into the benchmark suite via `benchmarks/CMakeLists.txt`, the Perf workflow's run loop, and the baselines coverage table.
- **`docs/AI_INTEGRATION.md` § Cost and budget controls** — Audit log bullet updated to name the new `heap_bytes_delta` field, the supporting-platform list (Linux + glibc ≥ 2.33 today), and the leak-detection use case + single-threaded recommendation.
- **`src/plugin-host/CMakeLists.txt`** — `heap_accountant.cpp` joins the `souxmar_plugin` source list. No new external dependency (`<malloc.h>` is glibc-stdlib).

What the audit log looks like now (line-by-line YAML; the new field surfaces only on supported platforms):

```yaml
{ts: 2026-05-11T14:23:01Z, tool: mesh, outcome: ok, duration_ms: 184, input_hash: "abc...", budget: {in: 1240, out: 320, total: 1560, max_total: 200000}, heap_bytes_delta: 4194304, summary: "meshed 50000 cells"}
```

Reviewers can grep `heap_bytes_delta:` across a session log and sum / sort by tool to find the heaviest allocators or spot a tool whose delta grows monotonically across repeated calls. The push-8 dashboard's per-binary cards don't render audit deltas directly (the audit log is per-session, not per-benchmark), but `bench_heap_accountant` keeps the *accountant itself* under the 5% perf gate so the always-on cost stays bounded.

The Sprint 9 perf-coverage roster now reads:

| Benchmark binary           | Surface                                              | Named budget                                  |
| -------------------------- | ---------------------------------------------------- | --------------------------------------------- |
| `bench_mesh_construction`  | Per-element vs. bulk mesh construction               | (no named budget — relative gate only)        |
| `bench_mmap_buffer`        | Heap vs. mmap buffer round-trip (ADR-0006 v2)        | (no named budget — relative gate only)        |
| `bench_face_tag`           | Per-face-tag sparse map (ADR-0012, ABI v1.3)         | constant-time vs. mesh size (push 6)          |
| `bench_plugin_dispatch`    | `RegistryDispatcher` hot path                        | < 20 µs warm (`ENGINEERING_PRACTICES.md`)     |
| `bench_heap_accountant`    | `mallinfo2` snapshot + delta pair                    | < 1 µs to keep always-on accounting cheap     |

#### Sprint 9 push 8 — benchmark dashboard published per release

Closes the DX-team named SPRINT_PLAN.md story for Sprint 9 ("Benchmark dashboard published per release"). The push 6 gate now produces a human-readable artifact alongside the machine-readable JSON: a self-contained HTML report that release notes can link to directly. Engineers reviewing a perf-regression PR get a one-page dashboard with red / green badges per binary instead of trawling through three JSON files in the artifact bundle. **No frozen-header surface touched** — new tool + workflow step; ABI v1.3 stands.

- **`tools/perf-compare/dashboard.py`** — new stdlib-only Python script (no `pip install` required on CI runners; same constraint as the existing `compare.py`). Renders `perf-report/*.json` to a single HTML file:
  - **Self-contained.** Inline CSS, inline SVG, no JavaScript, no external fonts. Attach to a GitHub Release, view offline in any modern browser, email to a reviewer — all of those work without further setup.
  - **Twitter-dim palette.** Uses the project's UI design tokens (`#15202B` base, `#1D9BF0` accent, `#F4212E` regress, `#00BA7C` improve) so the dashboard reads as souxmar-native rather than a generic Google-Benchmark dump.
  - **Per-binary cards.** One `<section>` per benchmark binary, with a header badge that surfaces the binary's overall state at a glance: "new — no baseline yet", "removed — no current report", "regression", or "improvement" (the last two appear when at least one workload in the binary tripped the threshold either way).
  - **Per-workload rows.** A table per binary with columns for benchmark name, iteration count, real time (rendered in the Google-Benchmark-reported unit — ns / µs / ms), delta-vs-baseline as a coloured pill, and an inline-SVG bar chart sized against the binary's own slowest workload.
  - **Threshold = the gate's threshold.** Defaults to 0.05 to match `docs/ENGINEERING_PRACTICES.md` § Performance budgets and Sprint 9 push 6's `REGRESSION_THRESHOLD` env var. Anything beyond the threshold is rendered red (regression) or green (improvement); within-noise rows render muted.
  - **Defensive against partial input.** A new binary without a baseline gets a "new — no baseline yet" badge instead of dropping out; a baseline file without a matching current report gets a "removed" badge; a malformed JSON file surfaces an error placeholder rather than crashing the run. Same carve-outs `compare.py` honors.
  - **Header.** Generation timestamp (UTC), git ref (passed via `--git-ref`), threshold value. Footer points at `docs/ENGINEERING_PRACTICES.md` § Performance budgets and the souxmar repo.
  Smoke-tested locally against synthetic JSON inputs covering the happy path (within-threshold), the regression path (red badge + red delta pill), the new-benchmark path (blue "new" badge), and the removed-benchmark path. The HTML rendered each case correctly.
- **`.github/workflows/perf-nightly.yml`** — new "Render benchmark dashboard" step between the comparison and the artifact upload. Runs with `if: always()` so a regressed comparison still produces a dashboard with the red badge — reviewers can see *what* broke without manually downloading the JSON artifact. The dashboard is written to `perf-report/dashboard.html` and rolls into the existing artifact upload (so the bundle now carries both the JSONs and the HTML). Title threads `${{ github.ref_name }}` and git ref threads `${{ github.sha }}` so a release-tagged run produces a report titled `souxmar benchmark report — v0.9.0-rc1` with the matching commit SHA in the header.
- **`benchmarks/baselines/README.md`** — new "Dashboard" section documenting the rendering pipeline + local regeneration loop. Notes the rule that the dashboard's red badges and the gate's failures share the same logic — there's one source of truth for "did this regress" and the dashboard is the visual presentation of it, not an independent judgment.

The full Sprint 9 perf-coverage stack now reads:

| Layer        | Where                                          | What                                                    |
| ------------ | ---------------------------------------------- | ------------------------------------------------------- |
| Gate         | `.github/workflows/perf-nightly.yml`           | Per-PR + nightly run; fails on > 5 % regression.        |
| Comparator   | `tools/perf-compare/compare.py`                | Directory-mode JSON diff, threshold-driven exit code.   |
| Dashboard    | `tools/perf-compare/dashboard.py`              | HTML rendering of the same data, published per release. |
| Baselines    | `benchmarks/baselines/*.json` (committed)      | Per-binary expected numbers; rotated by deliberate PR.  |
| Bench suite  | `benchmarks/*.cpp` (4 binaries)                | mesh-construction, mmap-buffer, face-tag, plugin-dispatch. |

The Sprint 9 themed work has now landed three pushes worth of perf machinery: gate hardened (push 6), absolute-budget enforcement enters the gate (push 7), human-readable artifact for review (push 8). The next themed pushes can pick from the remaining named SPRINT_PLAN.md items — Core's SIMD pass, Plugin Host's heap accounting, AI's BYOK latency — each of which can now add its benchmark binary and have it automatically picked up by the gate, the comparator, and the dashboard without further infrastructure work.

#### Sprint 9 push 7 — `bench_plugin_dispatch` (the 20 µs warm budget enters the gate)

First push to land a benchmark that enforces a named `ENGINEERING_PRACTICES.md` § Performance budgets entry. The "Plugin call overhead (no-op tool) < 20 µs (warm)" line has been on the books since Sprint 0 but had no measurement against it; Sprint 9 push 6's gate infrastructure made the natural follow-on a concrete coverage push. **No frozen-header surface touched** — new bench + workflow loop entry; ABI v1.3 stands.

- **`benchmarks/bench_plugin_dispatch.cpp`** — new Google Benchmark binary. Three workloads share a `DispatchHarness` that statically registers a no-op `mesher.noop` vtable into a `plugin::Registry` and builds a `RegistryDispatcher` over it; construction sits outside the timed region, so what's measured is the production hot path: namespace-prefix routing → `find_mesher` lookup → C ABI shim → vtable call → StageOutput wrapping.
  - `BM_PluginDispatch_Warm` — single call per iteration. Target: < 20 µs warm. The CI gate's 5% threshold means a hard regression budget of ~21 µs on the reference hardware.
  - `BM_PluginDispatch_BatchOf32` — 32 calls per iteration. Matches the typical agentic-session "do these 30-ish things in sequence" shape so a regression that only shows up in batched workloads (e.g. an inadvertent per-call allocation that thrashes the small-object heap) surfaces here even if the singleton looks fine.
  - `BM_PluginDispatch_NotFound` — the negative path. The not-found branch short-circuits before touching any vtable, so it should be at least as fast as the hit path; a future regression that ever made the miss path slower would surface as an absolute number rather than a ratio. Useful as a guard against accidental string-comparison churn in the dispatcher.
  Units forced to `kMicrosecond` so the report column is human-readable against the documented budget rather than ns scientific notation.
- **Static-registration harness instead of dlopen.** The harness builds against `souxmar::plugin` + `souxmar::pipeline` and calls `Registry::add_mesher(...)` directly with a constexpr `souxmar_mesher_vtable_t`. Plugin discovery (filesystem walk + `dlopen` + symbol resolution) is a session-amortised cost, not a per-call cost — including it in the per-call measurement would dilute the signal the budget exists to catch. The dispatcher path that runs N times in production is the path the benchmark exercises N times.
- **CMakeLists registration + workflow run-loop entry.** `benchmarks/CMakeLists.txt` gains the new target via the existing `souxmar_add_benchmark()` helper; `.github/workflows/perf-nightly.yml`'s "Run benchmarks" loop appends `bench_plugin_dispatch` to its list so the gate covers all four binaries (`bench_mesh_construction`, `bench_mmap_buffer`, `bench_face_tag`, `bench_plugin_dispatch`). The directory-mode `compare.py` from push 6 picks up the new `perf-report/bench_plugin_dispatch.json` automatically; no comparison-tool change.
- **`benchmarks/baselines/README.md`** — coverage table grows the fourth row; the regenerate-locally loop adds the new binary so a baseline rotation lands all four files together.

This is the first benchmark whose target is an *absolute number* rather than a relative-to-baseline ratio. The 5% regression gate from push 6 still applies (a hot-path slowdown blocks the PR), but the 20 µs warm number is also a reviewable wall the gate can hit independently — once the first baseline rotation lands, a `BM_PluginDispatch_Warm` mean over 25 µs is an audit-log moment even if it's within 5% of the previous baseline. The dispatcher's hot path is the most heavily exercised surface in the entire pipeline runtime; protecting it explicitly is the kind of thing the Sprint 9 "Performance + scale" theme is for.

The Sprint 9 perf-coverage roster now reads:

| Benchmark binary           | Surface                                              | Named budget                             |
| -------------------------- | ---------------------------------------------------- | ---------------------------------------- |
| `bench_mesh_construction`  | Per-element vs. bulk mesh construction               | (no named budget — relative gate only)   |
| `bench_mmap_buffer`        | Heap vs. mmap buffer round-trip (ADR-0006 v2)        | (no named budget — relative gate only)   |
| `bench_face_tag`           | Per-face-tag sparse map (ADR-0012, ABI v1.3)         | constant-time vs. mesh size (push 6)     |
| `bench_plugin_dispatch`    | `RegistryDispatcher` hot path                        | < 20 µs warm (`ENGINEERING_PRACTICES.md`) |

#### Sprint 9 push 6 — perf-regression gate hardened to the `ENGINEERING_PRACTICES.md` target

First push of Sprint 9's themed "Performance + scale" work; closes the Platform team's named story (`SPRINT_PLAN.md` § Sprint 9: "Benchmark suite gates merges (perf regression > 5 % blocks)"). Aligns the CI gate with the `docs/ENGINEERING_PRACTICES.md` § Performance budgets contract that's been on the books since Sprint 0 but only half-enforced. **No frozen-header surface touched** — CI + tooling + new benchmark; ABI v1.3 stands.

Background: Sprint 5 push 5 landed `perf-nightly.yml` with a 10% regression threshold, ran only on PRs touching a small handful of perf-relevant paths, executed only the single `bench_mesh_construction` binary, and had no committed baseline (so per-PR runs always fell through to the "no baseline — skipping" warning). The `ENGINEERING_PRACTICES.md` doc has named 5% as the live target since the founding crew wrote it. This push closes the gap — and adds perf coverage for the Sprint 9 push 2 ABI v1.3 per-face-tag surface, which had landed without any performance enforcement.

- **`benchmarks/bench_face_tag.cpp`** — new Google Benchmark binary. Four workloads pin the ADR-0012 sparse-map promises:
  - `BM_FaceTag_Set` — sparse-map insert throughput. One tag per cell, on a fresh mesh per iteration (using `state.PauseTiming` / `state.ResumeTiming` so the mesh-construction cost doesn't pollute the insert measurement). Catches a future regression that makes `set_face_tag` non-amortised-constant.
  - `BM_FaceTag_GetHit` — populated-tag lookup throughput. Rotates the cell index so the bucket walks aren't hot-cached against one slot.
  - `BM_FaceTag_GetMiss` — **the zero-storage-zero-cost promise**. No face tags set anywhere; every lookup must short-circuit constant-time regardless of mesh size. A future regression that ever made the empty-map lookup grow with mesh size would surface as a >5% delta between the 1K and 100K arg here.
  - `BM_FaceTag_Enumerate` — full `tagged_faces()` cost vs. tag count. Locks in the "scales with tagged-face count, not mesh size" promise.
  Sized at 1K / 10K / 100K cells — same scale step as the existing `bench_mesh_construction` workloads so per-cell costs across reports are directly comparable. Build target wired through the existing `souxmar_add_benchmark()` helper in `benchmarks/CMakeLists.txt`.
- **`tools/perf-compare/compare.py`** — extended with directory mode. Two new flags (`--baseline-dir`, `--current-dir`); a new `compare_single_pair()` helper extracts the per-pair logic so the directory driver can iterate. Files in current-dir without a same-named baseline are reported as "(new — no baseline yet; skipping)" and do **not** fail the gate — required so a PR that introduces a benchmark doesn't itself fail on the missing baseline. Files in baseline-dir without a matching current are reported as "(removed — no current report)" and don't fail either. Default threshold flipped 0.10 → 0.05 to match the ENGINEERING_PRACTICES.md contract. Single-file mode is preserved for ad-hoc / local use. Three smoke-test paths verified manually (happy path, regression, new-bench-only).
- **`.github/workflows/perf-nightly.yml`** — three changes:
  - Workflow name `Perf-nightly` → `Perf`. Reflects that the per-PR gate is now real (the workflow already ran on PRs under a path filter; pre-Sprint-9 push 6 the gate just didn't fail because the threshold was 10% and the baseline was absent).
  - `REGRESSION_THRESHOLD` env var `"0.10"` → `"0.05"`. Matches the `ENGINEERING_PRACTICES.md` § Performance budgets contract.
  - Pull-request path filter widened from the Sprint 5 whitelist (only `c_abi_mesh.cpp` / `c_abi_buffer.cpp` / two frozen-buffer headers / `tools/perf-compare`) to cover any host-side core / pipeline / plugin-host change. UI, docs, AI tools, and examples stay outside the gate by construction — they can't affect benchmark numbers.
  - Per-binary run-and-emit loop. The "Run benchmarks" step now iterates `bench_mesh_construction`, `bench_mmap_buffer`, `bench_face_tag` and writes each to `perf-report/<name>.json`. The "Compare" step uses `compare.py`'s new directory mode so a single comparison run covers the whole suite — no per-binary step duplication, no per-binary skip logic.
- **`benchmarks/baselines/README.md`** — rewritten to document the new layout. Names the three baseline files (one per binary), records the file-stem-as-baseline-name matching rule, names the new-benchmark-no-baseline carve-out, and points reviewers at the `bash for bench in ...` regeneration loop for full-suite rotations. Notes that loosening the threshold requires an RFC.

**No baseline files committed in this push.** The first rotation that lands non-empty `benchmarks/baselines/*.json` files is the natural next push — the workflow already prints "(new — no baseline yet; skipping)" for each binary in the meantime, and PRs that don't regress fall through cleanly. This is the same Sprint 5 "baseline established" exit criterion still pending on real CI hardware data; the infrastructure is now in place so the rotation is a one-PR motion.

The full Sprint 9 push 6 gate behaviour:

| Scenario                                                   | Workflow outcome                                          |
| ---------------------------------------------------------- | --------------------------------------------------------- |
| PR touches a path under the filter, suite all green        | ✅ pass — every binary within 5%                          |
| PR touches a path under the filter, one binary > 5% slower | ❌ fail — comparison step exits 1, "Fail if regressions" surfaces the diff |
| PR touches a path outside the filter                       | ⏭ skip — workflow doesn't run; non-perf surface           |
| PR adds a new benchmark binary                             | ⏭ partial — new bench prints "(new — no baseline)", other binaries still gated |
| Baseline directory completely empty (first run ever)       | ⏭ all-skip — workflow exits 0 with the "no comparisons ran" note |

The Sprint 9 "Performance + scale" theme moves forward from here — Core's assembly hot-path SIMD pass + PETSc handle pooling, Plugin Host's per-plugin heap accounting, and AI's BYOK latency budget enforcement all build on top of this gate's existence. Each can land its own benchmark + baseline pair without further infrastructure work.

#### Sprint 9 push 5 — `pipe-bend.obj` fixture + `usemtl` preservation in `obj-reader`

Closes the last carry-over from the Sprint 8 retro. The pipe-bend example now runs against real geometry — a 12-vertex L-shaped duct read from `examples/pipe-bend/pipe-bend.obj` — instead of the unit-tet placeholder. The Sprint 8 push 3 `obj-reader` is extended to preserve `usemtl` group names as per-cell tags, the natural metadata channel for downstream tetrahedralisers and the `openfoam-solver` per-patch routing chain (Sprint 9 push 3). **No frozen-header surface touched** — pure plugin-internal + new fixture; ABI v1.3 stands.

- **`examples/pipe-bend/pipe-bend.obj`** — new fixture. 12 vertices forming an L-shaped duct: two unit cubes sharing the y=1 face (cube A at x∈[0,1], y∈[0,1], z∈[0,1]; cube B at x∈[0,1], y∈[1,2], z∈[0,1]). The shared y=1 face is internal to the duct and absent from the surface OBJ. The 10 external quads are tagged via three `usemtl` groups:
  - `inlet` — 1 quad at x=0 on cube A (-x normal).
  - `walls` — 8 quads: cube A's +x / -y / -z / +z faces + cube B's -x / +x / -z / +z faces.
  - `outlet` — 1 quad at y=2 on cube B (+y normal).
  Face vertex ordering is CCW from outside the duct, so outward normals are correct for any downstream consumer that respects the polyMesh convention. The fan-triangulation that obj-reader performs (each quad → 2 Tri3 cells) produces a 20-cell surface mesh.
- **`examples/plugins/obj-reader/obj_reader.cpp`** — `usemtl` preservation. `parse_obj` grows a `std::unordered_map<std::string, int32_t>` recording each unique material name's tag id (1, 2, 3, ... in source-file appearance order — deterministic and dependency-free) plus a `current_tag` cursor that updates on every `usemtl X` directive. Every Tri3 cell emitted picks up the cursor's tag in its `tri_tags` parallel vector; the call site forwards that tag to `souxmar_mesh_add_cell`. Faces emitted before any `usemtl` (or in OBJ files with no usemtl directives) carry the untagged sentinel (-1). A bare `usemtl` (no argument) reverts `current_tag` to -1 — matches what some exporters emit when "no material" is meant.
- **`examples/pipe-bend/pipeline.yaml`** — the `mesh` stage swaps from `mesher.tetra.hello` (placeholder unit tet) to `reader.obj` reading `pipe-bend.obj`. Downstream stages (`solve`, `write`) reference the stage by id (`from: mesh`) and stay structurally identical — the plugin name change is the only edit. The default-CI path (cfd-stub solving on the Tri3 surface, shape-agnostic) continues to produce a valid VTU end-to-end; the nightly OpenFOAM path documents what's still pending (gmsh-mesher's cell-tag → face-tag preservation through tetrahedralisation, which lets the v1.3 per-face-tag ABI fully wire OBJ-driven CFD geometry into the openfoam-solver per-patch router).
- **`examples/pipe-bend/README.md`** — updated. The "geometry is a placeholder unit tet" caveat is replaced by an enumeration of the real `pipe-bend.obj` fixture (12 vertices, 10 quads, 3 usemtl groups). The "what this is not (yet)" list now names the surface-vs-volume gap as the remaining work, and the BC-routing bullet records the Sprint 9 push 3 status rather than the Sprint 8 future-tense version. Pipeline-mechanics section updated to mention the per-cell-tag preservation explicitly.
- **`tests/integration/test_obj_reader.cpp`** — two new tests:
  - `UsemtlGroupsBecomePerCellTags` — synthetic 6-cell OBJ with 2 leading untagged faces, then `usemtl inlet` (tag 1), then `usemtl walls` (tag 2), then a re-used `usemtl inlet` (tag 1 reused, *not* allocated as tag 3). Asserts the per-cell tag distribution `{-1, -1, 1, 2, 2, 1}` exactly. Pins the source-order + dedup contract.
  - `PipeBendFixtureTagsAreInletWallsOutlet` — loads the in-tree `examples/pipe-bend/pipe-bend.obj` via `SOUXMAR_TEST_SOURCE_ROOT` and asserts that the 20 emitted Tri3 cells partition exactly as 2 inlet + 16 walls + 2 outlet. Catches any future fixture edit that drifts the per-quad triangulation count or the usemtl ordering.

The cell-tag preservation is forward-compatible with the gmsh-mesher tag-passthrough work named in the README's "what this is not (yet)" section: once gmsh-mesher inherits a Tri3's cell tag onto the tet faces it generates on that surface, the openfoam-solver's Sprint 9 push 3 per-patch router will engage automatically — the patch routing's matching logic already keys off `souxmar_mesh_face_tag` integer values, and the usemtl tag IDs the obj-reader emits are exactly those integer values.

**Closes the Sprint 8 retro carry-over backlog** for Sprint 9. The four named follow-ons (per-face-tag C ABI ratchet [push 2], per-patch BC routing [push 3], mixed-element polyMesh [push 4], pipe-bend.obj fixture + reader-driven mesh [push 5]) have all landed. Subsequent pushes start the new Sprint 9 themed work per `docs/SPRINT_PLAN.md` § Sprint 9 ("Performance + scale").

#### Sprint 9 push 4 — mixed-element polyMesh translator (Tet4 / Hex8 / Prism6 / Pyramid5)

Drops the Tet4-only restriction in `openfoam-solver`'s polyMesh translator. The four linear 3D element types now translate as first-class citizens, including arbitrarily-mixed meshes where Tet4 + Pyramid5 + Prism6 + Hex8 cells share faces in the same `souxmar_mesh_t`. **No frozen-header surface touched** — pure plugin-internal change; ABI v1.3 stands.

This closes the Sprint 8 retro's "Tet4-only restriction in the polyMesh translator is brittle" follow-on. The retro named this work as additive-minor scope: the translator's structure (face-key dedup + owner/neighbour bookkeeping + boundary patch extraction) generalises directly once per-element-type face tables join the file — only the per-element face-vertex tables and the FaceKey shape change.

- **Per-element-type face tables (file-scope `constexpr` arrays).** Each table lists the canonical local face-vertex orderings for one element type, CCW from outside the cell so the face normal points outward (matching the OpenFOAM polyMesh convention + Gmsh / VTK side-set ordering).
  - `kTet4Faces[4]` — 4 triangular faces, opposite-vertex convention (unchanged from Sprint 8 push 6, just lifted to file scope from inside `write_polymesh_from_mesh`).
  - `kHex8Faces[6]` — 6 quadrilateral faces. Vertex ordering matches the VTK_HEXAHEDRON convention souxmar uses internally (verified against the mixed-element test in `tests/unit/test_mesh.cpp`): v[0..3] bottom face CCW from above, v[4..7] top face CCW from above, with v[i] stacked beneath v[i+4]. Bottom face emits as {0,3,2,1} (CCW from -z); top as {4,5,6,7}; sides per the standard right-hand-rule convention.
  - `kPrism6Faces[5]` — 2 triangular caps + 3 quadrilateral sides. v[0..2] bottom triangle, v[3..5] top triangle, with v[i+3] stacked above v[i].
  - `kPyramid5Faces[5]` — 1 quadrilateral base + 4 triangular sides meeting at the apex. v[0..3] base quad CCW from above, v[4] apex.
- **`face_table_for(element_type) → FaceTable`** dispatch. Returns `{pointer, count}` for supported types; `{nullptr, 0}` for the rest (used by the up-front validation to reject quadratic variants and 0D/1D/2D types with a clean diagnostic before any disk I/O).
- **Validation reshape.** Replaces the per-cell `if (cell_type != SOUXMAR_ET_TET4)` check with `face_table_for(et).faces == nullptr` — same fail-fast shape, broader acceptance. The diagnostic message now names the supported set explicitly ("Tet4 / Hex8 / Prism6 / Pyramid5 only — linear 3D elements") and points at the deferred quadratic-variant case.
- **`FaceKey` generalised to variable vertex count.** Carries `uint8_t size` (3 or 4) alongside the sorted vertex array — so a triangular face and a quadrilateral face that happen to share their first three sorted vertex ids never collide. The hash mixes `size` first so dispersion stays good across mixed-element meshes.
- **`FaceEntry` generalised.** `verts_owner` is now `std::array<uint64_t, 4>` + a `vertex_count` field (4 is the max for any linear 3D element's face). The face-emission writer uses `fe.vertex_count` to emit `N(v0 v1 ... vN-1)` lines — polyMesh accepts mixed N within the same `faces` list, so the format just works for mixed-element meshes.
- **Cell-walk rewritten.** Instead of hardcoded `cell_nodes[4]` + `for (int f = 0; f < 4; ++f) kTetFaces[f]`, the loop now: (a) reads `souxmar_mesh_cell_type` per cell, (b) looks up the face table, (c) sizes the cell-nodes scratch via `souxmar_mesh_cell_node_count` (8 for Hex8, 6 for Prism6, 5 for Pyramid5, 4 for Tet4), (d) walks each face in the table, resolves local→global node ids, and dedupes via the sized FaceKey. The scratch buffer is hoisted to the outer scope so it amortises across cells.
- **`examples/plugins/openfoam-solver/openfoam_solver.cpp` scope comment** updated. The "Tet4-only" v1 disclaimer is replaced by an enumeration of supported element types + an explicit note on the deferred quadratic-variant lowering (when a real use case asks for it, a future minor lowers Tet10/Hex20 etc. to their linear corner sets).
- **`examples/pipe-bend/README.md` mechanics section** updated. The "Tet4 → polyMesh translator" subhead now reads "polyMesh translator … generalised in Sprint 9 push 4 to all linear 3D element types"; the technical paragraph names the FaceKey-with-vertex-count discriminant and the quadratic-rejection contract.

The translator's correctness story stays the same: every internal face appears in two cells with consistent vertex sets; every boundary face appears in exactly one cell; the canonical-orientation pick (owner = lower cell index, orientation from the owner's face direction) ensures the normal points owner→neighbour. The only new failure mode the mixed path could introduce — a 3-vertex face from one cell mistakenly deduped against a 4-vertex face from another cell — is closed off by the size-keyed FaceKey.

In-tree unit tests for `openfoam-solver` remain absent by design (the plugin needs `simpleFoam` on PATH). The nightly OpenFOAM matrix continues to exercise the translator end-to-end via subprocess. **Closes the Sprint 8 retro carry-over** for the polyMesh mixed-element work.

#### Sprint 9 push 3 — per-patch BC routing in `openfoam-solver`

Consumes the Sprint 9 push 2 ABI v1.3 per-face-tag surface (ADR-0012). Boundary faces are now grouped by `souxmar_mesh_face_tag` and emitted as one polyMesh patch each, with the OpenFOAM patch type and matching `0/U` + `0/p` boundaryField sections driven by the BC entries the agent staged via Sprint 8 push 4's `apply_inlet` / `apply_wall` / `apply_outlet` / `set_bc` tools. **No frozen-header surface touched** — pure plugin-internal refactor; ABI v1.3 stands.

- **`examples/plugins/openfoam-solver/openfoam_solver.cpp`** — substantial refactor in the opt-in branch only (default CI doesn't compile this; the always-on `cfd-stub` sibling carries the default agent-eval surface). Five new pieces:
  - `parse_boundary_conditions(inputs)` — pulls the staged BC list off `inputs.boundary_conditions` (a List of Maps; same shape `solve.cpp` has forwarded since Sprint 8). For each BC, resolves the `tag` to an int32_t face tag. Resolution order: read `tag_id` field if present (a number from the BC tools, future-compatible); otherwise `try_parse_int32(tag_name)`. Unresolved BCs fall through silently — the patch they target just doesn't materialise, the agent sees the resulting boundary as untagged "walls".
  - `sanitise_patch_name(in)` — converts an arbitrary BC tag string into a safe OpenFOAM identifier (`[a-zA-Z0-9_]`, letter-leading). Falls back to `<bc_type>_<tag_id>` when the input doesn't sanitise to a valid identifier.
  - Per-face-tag grouping in `write_polymesh_from_mesh`. The translator now walks boundary faces, looks up `souxmar_mesh_face_tag(mesh, owner, owner_local_face)` per face, and groups by integer tag. BC-matched tags get patches in the order the BC list staged them; unmatched tags get sorted-by-id `tag_<n>` patches; untagged faces fall through to a single legacy "walls" patch (non-breaking against meshes that never set per-face tags — every Sprint 8 example continues to work).
  - `BoundaryPatch` — one entry per emitted patch, carrying the patch name, the OpenFOAM patch type (`wall` for walls / no-tag, `patch` for inlets and outlets), the souxmar BC type that drove it, and the matched ParsedBC pointer (or nullptr for the unmatched/legacy paths). Threaded back through to the U/p writers as an out-parameter.
  - `write_initial_U(work, patches)` and `write_initial_p(work, patches)` — replace the previous fixed-walls `boundaryField` blocks with one entry per patch. Inlet → `type fixedValue; value uniform (vx vy vz);` (velocity read from the BC, scalar or 3-vector); wall (no_slip / wall_function) → `fixedValue (0 0 0)`; wall (slip) → `type slip;`; outlet (pressure_outlet) → U `zeroGradient`, p `fixedValue p_val`; outlet (outflow / fully_developed) → both `zeroGradient`. Walls + untagged-fallback paths emit the canonical p `zeroGradient` for incompressible flow.
- **`FaceEntry` carries `owner_local_face`** (0..3 for Tet4 — the cell-local face index that originated the canonical face). The first claimant writes its `f`; if a lower-indexed second claimant takes over as owner, `owner_local_face` is updated to the new owner's local face index. Without this, the per-face-tag lookup couldn't reach the right (cell, local_face) pair after face dedup.
- **Boundary-face reordering**. Once the patch list is built, `boundary_faces` is rewritten so each patch's faces are contiguous in the final ordering. This is required by OpenFOAM's polyMesh contract: the `boundary` file's `(startFace, nFaces)` ranges have to be valid slices of the `faces` list. The reorder runs once, post-grouping, after the internal/boundary partition + the per-(owner, neighbour) sort that establishes the polyMesh canonical face order.
- **`examples/pipe-bend/README.md`** — "BC routing is uniform-wall only" replaced by an honest description of the v1.3 per-patch path. Notes that meshes built without per-face tags (the current `mesher.tetra.hello` placeholder is one) keep the Sprint 8 fallback behaviour automatically, and the full per-patch routing engages once `obj-reader` lands per-face tags from `usemtl` group names (planned alongside the `pipe-bend.obj` fixture in a follow-on push).
- **Plugin-file scope comment** updated. The "Single 'walls' boundary patch" v1 disclaimer is removed; the new per-patch routing path is documented in its place. Tet4-only remains the explicit v1 constraint (mixed-element follow-on is the next push per the Sprint 8 retro plan).

The whole change lives inside one plugin's source file and one example README; no host code touches, no ABI surface change, no test refactor. The integration test path (Sprint 8 push 2's nightly OpenFOAM matrix) continues to exercise this code end-to-end via subprocess; in-tree unit tests for the openfoam-solver remain absent by design (the plugin needs an actual `simpleFoam` binary). **Unblocks Sprint 9 push 5** — once `pipe-bend.obj` lands with per-face tags from `obj-reader`'s `usemtl` group preservation, the pipe-bend example will run with real inlet/outlet/wall semantics through the full mesh → polyMesh → simpleFoam → readback chain.

#### Sprint 9 push 2 — per-face-tag C ABI ratchet (v1.2 → v1.3)

The third additive-minor ratchet on the frozen v1 ABI, mirroring the precedent set by Sprint 6 push 4 (`reader.*` surface, v1.0 → v1.1) and Sprint 7 push 3 (mmap-backed buffer protocol, v1.1 → v1.2). Bumps `SOUXMAR_ABI_VERSION_MINOR` from 2 to 3. No existing declaration moves; no struct layout changes; no SOUXMAR_ET_* numeric value changes — a strict additive minor per ADR-0008 with the `Ratchet: additive minor surface (ADR-0008)` commit marker.

Background: the Sprint 8 retro named per-face tags on the C ABI as the most likely Sprint 9 ratchet event. Three downstream consumers want it — `openfoam-solver` boundary-patch routing (Sprint 9 push 3 blocker), the `mesh-quality` postproc's per-tag diagnostics, and the readers' tag-preservation work (`obj-reader` discards `usemtl` groups today; `occt-reader` discards face-level OCCT entity ids; `blender-reader` discards named-collection metadata). Per-cell tags are insufficient because the same volume cell carries different boundary conditions on different faces.

- **ADR-0012** (`docs/adr/0012-per-face-tag-c-abi-ratchet.md`) — accepted. Documents the three-function additive surface, the sparse storage contract (only explicitly-tagged faces consume host memory; untagged faces report `SOUXMAR_FACE_UNTAGGED` and pay no cost), and the per-element-type face count taxonomy (matches Gmsh / VTK / OpenFOAM side-set conventions). Names the four alternatives considered (`face_tags` in `souxmar_mesh_buffers_t`; signature-extending `souxmar_mesh_add_cell`; threading face tags through the value-bag; separate opaque handle) and rejects each with reasoning. The pre-mortem covers the most likely failure mode (sparse storage becomes non-trivial at million-face scale → v1.4 bulk path lands additive-non-breaking).
- **`include/souxmar-c/mesh.h`** — three new function declarations and one new constant under `Ratchet: additive minor surface (ADR-0008)`:
  - `SOUXMAR_FACE_UNTAGGED` = `((int32_t)-1)` — sentinel; matches the existing untagged-cell convention.
  - `souxmar_mesh_cell_face_count(mesh, cell_index) → size_t` — bounding-side count derived from the cell's element type (Tet4 → 4, Hex8 → 6, Prism6 → 5, Pyramid5 → 5, Tri3 → 3, Edge*/Vertex → 0). Returns 0 for out-of-range cells / NULL mesh — same fail-soft shape as `souxmar_mesh_cell_type`.
  - `souxmar_mesh_face_tag(mesh, cell, local_face) → int32_t` — sparse-map lookup; returns `SOUXMAR_FACE_UNTAGGED` for unset slots, out-of-range cells, or out-of-range local face indices. Pure getter, never errors — same shape as `souxmar_mesh_cell_tag`.
  - `souxmar_mesh_set_face_tag(mesh, cell, local_face, tag) → souxmar_status_t` — `SOUXMAR_OK` on success, `SOUXMAR_E_NOT_FOUND` for out-of-range cells, `SOUXMAR_E_INVALID_ARGUMENT` for out-of-range local face indices. Setting `SOUXMAR_FACE_UNTAGGED` clears the slot (drops the sparse-map entry, reclaiming memory).
- **`include/souxmar-c/abi.h`** — `SOUXMAR_ABI_VERSION_MINOR` 2 → 3; minor-history comment gets the new v1.3 row pointing at ADR-0012. Same surface as v1.1 / v1.2 ratchets.
- **`include/souxmar/core/element_type.h`** — `ElementInfo` grows a `num_faces` field with the FEM side-set count for each of the 17 element types (host-side C++ header, not under the C ABI freeze — extending in-place is safe). Helper `num_faces(ElementType) → uint8_t` joins the existing `dimension` / `num_nodes` / `order` / `name` helpers.
- **`include/souxmar/core/mesh.h`** — `souxmar::core::Mesh` grows three host-side accessors (`face_tag(CellIndex, uint8_t)`, `set_face_tag(CellIndex, uint8_t, EntityTag)`, `tagged_faces() → vector<TaggedFace>`) and one nested struct (`Mesh::TaggedFace { CellIndex cell, uint8_t local_face, EntityTag tag }`). The setter throws `std::out_of_range` for bad cell indices and `std::invalid_argument` for bad local-face indices; the C ABI wrapper translates these to `SOUXMAR_E_NOT_FOUND` / `SOUXMAR_E_INVALID_ARGUMENT` respectively.
- **`src/core/mesh.cpp`** — `Mesh::Impl` grows a `std::unordered_map<uint64_t, EntityTag>` sparse face-tag store keyed by `pack_face_key(cell, local_face) = (cell << 8) | local_face`. The 8-bit local-face field gives 256 possible faces per cell (max real value is 6 for Hex*); the 56-bit cell field covers 2⁵⁶ ≈ 7.2 × 10¹⁶ cells, well beyond any realistic 1.x release-series mesh size. Sparse-map storage means a mesh with zero tagged faces pays zero bytes for the feature; a mesh with N tagged faces costs O(N) bytes.
- **`src/core/c_abi_mesh.cpp`** — three new `extern "C"` shims wrapping the host-side accessors. `souxmar_mesh_set_face_tag` translates host-side exceptions to ABI statuses (`std::out_of_range` → `SOUXMAR_E_NOT_FOUND`, `std::invalid_argument` → `SOUXMAR_E_INVALID_ARGUMENT`, `std::bad_alloc` → `SOUXMAR_E_OUT_OF_MEMORY`, anything else → `SOUXMAR_E_INTERNAL`). NULL-mesh handling is fail-soft for the getters (return `SOUXMAR_FACE_UNTAGGED` / 0) and `SOUXMAR_E_INVALID_ARGUMENT` for the setter — same idiom as the existing surface.
- **Unit tests.** `tests/unit/test_mesh.cpp` gains 8 new host-side tests covering the default-untagged invariant, set-and-read round-trips, overwrite semantics, clearing via the sentinel, out-of-range cell + local-face indices (with the documented exception types), `tagged_faces()` enumeration coverage, and `num_faces()` for every element type. `tests/unit/test_c_abi_handles.cpp` gains 7 new C ABI tests covering the same surface through the C accessors plus NULL-mesh safety on both the getter and setter paths.

The third minor ratchet on the frozen v1 ABI ships under exactly the same shape as the first two: ADR + surface + impl + tests + commit marker + non-breaking by construction. Conformance check C004 already catches the "v1.3 plugin on v1.2 host" mismatch at registration time, so existing plugins continue to load unchanged and new plugins that opt into the surface fail-clean on older hosts. **Unblocks Sprint 9 push 3** — per-patch BC routing in `openfoam-solver` now has the mesh-side surface it needs.

#### Sprint 9 push 1 — tool-contract v1 frozen FINAL (soak complete, gate flipped to blocking)

Mirrors the ABI v1 final-freeze pattern from Sprint 7 push 1 (ADR-0007 → ADR-0008) for the agent tool surface. The ADR-0010 freeze-candidate period that began Sprint 8 push 5 ran across the remainder of Sprint 8 (push 6: real Tet4 → polyMesh translator + `examples/pipe-bend/` + v0.9.0-beta2 release) with **zero ratchet events** — no new tools, no new `ToolContext` fields, no schema-doc edits beyond commentary, and no breaking change requests opened against the catalogue. The candidate has cleared every gate ADR-0010 named.

- **ADR-0011** (`docs/adr/0011-tool-contract-v1-final-freeze.md`) — declares the agent tool contract **frozen final at v1**. Catalogue locks at **18 tools** across categories Read / Mesh / BC / CFD / Material / Solve / Field / Pipeline / Discovery / Export / UI. The framework surface in `include/souxmar/ai/tool.h` (Confirmation enum, ToolError / ToolResult / ToolContext / Tool / ToolRegistry / ConfirmationPolicy field shapes + method signatures; `dispatch_tool`'s five-step contract) is immutable for the entire 1.x release series. The ratchet vocabulary is preserved verbatim from ADR-0010 (`Ratchet: additive tool (ADR-0010)` and `Ratchet: additive context field (ADR-0010)`) so reviewer muscle memory carries through the freeze-final transition unchanged. Supersedes ADR-0010.
- **ADR-0010 marked Superseded** with a one-line pointer to ADR-0011 at the top, mirroring how ADR-0007 was retired by ADR-0008. The History block carries the 2026-05-11 Sprint 9 push 1 transition entry.
- **`scripts/check-tool-contract.sh`** flipped **blocking-by-default**. The candidate-period escape hatch is inverted: the script blocks unless `SOUXMAR_TOOL_CONTRACT_BLOCKING=0` is set in the environment (the only legitimate use is local dry-run inspection by a developer iterating on a ratchet PR; the CI job never sets it). Diagnostic messaging is updated to name ADR-0011 as the binding source and the "REJECTED" rhetoric matches `check-frozen-headers.sh` exactly.
- **`.github/workflows/ci.yml`** gains the `tool-contract-v1-lockdown` job, mirroring the `abi-v1-lockdown` job from Sprint 7 push 1. Runs on every pull_request event, checks out with `fetch-depth: 0`, invokes `scripts/check-tool-contract.sh` against the PR's base + head refs. A PR that quietly drops a tool from `default_v1_tools()`, reorders `ToolContext` fields, or renames a tool now fails CI at the gate rather than at review.
- **`include/souxmar/ai/tool.h`** — the comment over `default_v1_tools()` is updated: the old "More tools join the catalogue with future RFCs" sentence (written before the freeze candidate landed) is replaced by an authoritative pointer to ADR-0011, the locked 18-tool catalogue, and the `scripts/check-tool-contract.sh` enforcement. No type / signature / struct-field change — comment-only, on the freeze commit itself.
- **`src/ai/tools/default_registry.cpp`** — file-header doc + Sprint 8 push 5 trailing comment now point at ADR-0011 (freeze final) instead of ADR-0010 (candidate). The factory list + registration order are unchanged.
- **README** — Status banner updated. "Agent tool contract is a **freeze candidate**" → "Agent tool contract is **frozen FINAL at v1**" with the ADR-0011 link replacing the ADR-0010 link. Companion line about the `check-tool-contract.sh` gate is updated to drop the "flips to blocking when the final freeze ADR lands" hedge and instead names ADR-0011 as the binding source.
- **CHANGELOG header** — the standing note now reads "**ABI v1 is frozen final at v1.2**" (corrected from a stale v1.1 — the v1.1 → v1.2 ratchet landed Sprint 7 push 3 with the mmap-backed buffer protocol but the header never caught up) and appends "**agent tool contract v1 is frozen final at 18 tools**" with the ADR-0011 link.

The ABI v1 surface (15 frozen headers under `include/souxmar-c/`) and the agent tool contract v1 surface (`include/souxmar/ai/tool.h` + the 18-tool catalogue in `src/ai/tools/`) are now both under CI-enforced lockdown. Both ratchets are in place, both are tested by the same shape of script + workflow job, and both will hold for the entire 1.x release series.

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## [0.9.0-beta2] - 2026-05-11

Second public pre-release. Source + Linux CLI tarball + Python sdist published as a GitHub release. **Tag:** `v0.9.0-beta2`. **ABI:** v1.2 frozen (unchanged from beta1). **Tool contract:** v1 freeze candidate (ADR-0010 — soak in progress, final freeze targeted for Sprint 9 push 1).

Sprint 8 closes here. Everything below this header was the `[Unreleased]` block as of beta1 → beta2; it now snapshots what shipped in this release.

### Added

#### Sprint 8 push 6 — real Tet4 → polyMesh translator + `examples/pipe-bend/` + Sprint 8 retro

Closes Sprint 8. Lands the deferred work from push 2 (which named the real polyMesh translator as a mid-sprint follow-on) and the canonical CFD example the sprint was building toward. **No frozen-header surface touched** — `souxmar-c/*` unchanged; ABI v1.2 stands.

- **`examples/plugins/openfoam-solver/openfoam_solver.cpp`** — `write_placeholder_polymesh()` is gone. Replaced by `write_polymesh_from_mesh(work, mesh)`: walks every Tet4 cell, enumerates its 4 faces via the OpenFOAM tet face convention (`{(1,2,3), (0,3,2), (0,1,3), (0,2,1)}` — CCW from outside, normal-out by construction), builds canonical sorted-vertex keys to deduplicate, partitions internal-from-boundary, sorts internal by `(owner, neighbour)` per the polyMesh ordering rule, and emits all five files: `points` (from `souxmar_mesh_node`), `faces` (canonical vertex order from the owner cell), `owner` / `neighbour` (parallel arrays), `boundary` (single "walls" patch covering every boundary face). The scope comment in the plugin's header is updated: Tet4-only is now the explicit v1 constraint (mixed-element support is the named Sprint 9 follow-on; the translator's structure generalises directly once Pyramid5/Prism6/Hex8 face tables join).
- **`examples/pipe-bend/`** — three-stage YAML (`mesher.tetra.hello → solver.cfd.simple → writer.vtu`) + a README walking through the default-CI shape (cfd-stub serves `solver.cfd.simple`; uniform velocity output) and the nightly-matrix shape (openfoam-solver same capability, real simpleFoam invocation, real polyMesh translation via push 6's work). The geometry stage is still the unit-tet placeholder — `pipe-bend.obj` + reader-driven mesh is a Sprint 9 task per the retro.
- **`docs/retros/sprint-08.md`** — keep/fix/one-ADR-worthy-decision per the SPRINT_PLAN.md retro practice. Names per-face-tag C ABI exposure as the most likely Sprint 9 ratchet event (it unblocks per-patch BC routing in `openfoam-solver`, the mesh-quality postproc's face-tag diagnostics, and the readers' named-collection / `usemtl` preservation). Capacity forecast: ~55 pts again, with 24 pts of Sprint 8 follow-on carry.
- **`VERSION` unchanged** (still `0.9.0` — the SemVer pre-release suffix `-beta2` lives in the git tag, not the `VERSION` file).
- **README** — refreshed banner advertising the `v0.9.0-beta2` tag + the additions since beta1 (subprocess harness, OpenFOAM adapter, Blender importer, OBJ reader, CFD-aware tools, planner + validator, tool-contract candidate, polyMesh translator). 18 tools in the agent catalogue; 12 in-tree plugins (10 always-on + 2 opt-in).

#### Sprint 8 push 5 — CFD planner + BC validator (catalogue 16 → 18); ADR-0010 tool-contract v1 freeze candidate

Two new agent tools that close out the v1 catalogue at 18, plus the
**ADR-0010 freeze candidate** for the agent tool contract — mirroring
the ABI candidate-then-final process from ADR-0007 → ADR-0008. **No
frozen-header surface touched** — `souxmar-c/*` unchanged; ABI v1.1
stands.

- **`tools/propose_cfd_setup.cpp`** — `{goal, tags?, regime?, target_velocity?}`. Heuristic planner: maps an optional list of mesh boundary tags onto an inlet/wall/outlet trio (matches `/in|inlet|inflow/i` for the first, `/out|outlet|exit/i` for the last), recommends a solver capability per regime (`incompressible` → `solver.cfd.simple`; `compressible` → `solver.cfd.openfoam.pimple`; `multiphase` → `solver.cfd.openfoam.inter`). Returns a dispatch-ready plan (`[{tool, input}, ...]`) the agent walks through. Pure planner — read-only, deterministic, replayable, easy to score against the agent eval suite.
- **`tools/validate_bcs.cpp`** — stateless sanity check on `session_state.boundary_conditions`. Reports errors (malformed entry, missing tag/type, duplicate tag with conflicting types, `pressure_outlet` without a numeric pressure) and warnings (no inlet, no outlet, empty session). Returns per-type counts so the LLM can decide what's missing. Confirmation::Auto — agents loop on it during BC iteration.
- **ADR-0010** (`docs/adr/0010-tool-contract-v1-freeze-candidate.md`) — declares the agent tool contract a **freeze candidate** at 18 tools. Locks the framework surface in `include/souxmar/ai/tool.h` (Confirmation enum, ToolError / ToolResult / ToolContext / Tool / ToolRegistry / ConfirmationPolicy field shapes + method signatures; `dispatch_tool`'s five-step contract). Locks per-tool **name**, **category**, **confirmation tier**, and **schema shape** for every tool in `default_v1_tools()`. Tool *descriptions* (the LLM-facing blurbs) stay editable as documentation. Ratchet allows additive new tools (`Ratchet: additive tool (ADR-0010)`) and additive optional `ToolContext` fields (`Ratchet: additive context field (ADR-0010)`). Final freeze target: Sprint 9 push 1, gated on a clear soak.
- **`scripts/check-tool-contract.sh`** — CI guard that detects modifications to `include/souxmar/ai/tool.h` / `src/ai/tools/default_registry.cpp` and demands the matching ratchet marker. Runs **non-blocking** during the candidate period (prints a warning + exits 0) and flips to blocking via `SOUXMAR_TOOL_CONTRACT_BLOCKING=1` when the final-freeze ADR lands.
- **Tests** (`tests/unit/test_ai_tools.cpp`): generic-pipe planner returns inlet→wall→outlet with the target velocity carried through; tag-list planner picks `inflow_face`/`outflow_face` correctly and emits one apply_wall per wall tag; compressible regime → `pimple`; unknown regime rejected. validate_bcs: empty session → warning-OK; full inlet+wall+outlet → ok=true with right counts; only-walls → ok=true with NO_INLET + NO_OUTLET warnings; same tag staged as inlet then outlet → ok=false with DUPLICATE_TAG. Registry-count assertion updated `16 → 18`.

#### Sprint 8 push 4 — CFD-aware BC tools (catalogue 13 → 16)

Three new agent tools — `apply_inlet`, `apply_wall`, `apply_outlet` — extending the `set_bc` general-purpose surface with CFD vocabulary. Same staging contract as `set_bc` (append to `session_state.boundary_conditions`); the value of the trio is that each entry carries `type: 'inlet' | 'wall' | 'outlet'` plus the canonical CFD inputs, so downstream solvers (cfd-stub, openfoam-solver) can pattern-match on `type` instead of parsing free-form Dirichlet/Neumann bags. **No frozen-header surface touched** — `souxmar-c/*` unchanged; ABI v1.1 stands.

Background: until this push, an LLM driving a CFD case had to lower an inlet condition to `{type:'dirichlet', value:[1,0,0]}` via `set_bc`, with no schema hint that the value was a velocity (vs. e.g. a temperature in the same Dirichlet shape). The agent eval suite (Sprint 7 push 4) showed model error rates climbed sharply on CFD prompts where the tool list spoke only FEM vocabulary. These three tools land a CFD-shaped vocabulary that maps to the same downstream BC list — `set_bc` still works (and is preferred for FEM flows).

- **`tools/apply_inlet.cpp`** — `{tag, velocity, pressure?, turbulence_intensity?, hydraulic_diameter?}`. `velocity` accepts a scalar magnitude (inlet-normal) or a 3-vector. Optional turbulence + pressure carry through unchanged to the staged BC. Category `"CFD"`; ConfirmOnce confirmation tier (matches `set_bc`); rejects wrong-shape velocity (e.g. 2-vector) with `INVALID_ARGUMENT`.
- **`tools/apply_wall.cpp`** — `{tag, condition?, temperature?, roughness?}` with `condition ∈ {"no_slip", "slip", "wall_function"}` (default `"no_slip"`). Optional temperature for fixed-temp walls; optional Nikuradse-equivalent roughness for the `wall_function` family. Unknown conditions rejected with `INVALID_ARGUMENT`.
- **`tools/apply_outlet.cpp`** — `{tag, condition?, pressure?}` with `condition ∈ {"pressure_outlet", "outflow", "fully_developed"}` (default `"pressure_outlet"`). `pressure_outlet` requires `pressure`; the other two conditions accept any pressure value (or none) since the solver imposes velocity-side conditions instead.
- **`tools/default_registry.cpp` + `src/ai/CMakeLists.txt`** — three new factory functions + sources, registered after the Sprint 6 push 3 batch. Order is alphabetical-by-namespace which matches `ToolRegistry::list()`'s sort.
- **Tests** (`tests/unit/test_ai_tools.cpp`): scalar-velocity inlet → staged with `type == "inlet"`; vector-velocity inlet → `velocity` round-trips as a 3-list; wrong-shape velocity rejected; missing tag rejected; wall defaults to `no_slip`; wall accepts wall_function + temperature + roughness; unknown wall condition rejected; pressure_outlet missing `pressure` rejected, with pressure accepted; outflow doesn't require pressure; chained `apply_inlet → apply_wall → apply_outlet` on one session produces three coexistent BCs in order. Registry-count assertion updated `12 → 16` and stale comment refreshed.

#### Sprint 8 push 3 — concept-geometry readers (OBJ always-on, Blender opt-in)

Second use of the Sprint 8 push 1 subprocess harness. Lands the always-on / opt-in pair that gets concept-geometry meshes into souxmar — Wavefront OBJ directly, .blend through a Blender subprocess. **No frozen-header surface touched** — `souxmar-c/*` is unchanged; ABI v1.1 stands.

Why this pair: STEP/IGES (Sprint 6 push 4's `occt-reader`) cover production CAD; STL (sibling `stl-reader`) covers tessellated CAD. OBJ + .blend cover the concept-art / DCC funnel — what designers send before any CAD model exists. The same gate keeps the default CI matrix hermetic: always-on closed-form/text parsing, opt-in subprocess for anything heavy.

- **`examples/plugins/obj-reader/`** — always-on. Capability `reader.obj`, plugin id `dev.souxmar.examples.obj-reader`, reentrant. Wavefront OBJ parser: `v` (positions), `f` (faces with all four field forms — `v`, `v/vt`, `v/vt/vn`, `v//vn`), 1-based and negative-index resolution, polygon faces fan-triangulated into Tri3 cells from the first vertex. `vn` / `vt` / `vp` / `o` / `g` / `s` / `mtllib` / `usemtl` are accepted and silently ignored — Blender's OBJ exporter writes all of these, and the parser stays permissive to absorb vendor-specific extensions.
- **`examples/plugins/blender-reader/`** — opt-in. Built only when `SOUXMAR_WITH_BLENDER=ON` **and** `blender` is resolvable on `$PATH` at configure time. Capability `reader.blend`. `find_program(SOUXMAR_BLENDER_BIN blender)` at configure time + `find_executable_on_path("blender")` at plugin-load time (the plugin refuses to register the capability if the binary is missing at load — clean "capability not found" instead of runtime spawn failure). Per-call work directory under `std::filesystem::temp_directory_path()`; invokes `blender -b <input.blend> --python-expr "..."` to run `bpy.ops.wm.obj_export(filepath=..., export_triangulated_mesh=True)` into that work dir, then parses the OBJ back. Mandatory wall-clock timeout (default 5 min); cleaned up unconditionally on every exit path.
- **ADR-0009 compliance, extended to GPL Blender.** No `find_package(Blender)`, no Blender header in souxmar's include path, no link against `libblender.so`. The plugin links `souxmar::plugin` (Apache-2.0 internal C++) for `run_subprocess` and walks `blender` as a child process — the same well-trodden pattern push 2's OpenFOAM adapter follows.
- **Manifest.** `[plugin.blender_required]` carries `version_range = ">=4.0,<5.0"` because the `bpy.ops.wm.obj_export` operator name is canonical from Blender 4.0 onwards (3.4–3.6 expose the same operator under `bpy.ops.export_scene.obj` — a follow-on push can add a version probe if 3.x support is needed).
- **Tests** (`tests/integration/test_obj_reader.cpp`): tetrahedron OBJ → assert 4 nodes / 4 triangles + VTU lands on disk; quad-face OBJ → assert 4 nodes / 2 fan-triangulated cells; tetrahedron with `f v/vt/vn` triples → same shape (verifies the face-field parser's slash-stripping). The Blender adapter itself is exercised on the nightly Blender-bearing matrix (Docker image with Blender 4.x pre-staged), not in default CI.
- **Conformance gate.** `tests/integration/test_conformance.cpp` now also asserts `dev.souxmar.examples.obj-reader` passes all 10 v1 checks. **11 in-tree plugins green.**

#### Sprint 8 push 2 — OpenFOAM adapter (subprocess) + always-on CFD stub

Closes **R-003**. ADR-0009 (Sprint 7 push 5) defined the contract — subprocess invocation only, never `find_package(OpenFOAM)`, never link `libOpenFOAM.so`. Push 1 (below) landed the subprocess harness. Push 2 lands the actual adapter and its closed-form sibling. **No frozen-header surface touched** — `souxmar-c/*` is unchanged; ABI v1.1 stands.

The always-on / opt-in pattern continues: every external dependency ships behind a `SOUXMAR_WITH_*` gate plus an always-on closed-form sibling so default CI stays fast and hermetic. CFD is the fifth domain to land this pattern (OCCT / Gmsh / DOLFINx / readers preceded it).

- **`examples/plugins/cfd-stub/`** — always-on solver. Capability `solver.cfd.simple`, plugin id `dev.souxmar.examples.cfd-stub`, reentrant. Reads `mesh` (stage-ref), `velocity_magnitude` (default 1.0), `flow_direction` (default `[1, 0, 0]`). Writes a single-step nodal vector field `velocity` of `(magnitude · direction)` repeated across every node. No external dependency; runs in microseconds. Used by the conformance gate (10th in-tree plugin) and by Sprint 8 push 4's CFD-aware tool catalogue.
- **`examples/plugins/openfoam-solver/`** — opt-in subprocess adapter. Built only when `SOUXMAR_WITH_OPENFOAM=ON` **and** `simpleFoam` is resolvable on `$PATH` at configure time. Default builds skip the directory entirely (the option defaults `OFF` in `cmake/SouxmarOptions.cmake`). Registers three capabilities — `solver.cfd.openfoam.simple`, `solver.cfd.openfoam.pimple`, `solver.cfd.openfoam.inter` — each probed individually at plugin-load time via `find_executable_on_path()` so a system with `simpleFoam` but not `interFoam` still registers the available subset. The three vtables dispatch through a shared `openfoam_solve_impl(solver_binary, ...)` driver.
- **Case-directory generator.** The adapter writes a complete OpenFOAM v12 case under `std::filesystem::temp_directory_path()` per solve: `constant/polyMesh/{points,faces,owner,neighbour,boundary}`, `system/{controlDict,fvSchemes,fvSolution}`, `0/{U,p}`, `constant/{transportProperties,turbulenceProperties}`. The `polyMesh` written by this push is a **placeholder single-cube** that proves the contract end-to-end (case-dir → subprocess → run-back); the real Tet4→polyMesh translation (face deduplication, owner/neighbour bookkeeping, boundary-patch extraction) is the mid-Sprint-8 follow-on alongside `examples/pipe-bend/` (push 6).
- **Mandatory timeout.** `timeout_seconds` defaults to **3600 s** (1 hour); the adapter refuses to call `run_subprocess` without one. A timed-out child surfaces as `SOUXMAR_E_TIMEOUT`; a fatal signal or non-zero exit surfaces as `SOUXMAR_E_PLUGIN_REJECTED` with the captured `stderr` tail in the error message. Stdout/stderr are stream-capped per push 1's defaults — large logs are truncated, not echoed into the host's heap.
- **ADR-0009 compliance, mechanical.** `find_program(SOUXMAR_SIMPLE_FOAM_BIN simpleFoam)` is the *only* build-time touch — no `find_package`, no `include_directories(${FOAM_DIR}/src/...)`, no GPL header anywhere in the include path. The plugin links against `souxmar::plugin` (host-side C++) for the subprocess harness; that's an internal Apache-2.0 library, not a GPL component.
- **Manifest.** `[plugin.openfoam_required]` carries `version_range = ">=11.0,<13.0"` so the discovery report can call out a mismatch before the first solve; license metadata records the *plugin's* license as Apache-2.0 (separate from the runtime binary's GPL terms — souxmar never links it).
- **Tests** (`tests/integration/test_cfd_stub.cpp`): grid-mesher → cfd-stub → assert every node carries `(2.5, 0, 0)`; hello-mesher → cfd-stub with `flow_direction = [0, 0, 1]` → assert node-0 vector is `(0, 0, 3.0)`. The OpenFOAM adapter itself is exercised on the nightly CFD-bearing matrix (Docker image with OpenFOAM v12 pre-staged), not in default CI.
- **Conformance gate.** `tests/integration/test_conformance.cpp` now also asserts `dev.souxmar.examples.cfd-stub` passes all 10 v1 checks (C001…C010). **10 in-tree plugins green.**

#### Sprint 8 push 1 — process-isolation harness

Foundational work for the Sprint 8 OpenFOAM adapter (push 2) and the Blender importer (push 3). The harness lets any plugin author drive an external binary without rolling their own fork/exec/waitpid loop. Host-side C++ utility; **NOT** part of the frozen C ABI — `souxmar-c/*` is unchanged.

- **`include/souxmar/plugin/subprocess.h`** — `SubprocessOptions` (argv / env / work_dir / timeout / stdin_bytes / max_capture_bytes); `SubprocessResult` (ok / exit_code / fatal_signal / timed_out / stdout_bytes / stderr_bytes / *_truncated / error_message / duration); `run_subprocess()` and `find_executable_on_path()`. The contract is "spawn a child, wait for it to terminate, return everything the host needs for an audit-log entry."
- **`src/plugin-host/subprocess.cpp`** — POSIX path via `posix_spawnp` + `poll()` + `waitpid` + `SIGKILL` on timeout. The env overlay merges with the parent's environment (empty string deletes a key). Stdout/stderr capture is non-blocking and stream-capped (default 64 KiB per stream — keeps audit-log entries tractable). Windows path via `CreateProcessW` + `WaitForSingleObject` + `TerminateProcess` + handle-inherit pipes; structured-exception codes (`STATUS_ACCESS_VIOLATION` / `STATUS_STACK_OVERFLOW` / `STATUS_ILLEGAL_INSTRUCTION` / divide-by-zero variants) map into the signal-like `fatal_signal` slot so cross-platform consumers can pattern-match crashes uniformly. `run_subprocess` is `noexcept` — every failure routes through `SubprocessResult`.
- **`find_executable_on_path()`** — small helper used by adapters that probe for their underlying binary at plugin-load time. The Sprint 8 push 2 OpenFOAM plugin uses it to refuse to register `solver.cfd.openfoam` if `simpleFoam` isn't on $PATH; absolute paths are checked directly.
- **Crash isolation by construction.** A SIGSEGV in the child is a populated `fatal_signal`, not a host crash. The reentrancy guard from Sprint 4 push 2 is the cross-stage synchroniser; this harness handles the spawn/wait alone.
- **Tests** (`tests/unit/test_subprocess.cpp`, POSIX leg): exit-code round-trip, stdout / stderr capture, timeout terminates the child within 2 s of the deadline, missing-binary surfaces a clean error, stdin pass-through (echo via `cat`), environment overlay round-trip, `max_capture_bytes` truncation, `fatal_signal` populated when the child dies on SIGSEGV, empty-argv rejected, `find_executable_on_path("sh")` resolves, missing program returns nullopt. Twelve tests total.
- **No frozen-header surface touched.** This is a host-side C++ utility under `include/souxmar/plugin/` (not `souxmar-c/`); third-party plugins are welcome to vendor the implementation rather than link against this header.

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## [0.9.0-beta1] - 2026-05-11

First public pre-release. Source + Linux CLI tarball + Python sdist are published as a GitHub release. **Tag:** `v0.9.0-beta1`. **ABI:** v1.1 frozen (ADR-0008). The desktop app, hosted services, and OpenFOAM adapter are explicitly not part of this release; the SPRINT_PLAN.md roadmap names their target sprints.

### Added

#### Sprint 1 (in progress) — data model + plugin host + C ABI

- `libsouxmar-core` data model in `include/souxmar/core/`:
  - `tag.h` — strong-typed `EntityTag`, `NodeIndex`, `CellIndex`, `VertexIndex`, `EdgeIndex`, `FaceIndex`, `SolidIndex` with std::hash specialisations.
  - `element_type.h` — `ElementType` enum (numerically stable across the on-disk format and C ABI) plus per-type `ElementInfo` (dimension, node count, order, canonical name).
  - `geometry.h` — `Geometry` (PIMPL'd; vertex coordinates, opaque bookkeeping for edges/faces/solids, per-entity tag / name, bounding box, adapter-data slot for native handles).
  - `topology.h` — `Topology` (kind-indexed entity graph for meshes without a CAD model).
  - `mesh.h` — `Mesh` (mixed-element, contiguous flat storage, tag inheritance, element histogram, bounding box).
  - `field.h` — `Field` (scalar/vector/tensor over nodes/cells/faces/Gauss points, optional time series, contiguous storage with VTK-compatible stride).
- **[ABI v1]** Public C ABI headers in `include/souxmar-c/`: `abi.h` (export macros, version constants), `status.h` (numerically-stable error codes), `plugin.h` (entry-point declaration + host-info struct), `registry.h` (capability registration entry), `mesher.h` (first capability vtable), `types.h` (opaque handle declarations). ABI v1 frozen-candidate begins; final freeze at Sprint 7 per `docs/SPRINT_PLAN.md`.
- `libsouxmar-plugin`:
  - `plugin/manifest.h` + `manifest.cpp` — `souxmar-plugin.toml` parser via tomlplusplus, with `ParseError` carrying line context. Validates required fields, capability list, threading model, ABI compatibility.
  - `plugin/discovery.h` + `discovery.cpp` — search-path computation (`SOUXMAR_PLUGIN_PATH`, install prefix, per-OS user prefix, optional CWD), directory walker, manifest validation, binary-existence check, structured `DiscoveryReport` of loaded + rejected.
- Unit tests (GoogleTest) covering the above:
  - `test_tag`, `test_element_type` — strong types + element metadata
  - `test_geometry`, `test_topology` — counts, tag/name roundtrips, bounding-box, move semantics, adapter-data deleter
  - `test_mesh` — node/cell add, mixed elements, histogram, validation errors
  - `test_field` — metadata, components by kind, time-step indexing, error paths
  - `test_manifest` — valid parse, multi-capability, missing-field, threading-model parsing, ABI mismatch, malformed-TOML line reporting
  - `test_discovery` — empty paths, missing path, valid plugin, missing binary, wrong extension, malformed manifest, multi-root aggregation

#### Sprint 2 (in progress) — capability registry, plugin loader, hello-mesher

- `libsouxmar-plugin` capability registry (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`):
  - `Registry` class: thread-safe (`std::shared_mutex`, read-mostly), `add_mesher` with structured `RegistryError`, `find` / `find_mesher` / `list_capabilities` / `list_capabilities_in_namespace` / `remove_plugin`.
  - C ABI bridge: `extern "C" souxmar_registry_add_mesher` casts the opaque `souxmar_registry_t*` back to `Registry*`, enforces ABI version + null-vtable + null-mesh_fn checks, translates failures to `souxmar_status_t` with thread-local error strings.
- Crash-isolation guard (`include/souxmar/plugin/guard.h`, `src/plugin-host/guard.cpp`):
  - `guard_call(fn) -> GuardResult`. Sprint 2 implementation catches C++ exceptions (`std::exception` and unknown-throw) as `SOUXMAR_E_PLUGIN_FAULT`-equivalent. Sprint 5 hardening adds POSIX `sigaction`/`sigsetjmp` and Windows SEH around the same surface; the public API stays stable.
- Plugin loader (`include/souxmar/plugin/loader.h`, `src/plugin-host/loader.cpp`):
  - `LoadedPlugin` (move-only RAII) owns the dlopen / `LoadLibrary` handle and removes its capabilities from the registry on destruction.
  - `PluginLoader::load(DiscoveredPlugin)` opens the binary, resolves `souxmar_plugin_register_v1`, calls it inside `guard_call`, and threads the plugin id through the registry so `add_mesher` attribution is automatic.
  - Cross-platform: POSIX `dlopen` + `dlsym` + `dlclose`; Windows `LoadLibraryExW` + `GetProcAddress` + `FreeLibrary`. Linked against `${CMAKE_DL_LIBS}` on POSIX.
- `souxmar_add_plugin` CMake macro (`cmake/SouxmarPlugin.cmake`):
  - Declares a `SHARED` library, hides default symbol visibility, defines `SOUXMAR_BUILD_PLUGIN`, copies the manifest beside the binary, and stashes the announced capabilities as a target property for tooling.
  - Same macro is intended for in-tree examples and out-of-tree third-party plugins.
- `examples/plugins/hello-mesher/`:
  - Reference plugin proving the full SDK contract — single exported symbol, vtable, registration call against the host registry. `mesh_fn` is a placeholder until host-side mesh accessors land in Sprint 3.
  - Built with `souxmar_add_plugin`. CMake auto-enables examples when `SOUXMAR_BUILD_TESTS=ON` so the integration suite always has the plugin to load.
- Tests:
  - `test_registry` — empty / add / duplicate / null-vtable / null-mesh_fn / ABI-mismatch / namespace-listing / find / `remove_plugin`.
  - `test_guard` — Ok / std::exception / non-std exception / nested guard.
  - `tests/integration/test_load_hello_mesher` — end-to-end discovery + load + register + capability-listing + RAII unload + registry empties on `LoadedPlugin` drop.

#### Sprint 3 (in progress) — pipeline orchestrator critical path

- New library `libsouxmar-pipeline` (`src/pipeline/`).
- **[pipeline-format v1]** Pipeline data model + YAML parser:
  - `include/souxmar/pipeline/value.h` — typed Value tree (Null / Bool / Number / String / StageRef / List / Map). Heavy types (Geometry/Mesh/Field) stay out of Value; they live in the runner's result store and are referenced by StageRef.
  - `include/souxmar/pipeline/pipeline.h` — `Stage` (id + capability + input map) and `Pipeline` (version + stages).
  - `include/souxmar/pipeline/parser.h` — strict YAML parser via yaml-cpp. Recognises the `{ from: stage_id }` StageRef shorthand. Validates required fields, version = 1, non-empty stages, unique ids. Errors carry source line/column when yaml-cpp surfaces them.
- DAG validation + topological sort (`include/souxmar/pipeline/dag.h`):
  - All errors collected per call (not just the first).
  - Self-reference, dangling-reference, cycle detection.
  - Stable topological order (declaration-order tie-break) so determinism gate has a fighting chance.
  - `collect_stage_refs(Value)` exposed for tests and tooling.
- Content-addressed cache (`include/souxmar/pipeline/cache.h`):
  - 64-bit FNV-1a `ContentHash` over (capability_id + plugin_version + recursive Value tree). Upstream stage hashes folded transitively so an upstream change invalidates downstream cache keys automatically. (Cryptographic hashing — BLAKE3 or SHA256 — lands with the on-disk cache in Sprint 3 push 2.)
  - `Cache` class: thread-safe (`std::shared_mutex`), in-memory put / get / contains / size / clear.
- Runner (`include/souxmar/pipeline/runner.h`):
  - `IDispatcher` interface — runner is plugin-agnostic. Sprint 3 push 1 ships with mock dispatcher in tests; Sprint 3 push 2 adds `RegistryDispatcher` that goes through the C ABI to loaded plugins.
  - Sequential execution in topological order. Per-stage `StageRunResult` records Cached / Executed / Failed / Skipped. Optional `RunOptions::stop_on_first_failure` (default ON) and `use_cache` (default ON).
  - Aggregate `RunResult` carries validation errors + per-stage results + outputs by stage id.
- New default dependency: `yaml-cpp >= 0.8.0` (MIT). Documented in `THIRD_PARTY_LICENSES.md`.
- Tests (all in `tests/unit/`, ~50 new test cases):
  - `test_pipeline_value` — kind builders, accessors, try-access, equality, kind names.
  - `test_pipeline_parser` — cantilever-beam YAML round-trip, StageRef shorthand, missing/duplicate/empty-stages rejection, unknown-version explicit rejection, line-reporting on malformed YAML.
  - `test_pipeline_dag` — straight-line, diamond, self-reference, dangling-reference, cycle detection, deeply-nested StageRef collection, declaration-order topological tie-break.
  - `test_pipeline_cache` — context distinguishes, upstream change propagates, map-key ordering doesn't matter, hex format, put/get/overwrite/clear.
  - `test_pipeline_runner` — validation skip, topological dispatch order, output threading, fail-fast skip-downstream, full cache hit, input-change invalidation, plugin-version invalidation, upstream-change invalidation, cache-disabled mode.

#### Sprint 3 (in progress, push 2) — C ABI handles + RegistryDispatcher + end-to-end

- **[ABI v1]** Public C ABI handle accessors:
  - `include/souxmar-c/mesh.h` + `src/core/c_abi_mesh.cpp` — `souxmar_mesh_new`/`free`/`add_node`/`add_cell`/`reserve_*`/`num_*`/`node`/`cell_type`/`cell_node_count`/`cell_nodes`/`cell_tag`/`nodes_flat` plus `SOUXMAR_ET_*` numerically-stable element-type constants.
  - `include/souxmar-c/geometry.h` + `src/core/c_abi_geometry.cpp` — `souxmar_geometry_new`/`free`/`add_vertex`/`add_edge`/`add_face`/`add_solid`/`set_tag`/`set_name`/read accessors/`bounding_box`. `SOUXMAR_GK_*` entity-kind constants.
  - `include/souxmar-c/field.h` + `src/core/c_abi_field.cpp` — `souxmar_field_new`/`free` + read accessors. `SOUXMAR_FL_*` and `SOUXMAR_FK_*` enums.
  - `include/souxmar-c/value.h` + `src/pipeline/c_abi_value.cpp` — `souxmar_value_kind` + per-kind accessors (`as_bool`/`as_number`/`as_string`/`as_stage`) + list/map navigation. `SOUXMAR_VK_*` enums. Value handles are read-only across the ABI.
- **[ABI v1]** New capability vtable headers:
  - `include/souxmar-c/solver.h` — `souxmar_solver_vtable_t` + options struct. Solver inputs reach the plugin as a `souxmar_value_t` bag; the host extracts the `mesh: {from: ...}` upstream by convention.
  - `include/souxmar-c/writer.h` — `souxmar_writer_vtable_t`. Writer takes mesh + optional field + value bag; output is side-effect (typically a file at `path`).
- Registry extensions (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`):
  - `CapabilityKind` extended to {Mesher, Solver, Writer}.
  - `add_solver` / `add_writer` (C++) and `add_solver_c` / `add_writer_c` (C ABI bridges) with the same validation pattern as `add_mesher`.
  - `extern "C" souxmar_registry_add_solver` / `add_writer` extern wrappers.
  - `find_solver` / `find_writer` typed lookup.
- `RegistryDispatcher : IDispatcher` (`include/souxmar/pipeline/registry_dispatcher.h`, `src/pipeline/registry_dispatcher.cpp`):
  - Routes by namespace prefix (`mesher.*` / `solver.*` / `writer.*`).
  - Convention-based handle extraction: mesher reads optional `geometry: {from: ...}`; solver requires `mesh: {from: ...}`; writer requires `mesh: {from: ...}` + optional `field: {from: ...}`.
  - All plugin calls go through `plugin::guard_call` so a plugin throw cannot unwind into the host.
  - `StageOutput` typed wrapper (Mesh / Geometry / Field / Path) is the universal payload threaded through the runner's `shared_ptr<void>`.
  - Custom `shared_ptr` deleters call `souxmar_mesh_free` / `souxmar_field_free` so plugin-allocated handles round-trip the C ABI ownership rules.
- Reference plugins:
  - `examples/plugins/hello-mesher/hello_mesher.cpp` upgraded — `mesh_fn` now produces a real 1-tet mesh through the C ABI (`souxmar_mesh_new` → `souxmar_mesh_add_node` × 4 → `souxmar_mesh_add_cell` SOUXMAR_ET_TET4).
  - `examples/plugins/hello-writer/` — minimal writer plugin reading the mesh through C ABI accessors and writing a 2-line summary to the path supplied via the `souxmar_value_t` input bag. Built with `souxmar_add_plugin`; manifest declares `writer.text-summary`.
- Tests:
  - `tests/unit/test_c_abi_handles` — full coverage of the C ABI accessors for mesh, geometry, field, and value handles. Round-trip, error paths, NULL-pointer safety, capacity checks.
  - `tests/integration/test_pipeline_end_to_end` — the **canary** for the whole stack: discover hello-mesher + hello-writer, load both into a Registry, parse a 2-stage YAML pipeline, run through `RegistryDispatcher` + `Cache`, verify the output file exists with the expected content. Plus: cache-hit-on-rerun verification, missing-mesh-reference rejection.

#### Sprint 3 push 3 — CLI, VTU writer, on-disk cache, runnable example

- `souxmar` CLI binary (`src/cli/main.cpp`, `src/cli/CMakeLists.txt`):
  - `souxmar run <pipeline.yaml>` — parses the pipeline, discovers + loads every available plugin, dispatches stages through `RegistryDispatcher`, prints per-stage status (`[OK]` / `[CACHED]` / `[FAILED]` / `[SKIPPED]`) with content-hash hex.
  - `souxmar plugin list` — pretty-prints discovered plugins, manifest paths, ABI versions, and announced capabilities.
  - `souxmar version` — version + ABI string.
  - Flags: `--plugin-path <dir>` (repeatable, prepends to discovery path), `--no-cache`, `--cache-dir <dir>`.
  - Exit codes follow sysexits.h (0 / 64 / 65 / 70).
  - Built only when `SOUXMAR_BUILD_CLI=ON` (default).
- **[pipeline-format v1]** `examples/plugins/vtu-writer/` — in-tree writer plugin emitting ParaView-readable VTK XML UnstructuredGrid (`.vtu`) without linking VTK. Hand-emits the ASCII format; covers all 17 `SOUXMAR_ET_*` element types via a stable souxmar→VTK cell-type map. Capability: `writer.vtu`. The full VTK-backed adapter (binary + appended data + parallel pieces) lands in Sprint 6.
- `examples/cantilever-beam/` — runnable end-to-end example with `pipeline.yaml` + README walking the user from a fresh `cmake --build` to a `cantilever.vtu` opened in ParaView. Today the mesh stage is the placeholder hello-mesher (single tet); Sprint 6 swaps in the OpenCASCADE-loaded geometry + native tetra mesher with the same YAML structure.
- **SHA-256 content hash** (`src/pipeline/cache.cpp`) — `ContentHash` is now backed by a 32-byte SHA-256 digest (in-tree FIPS 180-4 implementation, no external dep). Public surface unchanged; `hex()` returns 64 lowercase chars. Replaces the FNV-1a Sprint 3 push 1 hash, which was good enough for in-process buckets but not durable enough to be the key for the on-disk cache and (in Sprint 7+) the distributed artifact store.
- **On-disk cache** (`include/souxmar/pipeline/cache.h`, `src/pipeline/cache.cpp`):
  - New `DiskCache` class — directory-backed byte-blob KV (`<dir>/<hex>` per key), atomic per-key writes via temp+rename, no cross-process locking yet (Sprint 5 adds advisory locks alongside the parallel runner).
  - `DiskCache::default_dir()` — resolves `$SOUXMAR_CACHE_DIR` → `$XDG_CACHE_HOME/souxmar` (Linux) / `~/Library/Caches/souxmar` (macOS) / `%LOCALAPPDATA%\souxmar\cache` (Windows) → `<tmp>/souxmar-cache`.
  - `RunOptions::disk_backing` — opt-in `DiskBacking` struct carrying a `DiskCache` plus `serialize` / `deserialize` callbacks. The runner consults disk on in-memory miss and writes through after a successful dispatch. Both callbacks owned by the caller — keeps the runner unaware of `StageOutput`.
- StageOutput round-trip:
  - `serialize_stage_output` / `deserialize_stage_output` (`include/souxmar/pipeline/registry_dispatcher.h`) — wire format for `Path`-kind outputs (the v0.0.1 case writers cover). `Mesh`/`Geometry`/`Field` return `nullopt` on serialize until plugin-side serializers land in Sprint 5.
  - Deserialize verifies the referenced file still exists; a vanished artifact is treated as a cache miss so the writer re-runs.
- Tests:
  - `tests/unit/test_pipeline_cache` extended — SHA-256 stability check (digest fills all 32 bytes, identical inputs produce identical digests), `DiskCache` round-trip / missing-key / empty-blob / `default_dir` honors override.
  - `tests/integration/test_cli_smoke` — invokes the real CLI binary via `std::system` against the cantilever-beam example. Asserts: `plugin list` enumerates in-tree plugins, `run` produces a well-formed VTU on disk, second `run` with the same `--cache-dir` marks the writer stage `[CACHED]`.

#### Sprint 4 push 1 — pysouxmar Python bindings

- New top-level subdirectory `bindings/python/` shipping the `pysouxmar` Python package.
  - `bindings/python/src/pysouxmar.cpp` — pybind11 module wrapping the C++ surface (parser, registry, loader, runner, cache).
  - `bindings/python/pysouxmar/__init__.py` — Python facade re-exporting the extension's symbols and documenting the API.
  - `bindings/python/pyproject.toml` — scikit-build-core build backend; `pip install ./bindings/python` produces a self-contained wheel with the souxmar libraries linked statically into the extension.
  - `bindings/python/CMakeLists.txt` — single CMakeLists that detects in-tree vs standalone (`pip install`) builds. Standalone bootstraps by adding the souxmar repo root with examples/tests/CLI/benchmarks off.
- **[pipeline-format v1]** Pipeline `Value` tree ↔ Python conversion in both directions:
  - `Null/Bool/Number/String/StageRef/List/Map` map to `None/bool/float/str/StageRef/list/dict`.
  - The `{from: stage_id}` YAML shorthand round-trips as a `pysouxmar.StageRef`. A plain dict `{"from": "stage_id"}` is also accepted on the Python side.
- Lifetime safety:
  - `pybind11::keep_alive<1, 2>` ties `PluginLoader` and `RegistryDispatcher` to the `Registry` they wrap, so a Python-level garbage collection of the registry cannot dangle a loader.
  - `LoadedPlugin` exposes the C++ move-only RAII semantics: drop the Python reference and the registry forgets the plugin's capabilities + the OS module is closed.
- Ergonomic surface:
  - `RunOptions.disk_cache_dir = "/path"` — assigning a path constructs a `DiskCache` and wires the `serialize_stage_output` / `deserialize_stage_output` callbacks automatically (no need to import the dispatcher serializers).
  - `RunResult.outputs` returns a `dict[str, dict]` keyed by stage id, with each value carrying `{"kind": "mesh"|"path"|...}` + kind-specific fields. Path-kind outputs include `{"path": "..."}`.
  - `parse_pipeline` / `parse_pipeline_file` raise `ValueError` carrying the YAML line+column on parse failure (no variant unwrapping needed Python-side).
- Build-system additions:
  - New `dev-python` CMake preset (in `CMakePresets.json`) — `Debug` build with `SOUXMAR_BUILD_PYTHON=ON` and `VCPKG_MANIFEST_FEATURES=tests;python` so vcpkg fetches `pybind11` automatically.
  - `cmake/SouxmarOptions.cmake`'s pre-existing `SOUXMAR_BUILD_PYTHON` option is now wired to `add_subdirectory(bindings/python)` from the top-level `CMakeLists.txt`.
- Tests:
  - `bindings/python/tests/test_basics.py` — pure-Python unit tests covering version round-trip, ABI version, pipeline parsing (good + error paths), `Value` tree symmetry through `Stage.input`, `StageRef` shorthand, registry-empty contract, `DiskCache.default_dir` override.
  - `bindings/python/tests/test_end_to_end.py` — integration tests against the in-tree `hello-mesher` + `vtu-writer` plugins. Asserts: discovery enumerates plugins, load registers capabilities, full pipeline run produces a well-formed VTU, second run with `disk_cache_dir` set hits the disk cache for the writer stage. Skips cleanly if no built plugins are found.
- Examples + docs:
  - `bindings/python/examples/cantilever.py` — 20-line script demonstrating the full discover/load/parse/run flow. Mirrors the Sprint 3 cantilever-beam C example.
  - `bindings/python/README.md` — install, quick start, API surface table, lifetime rules, roadmap to Sprint 4 push 2/3 and Sprint 5.

#### Sprint 4 push 2 — parallel runner + manifest-driven reentrancy

- **Parallel runner** (`include/souxmar/pipeline/parallel_runner.h`, `src/pipeline/parallel_runner.cpp`):
  - New `run_pipeline_parallel(...)` schedules independent DAG branches across an inline thread pool of size `RunOptions::max_workers`. Worker pulls from a ready queue, runs the stage (cache → dispatch under reentrancy guard → cache put), decrements dependents' in-degree, re-enqueues anything that becomes ready.
  - `run_pipeline(...)` (the public entry) dispatches into the parallel impl whenever `max_workers > 1`; `max_workers <= 1` keeps the original sequential path.
  - Stage results are emitted in declaration order regardless of completion order — same shape downstream consumers see from the sequential runner.
  - Stop-on-failure semantics: in-flight stages complete naturally, no new stage starts; downstream stages of a failed stage are marked `Skipped`.
- **`ReentrancyGuard`** (same header) — per-plugin `std::mutex` map. `acquire(plugin_id, threading)` returns a `unique_lock` that owns the per-plugin mutex for `SingleThreaded` / `InternalParallel`, and is empty for `Reentrant`. Plugin granularity means two capabilities from the same plugin share a lock; capabilities from different plugins overlap freely even when both declare single-threaded.
- **Threading model in the registry**:
  - `CapabilityEntry` gains a `threading` field (`include/souxmar/plugin/registry.h`).
  - `Registry::add_mesher` / `add_solver` / `add_writer` accept an optional `ThreadingModel` argument (defaults to `SingleThreaded` — the safe choice).
  - C ABI bridges (`add_mesher_c` etc.) read the loader-stamped `current_plugin_threading_` slot, mirroring the existing `current_plugin_id_` pattern.
  - `PluginLoader::load` sets `current_plugin_threading_ = discovered.manifest.threading` before invoking `souxmar_plugin_register_v1` and clears it after — same protocol as the plugin id slot.
  - New accessor: `Registry::find_threading(capability_id) -> std::optional<ThreadingModel>`.
- **`IDispatcher` extensions** (`include/souxmar/pipeline/runner.h`):
  - New optional virtual `plugin_id(capability_id)` — defaults to the capability id (over-serialises rather than under-).
  - New optional virtual `plugin_threading(capability_id)` — defaults to `SingleThreaded` (safe assumption when a dispatcher does not know).
  - `RegistryDispatcher` overrides both to consult the underlying registry.
- **`RunOptions::max_workers`** — `0`/`1` = sequential; `>1` = parallel up to that many workers (clamped to `min(max_workers, num_stages)`).
- Tests:
  - `tests/unit/test_parallel_runner.cpp` — mock dispatcher with sleep + atomic concurrency counter proves: independent stages run in parallel, dependent stages serialise, two `SingleThreaded` stages of the *same* plugin serialise, two `SingleThreaded` stages of *different* plugins still overlap, `stop_on_first_failure` marks downstream `Skipped`, `max_workers=1` produces a valid result, validation errors short-circuit before any worker spawns. Direct unit tests of `ReentrancyGuard` for `Reentrant` (no-op) and `SingleThreaded` (blocks) cases.
- Python:
  - `RunOptions.max_workers` exposed via `pysouxmar`. `bindings/python/tests/test_end_to_end.py` adds a parallel run of the cantilever pipeline asserting the expected result shape.
  - `bindings/python/README.md` documents `max_workers` + the per-plugin reentrancy contract.
- Build:
  - `src/pipeline/CMakeLists.txt` adds `parallel_runner.cpp` and links `Threads::Threads` (PUBLIC, so consumers don't have to repeat the find).

#### Sprint 4 push 3 — agent tool surface v1 + 5 tools

- **`libsouxmar-ai`** new library (`src/ai/`, `include/souxmar/ai/tool.h`):
  - `Tool` declaration: name, description, category, `Confirmation` policy (Auto / ConfirmOnce / ConfirmAlways), input/output schema docs, handler `std::function`.
  - `ToolRegistry` — O(1) lookup by name, sorted `list()`, mutable so tests can override v1 defaults.
  - `ToolResult` — structured `{data: Value, summary: string, error: optional<ToolError>}`. `ToolError` is `{code, message, suggestion}` per docs/AI_INTEGRATION.md ("model recovers, not retries").
  - `ToolContext` — runtime services (`Registry*`, `IDispatcher*`, `Cache*`) + per-session metadata bag (`session_state: Value*` plus an opt-in owning slot via `take_session_state`) + focus handles (`mesh_handle`, `geometry_handle`, `field_handle`) that mesh/solve update as a side effect.
  - `ConfirmationPolicy` — per-tool overrides, `confirmed_once` set, prompter callback. Default behaviour: tools at Confirmation > Auto without a prompter return `NOT_CONFIRMED` (recoverable, not a crash).
  - `dispatch_tool(...)` — name lookup → confirmation gate → handler invocation with full exception isolation (every throw lands as a `ToolError{code="INTERNAL"}`).
- **[agent-tool v1]** Five v1 tools (the docs/AI_INTEGRATION.md v1 catalogue):
  - `read_geometry_summary` (Read, Auto) — reads inline geometry input or `session_state['geometry']`, returns counts + bbox + tag list.
  - `mesh` (Mesh, Auto) — dispatches a registered `mesher.*` capability via `ToolContext.dispatcher`; stashes the resulting Mesh on `ctx.mesh_handle` for downstream tools; returns `{capability_id, num_nodes, num_cells}`.
  - `set_bc` (BC, ConfirmOnce) — validates tag/type/value; appends a BC to `session_state['boundary_conditions']`; rejects unknown BC types with `INVALID_ARGUMENT`.
  - `solve` (Solve, ConfirmAlways — the runtime / cost call) — requires a prior `mesh` call; dispatches `solver.*`; stashes the Field on `ctx.field_handle`; returns `{capability_id, location, kind, num_components}`. Wraps the session mesh as a synthetic upstream so `RegistryDispatcher`'s solver path picks it up by the `mesh: {from: ...}` convention.
  - `screenshot_viewport` (Read, ConfirmOnce) — stub returning `NOT_AVAILABLE` in the headless library + CLI build, with a structured suggestion (the desktop app build supersedes it in a later push).
- **`Value ↔ YAML` helpers** (`include/souxmar/pipeline/value.h`): `parse_value_yaml(src)` and `emit_value_yaml(value)` (deterministic indented emitter, recognises the `{from: stage_id}` StageRef shorthand). Used by the CLI agent shim and Python tests; stable across yaml-cpp versions because the emitter is hand-rolled.
- **CLI** (`src/cli/main.cpp`):
  - `souxmar agent list` — pretty-prints every registered tool with category + confirmation default.
  - `souxmar agent invoke <tool> [--input <yaml>] [--input-file <path>] [--yes]` — parses inputs, discovers + loads plugins, constructs a ToolContext, invokes the tool, prints the summary + YAML-emitted result.
  - Arg parser refactor: replaced the single positional with a `std::vector<std::string> positionals` so `agent invoke <tool>` works alongside `plugin list` and `run <pipeline>`.
- **Python** (`bindings/python/src/pysouxmar.cpp`):
  - New `pysouxmar.ai` submodule exposing `Confirmation`, `ToolError`, `ToolResult`, `Tool`, `ToolRegistry`, `ToolContext`, `ConfirmationPolicy`, `default_v1_tools`, `dispatch_tool`.
  - `ToolContext.session_state` is a transparent property: assigning a Python dict takes ownership via the new `take_session_state` helper; reading returns a Python view of the current Value tree (so tools that mutate it during dispatch round-trip back).
  - `pysouxmar.parse_value_yaml` / `emit_value_yaml` exposed for symmetric debugging.
  - v1 limitations documented: `ConfirmationPolicy.prompter` not yet exposed (use `overrides` to whitelist); Mesh / Field handles stashed by mesh / solve are not yet inspectable from Python.
- **Tests**:
  - `tests/unit/test_ai_tools.cpp` — framework (Auto / ConfirmOnce / ConfirmAlways / DENIED / NOT_CONFIRMED / override / exception → INTERNAL), default v1 registry contents, every tool's success + error path (mesh against a fake mesher vtable, solve precondition, set_bc append semantics, screenshot stub), plus Value↔YAML round-trip.
  - `bindings/python/tests/test_agent_tools.py` — Python mirror of the same surface, including a real-plugin mesh test (skips cleanly without built plugins).

#### Sprint 5 push 1 — plugin conformance suite v1 + ABI freeze gate

- **[ABI v1]** New plugin conformance suite (`include/souxmar/plugin/conformance.h`, `src/plugin-host/conformance.cpp`) — 10 v1 checks (C001–C010) covering the manifest, the load chain, the manifest↔registry mapping, the threading-model contract, and the load/unload cycle. Stable check ids; ratchet-only growth per [ADR-0004](docs/adr/0004-plugin-conformance-suite.md).
  - C001  manifest ABI version matches host
  - C002  manifest binary file resolves to an existing path
  - C003  plugin binary loads (dlopen / LoadLibrary succeeds)
  - C004  `souxmar_plugin_register_v1` symbol is exported
  - C005  registration returns success
  - C006  every capability announced in the manifest is registered
  - C007  no unannounced capabilities are registered
  - C008  each registered capability's threading model matches the manifest declaration
  - C009  plugin unload removes every capability owned by this plugin
  - C010  three load/unload cycles leave the registry at the same baseline
- **`souxmar-conformance`** runnable binary (`tools/conformance/`): `souxmar-conformance <search-dir> [--plugin-id <id>] [--quiet] [--summary-only]`. Discovers every plugin under the search dir, runs all 10 checks against each, prints a results table, exits 0 iff every plugin passes every check. Sysexits-style codes (0 / 1 / 2 / 3).
- `tests/integration/test_conformance.cpp` — the **Sprint 5 ABI freeze gate**. Runs `run_conformance` against all three in-tree plugins (hello-mesher, hello-writer, vtu-writer); asserts every check Passes for each, plus a negative test verifying a deliberately-mismatched ABI trips C001 and Skips the downstream chain.
- Docs:
  - New `docs/adr/0004-plugin-conformance-suite.md` explaining the 10 checks, the freeze-candidate process, and the ratchet policy.
  - `docs/PLUGIN_SDK.md` § Conformance updated to match the actual v1 surface that landed (was placeholder copy).
- Build:
  - `src/plugin-host/CMakeLists.txt` builds `conformance.cpp` into `libsouxmar-plugin`.
  - New top-level `tools/` subdirectory; `tools/conformance/CMakeLists.txt` builds the binary when `SOUXMAR_BUILD_CLI=ON` (default).

#### Sprint 5 push 2 — agent catalogue to 8 tools + audit log + session budget

- **3 new agent tools** (`src/ai/tools/`):
  - `query_field` (Read, Auto) — min/max/mean over `ctx.field_handle->data()`, with NaN filtering and finite-vs-total counts. Reports the field's location / kind / num_components labels so the agent can reason about scalar magnitude vs. vector component interpretation.
  - `compute_field` (Postproc, ConfirmOnce) — ships as an honest stub returning NOT_AVAILABLE. The postproc C ABI required by this tool lands in Sprint 5 push 3 alongside the heat-conduction solver; extending the existing solver vtable to accept an input field would be an ABI break right before freeze candidacy, so we ratchet rather than rush.
  - `propose_pipeline` (Pipeline, Auto) — round-trips a structured spec through `emit_value_yaml` + `parse_pipeline`. The parser is the ground truth on what "valid" means, so a draft that passes here is guaranteed to load at `souxmar run` time. Read-only by design — `write_pipeline` (future) is the matching commit tool.
  - `default_v1_tools()` registry size goes from 5 → 8. The Sprint 5 plan's "tool count to 8" commitment is satisfied.
- **`souxmar::ai::AuditLog`** (`include/souxmar/ai/audit_log.h`, `src/ai/audit_log.cpp`):
  - Append-only YAML one-liner per dispatch: `{ts: <iso8601 utc>, tool: ..., outcome: ..., duration_ms: N, input_hash: <sha256>, summary: "...", budget: {in, out, total, max_total}}`.
  - Thread-safe (internal mutex around the ofstream). Cross-process safe at line granularity on POSIX (`O_APPEND` + PIPE_BUF guarantee); best-effort on Windows.
  - `default_path()` resolves `$SOUXMAR_AUDIT_LOG` → `<project_root>/.souxmar/chat/audit.log` → `cwd/.souxmar/chat/audit.log`.
  - Parent directories created lazily on construction; permission failures throw `filesystem_error` rather than silently swallowing.
- **`souxmar::ai::SessionBudget`** (same header):
  - Per-session `{max_input, max_output, max_total} × {consumed_input, consumed_output}` counters.
  - `record(in_delta, out_delta)` increments the counters and fires `on_threshold(pct, axis, current)` exactly once per crossed (axis, threshold) pair. Thresholds: 50% / 80% / 100% of each `max_*_tokens`. `max == 0` means unlimited on that axis (callback suppressed).
  - Used by tools that talk to AI providers — the v1 catalogue today doesn't, so audit lines carry `budget: {in: 0, out: 0, ...}` for now. The plumbing is here for the desktop / API client work in later pushes.
- **`ToolContext` extensions** — non-owning `audit_log`, `budget` pointer slots. `dispatch_tool` reads both: every call records one audit entry (with timing via `std::chrono::steady_clock`), and the budget snapshot rides along.
- **Stable audit outcome vocabulary**: `ok` / `fail` / `denied` / `not_confirmed` / `not_found`. The dispatcher's `outcome_token` mapping is the single source so external log parsers can group / count without dispatch-internals knowledge.
- **CLI**: new `--audit-log <path>` flag on `souxmar agent invoke`. Default behaviour writes to `default_path()`; the flag overrides. A permission failure surfaces a warning but does NOT block the tool from running.
- **Python**:
  - `pysouxmar.ai.AuditLog`, `pysouxmar.ai.SessionBudget` bound; `ToolContext.audit_log` and `.budget` are non-owning Python properties (pybind11 `keep_alive` ties their lifetimes).
  - `on_threshold` callback for `SessionBudget` is intentionally not bound in v1 (signature involves `std::string_view` + struct ref). Python users watch `consumed_total` after each `record()`. First-class callback lands in Sprint 6.
- **Tests**:
  - `tests/unit/test_ai_tools.cpp` extended — registry count assertion now 8; `query_field` precondition + aggregation paths; `compute_field` stub contract; `propose_pipeline` good + bad spec paths; `AuditLog` round-trip + env-override default path; `SessionBudget` crosses-once threshold semantics; full dispatch → audit-line wiring.
  - `bindings/python/tests/test_agent_tools.py` mirrors the same surface from Python, including a 3-call audit-log roundtrip that asserts on the YAML line content + count.
- **Build**: `src/ai/CMakeLists.txt` gains `audit_log.cpp`, `tools/query_field.cpp`, `tools/compute_field.cpp`, `tools/propose_pipeline.cpp`.

#### Sprint 5 push 3 — postproc C ABI surface + heat solver + scalar-magnitude

- **[ABI v1]** New capability namespace `postproc.*` with a dedicated C ABI surface:
  - `include/souxmar-c/postproc.h` — `souxmar_postproc_vtable_t` (`abi_version`, `compute_fn`, `destroy_fn`) + `souxmar_postproc_options_t`. `compute_fn` takes `(mesh, input_field, value_bag, options, &out_field, user_data)` — the field input parameter is the key difference from `solver.*`.
  - `souxmar_registry_add_postproc()` registration entry (see [ADR-0005](docs/adr/0005-postproc-c-abi.md) for why this is a new vtable instead of an extension to `solver.*`).
  - **The solver C ABI is unchanged** — existing solver plugins keep compiling. The ratchet rule from ADR-0004 (no breaking changes pre-freeze) is honored.
- **Registry + dispatcher extensions** (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`, `src/pipeline/registry_dispatcher.cpp`):
  - `CapabilityKind::Postproc` (value 3), `PostprocEntry`, `add_postproc` / `add_postproc_c` / `find_postproc`.
  - `RegistryDispatcher::dispatch_postproc` requires both `mesh: {from: ...}` and `field: {from: ...}` upstream; passes them through to the plugin's `compute_fn` under `plugin::guard_call`; wraps the returned field as a `StageOutput::Kind::Field` with the standard `souxmar_field_free` deleter.
  - Namespace routing table now: `mesher.*` / `solver.*` / `writer.*` / `postproc.*`.
- **`compute_field` agent tool — activated**: the Sprint 5 push 2 stub is replaced. The tool wraps `ctx.mesh_handle` + `ctx.field_handle` as synthetic upstream stages, dispatches the named `postproc.*` capability via `RegistryDispatcher`, and stashes the resulting Field on `ctx.field_handle` for downstream tools. Returns `{capability_id, location, kind, num_components, num_time_steps}`. Marked `ConfirmAlways` (runtime / cost surface).
- **`examples/plugins/heat-solver/`** — registers `solver.heat.linear`. Reads `num_time_steps` / `dt` / `tau` / `t_steady` from the value bag; produces a nodal scalar `Field` with multi-step temperature evolution: `T(node_i, step_j) = T_steady · (1 − exp(−t_j/τ)) · ½(1 + cos(π·x_norm))`. Demonstrates `Field` time-series across the C ABI — the Sprint 5 deliverable.
- **`examples/plugins/scalar-magnitude/`** — registers `postproc.scalar_magnitude`. Takes any-kind input field (scalar / vector / tensor), emits a scalar Field with same `count` × `num_time_steps`. Per-location output is `sqrt(sum_c v_c²)` (Frobenius norm). Declared `reentrant` — pure functional transform, no shared state — so the parallel runner can fan out concurrent calls.
- **Conformance gate**: `tests/integration/test_conformance.cpp` extended with the two new plugins. The freeze gate now covers five in-tree plugins (hello-mesher, hello-writer, vtu-writer, heat-solver, scalar-magnitude) — all 10 v1 checks pass on every one.
- **New integration test**: `tests/integration/test_postproc_end_to_end.cpp` runs `mesh → heat → scalar_magnitude` end-to-end against the in-tree plugins. Asserts every stage `Executed`, that the postproc output is a Field with `num_time_steps == 3` and `count == 4` (matches the upstream heat solver), and that a missing `field: {from: ...}` upstream is rejected by the dispatcher.
- **Unit test updates**: `tests/unit/test_ai_tools.cpp` — `compute_field` is no longer NOT_AVAILABLE. New assertions: `INVALID_ARGUMENT` for a missing `capability_id`, `PLUGIN_NOT_FOUND` / `PRECONDITION_FAILED` for an empty registry. Python pytest mirrors the same.
- **Docs**: ADR-0005 documents the decision, the three alternatives considered (extend solver vtable / smuggle through value bag / subprocess), and the consequences.

#### Sprint 5 push 4 — bulk-buffer ABI for large mesh transfer (ADR-0006)

- **[ABI v1]** New opaque handle `souxmar_buffer_t` (`include/souxmar-c/buffer.h`, `src/core/c_abi_buffer.cpp`):
  - `souxmar_buffer_new(bytes)` / `_free` / `_data` / `_data_const` / `_size` / `_alignment` (≥16-byte SIMD-friendly).
  - Heap-backed v1 implementation. Internal header carries a magic word + size + allocation pointer; double-free is a no-op rather than a corruption (poisons the magic on first free).
  - Forward-compatible with the v2 mmap-backed implementation per [ADR-0006](docs/adr/0006-memory-mapped-buffer-protocol.md) — no plugin-side change when v2 lands.
- **`souxmar_mesh_from_buffers()`** (`include/souxmar-c/mesh.h`, `src/core/c_abi_mesh.cpp`):
  - `souxmar_mesh_buffers_t` descriptor: `node_coords` (3·num_nodes doubles) + `cell_types` (uint16 per cell) + `cell_connectivity` (flat uint64 node ids) + `cell_offsets` (num_cells+1 uint64 prefix sum) + optional `cell_tags` (int32 per cell, NULL = untagged).
  - Single-call mesh ingest — amortizes the ~50 ns per-call ABI overhead the per-element `souxmar_mesh_add_node` / `add_cell` path pays.
  - Full validation: required-pointer null check, each buffer's size matches its declared count, offsets monotonic + zero-prefixed + terminator matches connectivity length, every cell type is a known `SOUXMAR_ET_*`, per-cell node count matches the element type's expected count, every node index is in range. `out_status` carries a structured rejection reason on any failure.
  - Pre-reserves the underlying `Mesh` vectors from the declared counts so the hot loop is amortised O(1) per element.
- **Latent bug fixed**: `souxmar_value_t` was referenced throughout the C ABI (`solver.h`, `value.h`) but never `typedef`'d. The in-tree plugins all compile as C++, where `struct X` aliases `X` automatically, masking the issue. Pure-C plugin authors would have hit an "unknown type" error. Typedef added to `souxmar-c/types.h` next to the new `souxmar_buffer_t`.
- **Benchmark**: `benchmarks/bench_mesh_construction.cpp` compares per-element vs bulk construction across N³ tetrahedral grids (N = 8 / 16 / 32 / 64). First in-tree benchmark — `benchmarks/CMakeLists.txt` is the seed for the nightly perf-regression CI work in the Sprint 5 plan.
- **Tests**: `tests/unit/test_c_abi_buffer.cpp` covers the buffer round-trip, alignment guarantee, null-safety, every documented bulk-mesh validation failure path (null inputs, wrong sizes, non-monotonic offsets, unknown element type, mismatched node count, out-of-range node index), and a bulk-vs-incremental equivalence test against a 5-node 2-tet mesh.
- **ADR-0006**: documents the design, the v1-heap / v2-mmap rollout plan, the alternatives considered (raw pointers, shared-memory from day 1, variable-batch per-element setters), and the freeze-ratchet implications.
- **Build**: `src/core/CMakeLists.txt` picks up `c_abi_buffer.cpp`; new `benchmarks/` subdirectory wired to the existing top-level `SOUXMAR_BUILD_BENCHMARKS` gate.

#### Sprint 5 push 5 — DX + Platform: plugin tutorial, thermal-fin example, perf-nightly CI

- **`docs/tutorials/plugin-authoring.md`** — end-to-end walkthrough from `cmake --build` to a `souxmar-conformance`-passing plugin. References the in-tree hello-mesher (mesher), heat-solver (solver with time-series Field), hello-writer / vtu-writer (writer), scalar-magnitude (postproc) as canonical examples. Sections: project layout, manifest, single export, per-namespace vtable patterns, CMake with `souxmar_add_plugin`, conformance verification, distribution (per-platform plugin prefixes), troubleshooting the common first-attempt failures (C001 / C002 / C006 / C007).
- **`examples/thermal-fin/`** — second runnable example. 4-stage pipeline (mesh → heat → scalar_magnitude → write) exercising every Sprint 5 capability namespace end-to-end:
  - The hello-mesher placeholder produces a unit tet (Sprint 6's Gmsh adapter swaps this for real CAD-loaded geometry with the same YAML shape).
  - The heat solver writes a 5-step nodal scalar `Field` per the Sprint 5 time-series demo.
  - The scalar-magnitude postproc round-trips the Field through the new `postproc.*` C ABI (ADR-0005).
  - The VTU writer dumps the mesh for ParaView inspection.
  - README walks the runtime steps, the knobs the user can vary, and the diff to `cantilever-beam`.
- **`.github/workflows/perf-nightly.yml`** — nightly + on-demand + PR-gated (on relevant paths) benchmark CI. Builds Release + `SOUXMAR_BUILD_BENCHMARKS=ON`, runs the mesh-construction bench in JSON format with 3 repetitions × 0.2 s min time, compares to `benchmarks/baselines/main.json` (skip + warn when absent), uploads the report as an artifact, fails on >10% regression.
- **`tools/perf-compare/compare.py`** — Google-Benchmark-JSON diff utility. Prefers `_mean` aggregates when present, falls back to raw run times. Per-benchmark table with delta% + visual markers (`⚠` regression, `↓` improvement). Single-source threshold for tuning when shared-runner noise floors shift.
- **`benchmarks/baselines/README.md`** — documents the baseline-update workflow, the deliberate "commit, don't auto-rotate" policy, the regression threshold rationale, what belongs in `baselines/` vs. workflow artifacts.

This closes the Sprint 5 DX + Platform items called out in `docs/SPRINT_PLAN.md`. The baseline file itself is intentionally NOT committed in this push — the "baseline established" exit criterion is the first follow-on PR that lands a `benchmarks/baselines/main.json` generated on the CI hardware tier.

#### Sprint 5 push 6 — ABI v1 frozen-candidate declared

- **[ABI v1]** **`SOUXMAR_ABI_FREEZE_CANDIDATE` macro** set in `include/souxmar-c/abi.h`, declaring the start of the two-sprint soak. Formal-freeze target: **2026-06-08**. Status block in the header names the soak rules inline so plugin authors hitting the header see the contract immediately.
- **[ABI v1]** [ADR-0007 — ABI v1 freeze-candidate](docs/adr/0007-abi-v1-freeze-candidate.md) lands the full mechanics: the 14 frozen headers (abi / status / types / plugin / registry / mesher / solver / writer / postproc / mesh / geometry / field / value / buffer), the ratchet rules during soak (additive minor surfaces only — zero-init forward-compat by construction), the cancellation triggers (any breaking change, two consecutive conformance failures, confirmed perf regression), and the exit criteria for State 2 → State 3 (clean conformance + ASAN/TSAN nightly + perf-nightly within threshold).
- **`docs/PLUGIN_SDK.md` § Versioning** gains a "Current freeze status: **frozen-candidate v1** (since 2026-05-11)" callout pointing at the ADR and the governance mechanics.
- **`docs/GOVERNANCE.md` § ABI freeze process** documents the three-state model (pre-freeze → frozen-candidate → formally frozen) the project commits to use for every future ABI version. The State 2 → State 3 merge carries the same two-maintainer-approval bar as any Tier 3 change.
- **`README.md` § Status** refreshed: replaces the stale "Sprint 0 scaffolding" snapshot with the current runnable surface (CLI / Python / plugin SDK / 5 in-tree plugins / 2 runnable examples / parallel runner / agent tool v1 / perf-nightly CI) and an honest list of what's still scoped out of Phase 0 (no Tauri yet, no OCCT / Gmsh / FEniCSx adapters yet, agent tool surface still offline).

This push lands no code under `src/` — it is the contractual moment Sprint 5 has been building toward across pushes 1–5. The first follow-on PR opens the soak tracking issue; PRs landing during soak that touch any frozen header must inspect the ratchet rules in ADR-0007.

#### Sprint 6 push 1 — mesh-quality postproc + `query_mesh_quality` agent tool

- **`souxmar::core::quality`** (`include/souxmar/core/mesh_quality.h`, `src/core/mesh_quality.cpp`): pure-math metric functions on per-element coordinate arrays. v1 catalogue: `SignedVolume`, `EdgeRatio`, `MinDihedralDeg`. Tet4 and Tri3 supported; other element types return NaN (Hex8 / Quad4 / higher-order land when an in-tree mesher emits them). `evaluate` / `evaluate_all` are stateless; `summarise` aggregates a whole-mesh quality field with NaN-skip semantics and exposes per-metric stats plus advisory threshold counters (inverted / sliver-dihedral / extreme-aspect / unsupported). Metric numeric ids are STABLE — they pin the component layout of the field the postproc plugin emits.
- **`examples/plugins/mesh-quality/`** — sixth in-tree reference plugin. Registers `postproc.mesh_quality`; reads the mesh through `souxmar-c/` accessors, calls `quality::evaluate_all` per cell, emits a per-cell `FieldKind::Vector` (3-component) Field. Declared `reentrant`. The plugin is self-contained on the C ABI: it pulls `src/core/mesh_quality.cpp` directly into its compile list rather than linking `libsouxmar-core`, preserving the docs/PLUGIN_SDK.md contract while keeping the math DRY across the in-tree consumers.
- **`query_mesh_quality` agent tool** (`src/ai/tools/query_mesh_quality.cpp`) — `default_v1_tools()` registry size 8 → 9. Confirmation::Auto (read-only inspection). Reuses an existing 3-component cell-located Field from `ctx.field_handle` when present; otherwise dispatches `postproc.mesh_quality` against `ctx.mesh_handle` through `ctx.dispatcher` (same synthetic-upstream pattern `compute_field` uses) and stashes the result for follow-up tools. Returns `{metrics: {<name>: {min, max, mean, finite, total}}, flags: {cells_inverted, cells_sliver_dihedral, cells_extreme_aspect, cells_unsupported}, num_cells, source}`.
- **Conformance gate**: `tests/integration/test_conformance.cpp` extended — the freeze gate now covers six in-tree plugins (hello-mesher, hello-writer, vtu-writer, heat-solver, scalar-magnitude, mesh-quality), all 10 v1 checks green on every one. **No frozen-header surface was touched; the ABI v1 freeze-candidate soak rolls forward unchanged.**
- **Tests**:
  - `tests/unit/test_mesh_quality.cpp` pins the math: regular-tet exact values (arccos(1/3) ≈ 70.529° dihedral, edge_ratio == 1, volume > 0), orientation flip → negative volume, sliver tet → sub-1° dihedral, stretched tet → edge_ratio > 50, Tri3 right-angle case, unsupported element type → NaN, degenerate (coincident-vertex) tet → volume 0 + NaN for ratio / dihedral, summariser threshold counters round-trip.
  - `tests/integration/test_mesh_quality_plugin.cpp` runs `mesh → postproc.mesh_quality` end-to-end against the in-tree hello-mesher, inspects the resulting `FieldKind::Vector` payload, and asserts the summariser flags zero pathologies on the (well-formed) tet.
  - `tests/unit/test_ai_tools.cpp` registry assertion bumped 8 → 9; new `RequiresMeshHandle` precondition test for `query_mesh_quality`.
  - `bindings/python/tests/test_agent_tools.py` mirror catalogue assertion bumped 8 → 9.
- **Build**: `src/core/CMakeLists.txt` adds `mesh_quality.cpp`; `src/ai/CMakeLists.txt` adds `tools/query_mesh_quality.cpp`; `examples/CMakeLists.txt` adds the new plugin subdirectory; `tests/integration/CMakeLists.txt` depends on `mesh_quality` so the integration suite has the plugin to load.

#### Sprint 6 push 2 — manifest schema validation hardening

- **Stable rejection codes** for every manifest failure mode (`include/souxmar/plugin/manifest.h`):
  - New `enum class ManifestRejection` with stable string tokens: `ok`, `toml_syntax`, `missing_field`, `wrong_type`, `abi_unsupported`, `empty_capabilities`, `unknown_threading`, `invalid_capability_namespace`, `invalid_plugin_id`, `invalid_version`, `file_io`. Append-only; numeric values are stable so on-disk audit log records keep parsing.
  - `ParseError` extended with `code`, `column`, `field` (dotted path, e.g. `"plugin.abi"`). Existing brace-init `{message, line}` still compiles — the new fields default — so every call site upstream of the parser keeps working unchanged.
- **Tighter validation**:
  - Capability strings must use one of the host-allow-list namespaces (`reader`, `writer`, `mesher`, `element`, `solver`, `postproc`). `garbage.foo` is rejected at parse time rather than silently registered. New `is_allowed_capability(id)` + `allowed_capability_namespaces()` are exposed for tooling.
  - Plugin id must look like reverse-DNS (at least one `.`, alphanumerics / `.` / `-` / `_`, no whitespace, no path separators). The marketplace publish step tightens further at upload time; this layer catches the obvious classes today.
  - Version must look like SemVer (`major.minor.patch[-pre][+build]`). "abc" / "1" / "1.2" are rejected.
  - Malformed-TOML errors now surface both `line` and `column` from toml++.
- **Additive optional manifest fields** under the ABI v1 soak ratchet (forward-compatible by construction — missing → default):
  - `plugin.description` (one-line summary) · `plugin.documentation` (URL) · `plugin.tags` (string array for the plugin index) · `plugin.min_souxmar_abi_minor` (plugin declares the minimum minor ABI it needs; the host today recognises it as advisory metadata, with the loader gate landing alongside the first minor bump).
  - `examples/plugins/mesh-quality/souxmar-plugin.toml` is updated as the canonical example with the new fields populated.
- **Structured discovery rejections** (`include/souxmar/plugin/discovery.h`): `DiscoveryRejection` gains a `code` enum (`cannot_iterate_search_path` / `manifest_parse_failed` / `binary_not_found` / `binary_unrecognised_extension`) plus an `optional<ManifestRejection> manifest_code` populated when the rejection came from the parser. `{candidate_path, reason}` brace-init still compiles.
- **`souxmar plugin list` upgrade** (`src/cli/main.cpp`): rejection lines now print as `- <path>: [<discovery_code>/<manifest_code>] <reason>` so log readers can group failures by class without regex-on-message. The loaded-plugin listing also surfaces the new `description` and `tags` fields when present.
- **Python bindings** (`bindings/python/src/pysouxmar.cpp`): `pysouxmar.ManifestRejection` and `pysouxmar.DiscoveryRejectionCode` enums; the new `Manifest` fields (`description`, `documentation`, `tags`, `min_souxmar_abi_minor`); `DiscoveryRejection.code` + `.manifest_code` properties.
- **Docs** (`docs/PLUGIN_SDK.md`): manifest example shows the new fields with comments; a short snippet illustrates the `[<code>] <reason>` format `souxmar plugin list` emits.
- **Tests**:
  - `tests/unit/test_manifest.cpp` — one assertion per rejection code (missing-field / wrong-type / abi-unsupported / empty-capabilities / unknown-threading / invalid-capability-namespace / invalid-plugin-id / invalid-version / toml-syntax with line+column). Round-trip for the additive fields (parse when present + default when absent). Token-stability assertion for all 11 `to_string(ManifestRejection)` values. New `CapabilityNamespace.AllowList` direct test.
  - `tests/unit/test_discovery.cpp` — every existing rejection test now asserts on the new `code`; new `BadCapabilityNamespaceManifestRejectedWithStructuredCode` test that walks the full plugin-host stack: bad manifest → discovery → structured rejection.
- **No frozen-header surface was touched** — `souxmar-c/*` is unchanged. The new structured-rejection surface is all C++; the ABI v1 freeze-candidate soak rolls forward unchanged.

#### Sprint 6 push 3 — agent tools 9 → 12

- **Four new agent tools** complete the docs/AI_INTEGRATION.md v1 catalogue:
  - **`set_material`** (`src/ai/tools/set_material.cpp`, BC / ConfirmOnce) — stages a material spec on `session_state['materials']`. Mirrors `set_bc`'s shape: `{tag, model, properties: {<key>: number|string, ...}, name?}`. Validates required fields; passes unknown keys through so future solver plugins can introduce material parameters without a tool upgrade.
  - **`list_plugins`** (`src/ai/tools/list_plugins.cpp`, Read / Auto) — walks `ctx.registry`, returns `{capabilities: [{id, kind, plugin_id, abi_version, threading}, ...], count_total, count_by_kind}`. Optional `{namespace: string}` filter routes through `Registry::list_capabilities_in_namespace`. This is the inventory call the agent makes before `mesh` / `solve` / `compute_field` / `export_results` so it picks a capability the host has actually loaded.
  - **`apply_pipeline_diff`** (`src/ai/tools/apply_pipeline_diff.cpp`, Pipeline / ConfirmOnce) — applies `{base, ops}` where `ops` are `{op: 'add'|'remove'|'set_input'|'replace', ...}`. The result is re-emitted via `emit_value_yaml` and re-parsed via `parse_pipeline`, so a returned draft is guaranteed to load at `souxmar run` time. A `remove` op that leaves a `{from: <id>}` dangling trips the parser; the tool surfaces `INVALID_ARGUMENT` with the parser's line/column rather than producing a broken draft. The matching `write_pipeline` (commit-to-disk) lands in Sprint 7.
  - **`export_results`** (`src/ai/tools/export_results.cpp`, Export / ConfirmAlways) — dispatches a registered `writer.*` capability against the session mesh + (optional) field via the synthetic-upstream pattern (`__session_mesh__` / `__session_field__`) `compute_field` / `query_mesh_quality` already use. The `path` is passed through in the stage input bag. ConfirmAlways because writers have observable side-effects (files appearing on disk).
- **`default_v1_tools()` catalogue size: 9 → 12.** `tests/unit/test_ai_tools.cpp` and `bindings/python/tests/test_agent_tools.py` registry assertions bumped to 12 and the full expected name set updated.
- **Tests**:
  - `set_material`: success path appends to `session_state['materials']`; missing `properties` is rejected with `INVALID_ARGUMENT`.
  - `list_plugins`: missing-registry → `INTERNAL`; empty registry → zero-result success path.
  - `apply_pipeline_diff`: add-stage round-trips through the parser; remove-with-dangling-reference is rejected with `INVALID_ARGUMENT` (the parser is the ground truth on what a valid pipeline is).
  - `export_results`: precondition failures (no mesh / no registry / unknown writer) all surface structured `ToolError` codes; no writer is invoked.
  - Python mirror covers the same four surfaces via the existing `sx.Registry()` / `sx.ai.dispatch_tool` bindings.
- **No frozen-header surface touched.** Four pure additions to `libsouxmar-ai`; the new tools talk to plugins exclusively through `RegistryDispatcher`. ABI v1 freeze-candidate soak rolls forward unchanged.

#### Sprint 6 push 4 — `reader.*` C ABI surface (ABI v1.1 ratchet)

- **[ABI v1.1]** `SOUXMAR_ABI_VERSION_MINOR` bumped **0 → 1**. **First additive minor ratchet event during the v1 freeze-candidate soak.** Per ADR-0007, additive minor surfaces are forward-compatible by construction: a v1.0 plugin keeps loading on a v1.1 host because every new symbol is opt-in, and a v1.1 plugin attempting to register a reader against a v1.0 host fails at symbol resolution time (conformance check C004 catches it). Soak rolls forward; this bump does NOT reset the soak window — only breaking changes do.
- **[ABI v1.1]** New header `include/souxmar-c/reader.h` and registration entry `souxmar_registry_add_reader`:
  - `souxmar_reader_vtable_t` with `read_fn(path, inputs, options, &out_mesh, &out_geometry, user_data)`. The plugin fills exactly one of `out_mesh` / `out_geometry` per the file format it consumes; the dispatcher's `dispatch_reader` routes the produced handle to the matching `StageOutput::Kind::Mesh` or `Kind::Geometry`.
  - `souxmar_reader_options_t`: `merge_coincident_nodes`, `coincidence_tolerance`, `preserve_tags`, `random_seed`.
  - Registry extensions: `CapabilityKind::Reader`, `ReaderEntry`, `add_reader` / `add_reader_c` / `find_reader`. Capability variant payload extended with `ReaderEntry`.
  - Dispatcher routing table now: `mesher.*` / `solver.*` / `writer.*` / `postproc.*` / **`reader.*`**.
- **`examples/plugins/stl-reader/`** — seventh in-tree reference plugin, **always-on** (no external dependencies). Registers `reader.stl`. Parses ASCII STL into a Tri3 mesh through the C ABI, deduplicating coincident vertices (quantised to 1e-7 in world coords) so adjacent facets share nodes — without dedup every cell carries 3 fresh nodes and topological adjacency is lost. Declared `reentrant`. Manifest declares `min_souxmar_abi_minor = 1` (the floor for the reader surface).
- **`examples/plugins/occt-reader/`** — opt-in OCCT-backed STEP / IGES reader, gated behind `-DSOUXMAR_WITH_OPENCASCADE=ON` + `find_package(OpenCASCADE)`. Registers `reader.step` AND `reader.iges` (shared vtable, switches on file extension). Walks `TopExp_Explorer` over the loaded `TopoDS_Shape` and emits vertices / edges / faces / solids into a souxmar Geometry with stable tag preservation. Declared `single-threaded` — OpenCASCADE's translator state isn't thread-safe. Not built in default CI; nightly builds with OCCT-bearing runners exercise it.
- **`examples/stl-cube/`** — first souxmar pipeline driven by a real input file. `reader.stl → postproc.mesh_quality → writer.vtu` against a 12-facet ASCII STL cube. After `souxmar run`, the user has a `cube.vtu` ParaView can open and a deduplicated 8-node / 12-cell Tri3 mesh inside it. README documents the cantilever-beam upgrade path: swap `reader.stl` for `reader.step` and the rest of the pipeline keeps working with no other changes — that's the namespace contract.
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `stl-reader` passes all 10 v1 checks (7 in-tree plugins green); the suite itself didn't change. C001 / C004 / C006 / C008 directly cover the new reader namespace's manifest + registration + threading.
- **Tests**:
  - `tests/integration/test_reader_end_to_end.cpp` — full `reader.stl → writer.vtu` flow against a generated STL fixture: discovery, load, parse, dispatch, file appears on disk. Asserts the deduplicated 8-node / 12-cell shape — pins the dedup logic against regression. Negative test for missing-`path` input asserts `dispatch_reader` surfaces the structured rejection.
- **Build**: `cmake/SouxmarOptions.cmake`'s pre-existing `SOUXMAR_WITH_OPENCASCADE` option now gates `examples/plugins/occt-reader/`. The plugin's CMakeLists calls `find_package(OpenCASCADE QUIET)` and `return()`s with a `STATUS` message if OCCT isn't installed — clean skip, no noisy failure.
- **Docs** (`docs/PLUGIN_SDK.md`): new Reader subsection documents the dual-output vtable contract and explicitly names this push as the first soak ratchet event.

#### Sprint 6 push 5 — second tetrahedral mesher (`mesher.tetra.grid` + opt-in Gmsh)

Closes the Sprint 6 plan exit criterion: **"A user can swap `mesher.tetra.native` for `mesher.tetra.gmsh` in pipeline YAML with no other changes; same result format."**

- **`examples/plugins/grid-mesher/`** — eighth in-tree reference plugin, **always-on**. Registers `mesher.tetra.grid`; reads the input Geometry's bounding box, builds an N×N×N tetrahedral grid via the 5-tet hex decomposition (same one `benchmarks/bench_mesh_construction.cpp` uses). `options.target_size` derives N from the largest bbox axis; default N=4. Declared `reentrant` — pure functional over its inputs. Tag inheritance is left as `-1` (untagged); a real CAD-aware mesher (gmsh-mesher, occt+netgen, ...) propagates face tags from the source geometry per the PLUGIN_SDK contract.
- **`examples/plugins/gmsh-mesher/`** — opt-in via `SOUXMAR_WITH_GMSH` + `find_package(Gmsh)`. Drives Gmsh's C++ API: `gmsh::model::occ::addBox` over the input bbox, `gmsh::model::mesh::generate(3)`, `getNodes` / `getElementsByType(4)` → souxmar mesh through the C ABI. Gmsh node tags are 1-based with gaps; the adapter remaps onto contiguous souxmar indices. Declared `single-threaded` because Gmsh holds process-global state (`gmsh::initialize()` is process-wide); the reentrancy guard serialises concurrent stages. `destroy_fn` calls `gmsh::finalize()` so the plugin doesn't leak past host exit. **Not built in default CI**; nightly Gmsh-bearing runners exercise it. The plugin's CMakeLists `find_package(Gmsh QUIET)` and `return()`s with a clean STATUS message when Gmsh isn't installed — `SOUXMAR_WITH_GMSH=ON` on a Gmsh-less machine produces a clear skip, not a noisy failure.
- **`examples/swap-mesher/`** — `grid.yaml` and `gmsh.yaml` differ by one line:
  ```diff
  -    plugin: mesher.tetra.grid
  +    plugin: mesher.tetra.gmsh
  ```
  README documents the contract: the upstream geometry stage, downstream postproc + write stages, input keys, and result schema are identical regardless of which mesher implements the namespace.
- **`tests/integration/test_swap_mesher.cpp`** — the always-on gate. Builds a unit-cube Geometry programmatically (8 corner vertices via `souxmar_geometry_add_vertex`), dispatches `mesher.tetra.grid` with `target_size=0.5`, asserts the produced Mesh has the pinned shape (27 nodes / 40 tets — N=3 nodes per axis × 5 tets per cube). Negative test: missing-geometry input is rejected with a structured `DispatchError` from `dispatch_mesher`. The Gmsh variant runs nightly with the opt-in flag; default CI exercises the contract via the always-on side.
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `grid-mesher` passes all 10 v1 checks (**8 in-tree plugins green**). The suite itself didn't change — the second mesher fits the same shape as the first.
- **Build**: `examples/CMakeLists.txt` adds `plugins/grid-mesher` unconditionally; `plugins/gmsh-mesher` only when `SOUXMAR_WITH_GMSH=ON`. `tests/integration/CMakeLists.txt` depends on `grid_mesher`.
- **No frozen-header surface was touched.** The mesher.* C ABI is unchanged; both new plugins build against the existing `souxmar-c/mesher.h`. ABI v1 freeze-candidate soak rolls forward.

#### Sprint 6 push 6 — Sprint 6 closeout: cost meter + budget config

- **First-class `SessionBudget.on_threshold` Python binding** (`bindings/python/src/pysouxmar.cpp`): pulls in `<pybind11/functional.h>`, wraps the C++ `std::function<void(int, std::string_view, const SessionBudget&)>` so Python callers write `b.on_threshold = lambda pct, axis, cur: ...`. Sprint 5 push 2 left this unbound; the desktop-app cost-meter work blocked on it.
  - The wrapper acquires the GIL on each callback and routes Python exceptions through `PyErr_WriteUnraisable` so a misbehaving callable can't unwind into the dispatcher's audit-write path. Per the SessionBudget contract, `on_threshold` callbacks must not throw.
  - Setting `b.on_threshold = None` clears the callback.
- **`.souxmar/budget.toml` per-project config** (`include/souxmar/ai/budget_config.h`, `src/ai/budget_config.cpp`): tomlplusplus-backed parser for a small `[budget]` block — `max_input_tokens` / `max_output_tokens` / `max_total_tokens`, all optional, default `0` (the SessionBudget "unlimited" sentinel). `parse_budget_config(toml)` / `parse_budget_config_file(path)` return a `std::variant<BudgetConfig, BudgetConfigError>`; the error carries the offending dotted field path (e.g. `budget.max_input_tokens`) so tooling can group failures by class. `BudgetConfig::apply_to(SessionBudget&)` sets the caps and explicitly **leaves the running counters and the threshold callback untouched** — so a project can hot-reload its budget without losing in-flight session state.
- **`souxmar agent invoke --budget-config <path>`** (`src/cli/main.cpp`): explicit override path; when omitted, the CLI auto-loads `.souxmar/budget.toml` from CWD if present. A parse error logs one warning line and continues unbudgeted (best-effort, never blocks the agent from running). When a budget is in effect, the CLI prints a one-line `budget: max_input=... max_output=... max_total=... (<path>)` summary so the user sees what's being enforced.
- **Python**: `pysouxmar.ai.BudgetConfig` class with the same `apply_to` method; `pysouxmar.ai.parse_budget_config(toml)` / `parse_budget_config_file(path)` raise `ValueError` on parse failures; `default_budget_config_path(project_root)` exposes the path resolver.
- **Tests**:
  - `tests/unit/test_budget_config.cpp` — valid parse, missing-fields-default-to-unlimited, negative rejection (with dotted field path), wrong-type rejection, malformed-TOML line reporting, `apply_to` semantics (caps set / counters preserved), `default_path` respects project_root, file round-trip via TempDir.
  - `bindings/python/tests/test_agent_tools.py` — `on_threshold` fires exactly once per crossed (axis, threshold) pair (50 / 80 / 100); clearing to `None` is idempotent; `BudgetConfig` round-trips through a real `.toml` file; `apply_to` leaves `consumed_*` alone.
- **Build**: `src/ai/CMakeLists.txt` adds `budget_config.cpp` and links `tomlplusplus::tomlplusplus`. The Python module pulls in `<pybind11/functional.h>` to support the `on_threshold` callback signature.
- **README Status section refreshed** with the Sprint 6 closing summary: ABI v1.1 frozen-candidate, **8 in-tree reference plugins** + 2 opt-in external adapters, 12 agent tools, three runnable examples, structured manifest rejection codes, per-project budget config. Honest about what's still scoped out of Phase 0.
- **No frozen-header surface touched.** Sprint 6 close-out is host-side and Python-side ergonomics — the `souxmar-c/*` surface remains at the ABI v1.1 shape declared in push 4. **Soak rolls forward unchanged; formal-freeze target 2026-06-08 stays on track.**

This closes Sprint 6 cleanly. Six pushes landed:
1. Mesh-quality metrics + 9th agent tool (`postproc.mesh_quality`, `query_mesh_quality`).
2. Manifest schema validation hardening (11 stable rejection codes, additive fields).
3. Agent tools 9 → 12 (`set_material`, `list_plugins`, `apply_pipeline_diff`, `export_results`).
4. `reader.*` C ABI surface — **the first soak ratchet event** — STL reader (always-on) + OCCT reader (opt-in).
5. Second tetrahedral mesher: grid-mesher (always-on) + Gmsh adapter (opt-in). Swap-test exit criterion met.
6. Cost-meter close-out: first-class `SessionBudget.on_threshold` from Python + `.souxmar/budget.toml` loader.

#### Sprint 7 push 1 — ABI v1 frozen FINAL

- **[ABI v1.1 FROZEN]** ADR-0007's two-sprint soak completed cleanly. Conformance was green across Sprint 6 for all 8 in-tree plugins; ASAN/TSAN nightly clean; perf-nightly within threshold; one additive ratchet event landed (the `reader.*` surface, Sprint 6 push 4) and behaved exactly as the ratchet predicted. **The v1 ABI is now immutable for the entire 1.x release series.**
- **`SOUXMAR_ABI_FREEZE_CANDIDATE` removed** from `include/souxmar-c/abi.h`. The status comment is rewritten to name [ADR-0008](docs/adr/0008-abi-v1-final-freeze.md) as the binding declaration; the new wording lists the post-freeze ratchet rules and points at the CI gate. Plugin authors that branched on the candidate macro during soak can now drop the conditional — the contract is permanent.
- **[ADR-0008]** [docs/adr/0008-abi-v1-final-freeze.md](docs/adr/0008-abi-v1-final-freeze.md) — the binding declaration. Inventory of locked headers, post-freeze ratchet rules (additive minor / bug-fix), CI enforcement, the path to a hypothetical v2 (one-major-overlap deprecation). Supersedes ADR-0007, which is now closed.
- **CI lockdown gate** — new `scripts/check-frozen-headers.sh` + an `abi-v1-lockdown` job in `.github/workflows/ci.yml`. PRs that touch any header in the v1 inventory must carry one of two commit-message markers in the PR range:
  - `Ratchet: additive minor surface (ADR-0008)` — new declarations / new headers / new `SOUXMAR_X_*` macros under a fresh prefix.
  - `Ratchet: bug-fix (ADR-0008) — <reason>` — comments / docs / declaration restorations. The Sprint 5 push 4 `souxmar_value_t` typedef fix is the documented precedent.
  Anything else fails the gate. The escape hatch is a Tier-3 ADR per `docs/GOVERNANCE.md` — in practice the trigger for a v2 ABI conversation.
- **`docs/PLUGIN_SDK.md`** — "Current freeze status" section rewritten to **frozen FINAL at v1.1**. Documents the ratchet markers and tells plugin authors the candidate macro is gone.
- **`README.md` § Status** — Sprint 7 push 1 marker; ADR-0008 reference; "frozen FINAL" language.
- **The commit landing this entry is tagged `abi-v1-frozen`** (annotated, signed by a release maintainer). Per `docs/GOVERNANCE.md` § ABI freeze process, this is the State 2 → State 3 transition: candidate → formally frozen. The two-maintainer-approval bar that gates Tier-3 changes was met before merge.
- **No frozen-header surface was modified.** The freeze IS the contract — we did not change a single function signature or struct layout in this push. We removed exactly one declaration (`SOUXMAR_ABI_FREEZE_CANDIDATE`), which was explicitly designated for removal at this milestone by ADR-0007, and rewrote the status comment. The ABI binary surface is byte-identical to v1.1 at end-of-Sprint-6.

#### Sprint 7 push 2 — second solver: elasticity-stub (always-on) + fenicsx-solver (opt-in)

- **`examples/plugins/elasticity-stub/`** — ninth in-tree reference plugin, **always-on**. Registers `solver.elasticity.linear`. Reads `load_magnitude` / `youngs_modulus` / `poisson_ratio` from the stage input bag and produces a per-node 3-component `Field` ("displacement") from the closed-form uniaxial-tension solution:
  ```
  u_x =  (load / E) * x
  u_y = -nu * (load / E) * y
  u_z = -nu * (load / E) * z
  ```
  Documented as a stub: ignores BC manifest, ignores mesh-dependent stiffness. The point is to give the agent eval suite (push 4 of this sprint), the cantilever-beam example, and the documentation tutorials a runnable elasticity solver in the default CI matrix without dragging DOLFINx + PETSc into every build. The closed-form happens to be the analytical answer the FEniCSx adapter validates against on the canonical patch test — the `validating-solver` skill walks the comparison.
- **`examples/plugins/fenicsx-solver/`** — opt-in DOLFINx-backed Poisson solver. Gated behind `-DSOUXMAR_WITH_FENICSX=ON` + `find_package(DOLFINX)`. Registers `solver.heat.fenicsx`. The real plugin: walks a souxmar Tet4 mesh into `dolfinx::mesh::create_mesh`, assembles the P1 Poisson weak form (`a(u,v) = ∫ ∇u · ∇v` and `L(v) = ∫ f v`) with FFCx-generated kernels (`poisson.py` is the canonical UFL source; a developer runs `ffcx` once and commits `poisson.c`), applies homogeneous Dirichlet BCs on the whole boundary, runs a PETSc Krylov solver, reads `u` back into a souxmar `Field`. Declared `single-threaded` because PETSc holds process-global MPI state. The CMakeLists `find_package(DOLFINX QUIET)` and `return()`s with a STATUS skip when DOLFINx isn't installed — `SOUXMAR_WITH_FENICSX=ON` on a DOLFINx-less machine produces a clear skip rather than a noisy linker failure. A separate CMake guard skips with an actionable message when `poisson.c` hasn't been generated. **Not built in default CI**; nightly DOLFINx-bearing runners exercise it.
- **`examples/plugins/fenicsx-solver/README.md`** documents the FFCx regen step, the validation expectation (within FEM discretisation error of `solver.heat.linear` on the same problem; 1e-2 relative bar in the agent eval suite), and the v1 limitations (Poisson only, homogeneous Dirichlet only, single-rank). Sprint 8 lifts the BC + elasticity restrictions via an additive minor ratchet (ADR-0008 compliant — the structured BC array threads through the value bag, not the vtable).
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `elasticity-stub` passes all 10 v1 checks (**9 in-tree plugins green**). The suite itself didn't change; the second solver fits the same shape as the first.
- **`tests/integration/test_elasticity_stub.cpp`** — full `grid-mesher → elasticity-stub` end-to-end: programmatic unit-cube geometry → 3×3×3 grid mesh (27 nodes) → elasticity-stub against arbitrary load / E / ν → pin the closed-form values at the corner (1,1,1) and the origin (0,0,0) within 1e-12. Negative test: missing-mesh input rejected with a structured `DispatchError`.
- **Build**: `examples/CMakeLists.txt` adds `plugins/elasticity-stub` unconditionally; `plugins/fenicsx-solver` only when `SOUXMAR_WITH_FENICSX=ON`. `tests/integration/CMakeLists.txt` depends on `elasticity_stub`.
- **No frozen-header surface was touched.** Both new plugins build against the existing `souxmar-c/solver.h`. ABI v1.1 stays locked; no ratchet marker needed.

#### Sprint 7 push 3 — out-of-core mesh streaming (mmap-backed buffer v2, ABI v1.2 ratchet)

- **[ABI v1.2 — additive minor ratchet event]** `SOUXMAR_ABI_VERSION_MINOR` bumped **1 → 2**. The first post-freeze ratchet event; lands the `souxmar_buffer_t` v2 backing per ADR-0006. The commit carries the `Ratchet: additive minor surface (ADR-0008)` marker, exercising the CI gate that landed in Sprint 7 push 1.
- **New entry points in `souxmar-c/buffer.h`**:
  - `souxmar_buffer_new_mmap(path, size_bytes, flags)` — opens a file-backed mapping. `flags == 0` is "open existing RW file"; `SOUXMAR_BUFFER_FLAG_READONLY` opens read-only and uses the file's natural size when `size_bytes == 0`; `SOUXMAR_BUFFER_FLAG_CREATE` creates+truncates the file (RW only — `READONLY | CREATE` is rejected with NULL).
  - `souxmar_buffer_is_mmap(buffer)` — explicit kind discriminator for tooling. Returns 1 for both RW and RO mappings, 0 for heap-backed (the v1 path) and NULL inputs.
  - The existing v1 accessor surface (`souxmar_buffer_new` / `_free` / `_data` / `_data_const` / `_size` / `_alignment`) is unchanged; plugins that only use those work on a v1.2 host without recompilation, and they continue to return heap-backed buffers when called.
- **Implementation in `src/core/c_abi_buffer.cpp`**:
  - The `BufferHeader::reserved` field that ADR-0006 explicitly reserved for this exact purpose is now the `kind` discriminator. v1 zero-initialised it (→ `KindHeap`) so the v1 binary layout is **byte-identical** — no consumer of the v1 ABI shifts. Mmap-backed headers add a small set of trailing fields (mmap address, length, fd/HANDLE) used only by `souxmar_buffer_free` to unmap and close.
  - POSIX path: `open` + optional `ftruncate` + `mmap(MAP_SHARED)`. Windows path: `CreateFileA` + `SetEndOfFile` + `CreateFileMappingA` + `MapViewOfFile`. Both paths funnel through the same `BufferHeader` and the same `souxmar_buffer_free` cleanup.
  - `souxmar_buffer_data(rw_mapping)` returns the mmap address; `souxmar_buffer_data(ro_mapping)` returns NULL (callers use `_data_const`). The asymmetry surfaces an obvious bug class — a plugin writing to a read-only mapping — at the API boundary rather than via SIGSEGV.
- **`souxmar_mesh_from_buffers` works unchanged.** The bulk mesh ingest reads buffers through `souxmar_buffer_data_const`, which transparently returns either the heap data slot or the mmap region. **Out-of-core mesh ingest is operational with zero changes to `c_abi_mesh.cpp` or any caller** — that's the whole point of the v1 → v2 forward-compatibility plan.
- **[ADR-0006 v2 section]** Documents the v2 implementation, the ratchet that lifted minor 1 → 2, and the still-deferred subprocess shared-fd path (Sprint 8+ alongside the OpenFOAM subprocess plugin). v1 plugins that call only `souxmar_buffer_new` keep working unchanged.
- **Tests** (`tests/unit/test_c_abi_buffer.cpp`):
  - `IsMmapFalseForHeapBacked` — v1 buffers correctly self-report.
  - `RoundTripRwThenReadOnly` — write 4 KiB pattern through an RW mapping → free → reopen RO → byte-by-byte verify.
  - `NullPathRejected`, `ReadOnlyAndCreateAreMutuallyExclusive`, `MissingFileReadOnlyFails` — error paths.
  - `BulkMeshIngestThroughMmapBuffers` — the integration claim: build a full 5-node / 2-tet mesh whose four bulk buffers (coords/types/conn/offsets) are all mmap-backed; `souxmar_mesh_from_buffers` ingests them transparently.
- **Benchmark** (`benchmarks/bench_mmap_buffer.cpp`): heap-roundtrip vs mmap-create-write vs mmap-reopen-readonly across 1 / 16 / 64 / 256 MiB sizes. The CI baseline carries the expected ratio (≤ 1.2× heap path at 64 MiB+); the perf-nightly workflow tracks it.
- **No load-bearing v1 surface changed.** Every v1 function signature is byte-identical; every v1 struct layout is byte-identical (the `reserved → kind` rename is a comment-level change with identical semantics for zero-init). The ratchet marker the commit carries is the additive-minor variant, which the CI gate accepts as exactly the case it was designed to permit.

#### Sprint 7 push 4 — agent eval suite v1 (30 canonical tasks + `souxmar-eval` runner)

Closes the Sprint 7 plan's "Agent eval suite v1: 30 canonical engineering tasks with pass/fail criteria" deliverable.

- **`souxmar-eval` runner** (`tools/eval/main.cpp`, `tools/eval/CMakeLists.txt`) — new operator binary, pair-built with the existing `souxmar` and `souxmar-conformance` under the `SOUXMAR_BUILD_CLI` gate. Walks an evals directory, parses each `.yaml` task via yaml-cpp, instantiates a `Registry + RegistryDispatcher + ToolContext`, loads every discoverable plugin, and runs each task's steps through `ai::dispatch_tool`. Per-task `[PASS]` / `[FAIL]` lines + a per-category summary go to stdout; exit 0 iff every task passes.
- **Eval YAML schema** — small, tagged-union assertion language designed for the agent surface specifically:
  - `tool_outcome` — `value: ok | fail`. The pass-vs-fail discriminator for any tool call.
  - `tool_error_code` — when a tool fails, assert on the structured code (`NOT_AVAILABLE` / `INVALID_ARGUMENT` / `PLUGIN_NOT_FOUND` / `PRECONDITION_FAILED` / …). The dispatcher's structured-error surface is the contract being pinned.
  - `tool_data_equals` — dotted-path lookup into `result.data` with scalar equality.
  - `tool_data_gte` — same path, numeric `≥`.
  - `tool_data_present` — non-null entry at a path.
  - `tool_summary_contains` — substring search on the human-readable summary.
  - Each assertion defaults to checking the **last** step's result; `step: N` targets earlier steps. `setup.session_state` pre-populates `ctx.session_state` for read-tier tasks. `auto_confirm: true` (default) installs an always-yes prompter so `ConfirmOnce` / `ConfirmAlways` tools run without bailing at `NOT_CONFIRMED`.
- **`evals/v1/` catalogue — 30 tasks**, covering every v1 tool, grouped:
  - **read** (4): `read_geometry_summary` against pre-staged geometry / inline input / missing-geometry NOT_AVAILABLE path.
  - **mesh** (4): hello-mesher success path; unknown-capability `PLUGIN_NOT_FOUND`; missing `capability_id` `INVALID_ARGUMENT`; mesh-stashes-handle proved via a follow-on solve.
  - **bc** (3): Dirichlet clamp; Neumann flux as a vector; chained Dirichlet + Neumann + Robin with counter monotonicity.
  - **material** (3): linear-elastic steel; thermal conductivity; missing-`properties` `INVALID_ARGUMENT`.
  - **solve** (3): `solver.heat.linear` over hello-mesher; `solver.elasticity.linear` (Sprint 7 push 2 stub) with closed-form load/E/ν inputs; no-prior-mesh `PRECONDITION_FAILED`.
  - **query** (3): `query_field` after heat solve; without a field; after elasticity solve (vector field, 12 doubles total).
  - **quality** (3): `query_mesh_quality` on hello-mesher's tet (zero inverted / zero unsupported); no-mesh `PRECONDITION_FAILED`; `source: dispatched` when no quality field is on the session.
  - **postproc** (3): `compute_field` via `postproc.scalar_magnitude` (vector → scalar); unknown postproc `PLUGIN_NOT_FOUND`; no-prior-solve `PRECONDITION_FAILED`.
  - **pipeline** (2): `propose_pipeline` valid 2-stage spec; duplicate-stage-id rejected.
  - **diff** (2): `apply_pipeline_diff` add-stage-after-mesh; remove-leaving-dangling-`{from:}` rejected by the parser round-trip.
- **`evals/v1/README.md`** — schema reference, the category table, how to author a new task. The catalogue is documented as a ratchet: it grows by PR, never shrinks, and pass criteria never loosen.
- **`.github/workflows/eval-nightly.yml`** — nightly + PR-gated (on `tools/eval/**`, `evals/v1/**`, `src/ai/**`, `include/souxmar/ai/**`) workflow. Builds the runner + every example plugin, walks `build/.../examples/plugins/*/` to assemble `--plugin-path` args, runs the suite, uploads the report as an artifact. Fails the workflow on any task failure.
- **No frozen-header surface touched.** This is pure new tooling under `tools/eval/` + a YAML catalogue under `evals/`. No commit-message ratchet marker needed.

#### Sprint 7 push 5 — ADR-0009: OpenFOAM process isolation

Closes the Sprint 7 plan's "OpenFOAM legal/process-isolation ADR finalised (precondition for Sprint 8)" deliverable. Pure docs/design — no code, no build changes, no frozen-header touch.

- **[ADR-0009]** [docs/adr/0009-openfoam-process-isolation.md](docs/adr/0009-openfoam-process-isolation.md) — binding architectural decision: the Sprint 8 OpenFOAM adapter **must** invoke OpenFOAM as a child process via `exec(3)`, never link `libOpenFOAM.so`. The GPL ↔ Apache 2.0 license boundary forces this; the FSF + OpenFOAM Foundation FAQs explicitly recognise the subprocess workaround. The ADR carries the legal context, the case-directory IPC contract (PolyMesh + `system/` + `0/` dictionary files), the crash + timeout isolation story (subprocess boundary IS the guard), the per-release version-pin policy (OpenFOAM Foundation v11–v12; foam-extend not supported in v1), and the implementation hand-off to Sprint 8 push 1.
- **Risk register** (`docs/SPRINT_PLAN.md`): R-003 likelihood downgraded High → Med now that the design contract is in place; closure deferred to the Sprint 8 push 1 implementation landing.
- **Plugin host surface unchanged.** The OpenFOAM plugin will expose the standard `solver.*` C ABI to the host (`souxmar_solver_vtable_t`); the GPL boundary lives entirely below that vtable. The plugin's manifest declares Apache-2.0 (the *plugin*'s license, since it links nothing GPL'd); the OpenFOAM binary it `exec`s remains the operator's separate install.
- **Pre-mortem one year out**: the most likely failure mode is the agent fumbling case-file authoring (LLMs without targeted training data write plausible-but-wrong `fvSchemes` / `fvSolution` dictionaries). The ADR names this explicitly + lists the leading indicators (CFD-bucket failure rate in the agent eval suite ≥ 30% is the trigger to revisit the agent surface, not the adapter).

#### Sprint 7 push 6 — `v0.9.0-beta1` release scaffolding + sprint close-out

Closes Sprint 7. **First public pre-release tagged `v0.9.0-beta1`.**

- **`VERSION` bump 0.0.1 → 0.9.0.** CMake's `project(VERSION ...)` reads through; the SemVer pre-release suffix (`-beta1`) lives in the git tag, not the `VERSION` file (CMake's project version is required strict `MAJOR.MINOR.PATCH`).
- **CHANGELOG release-stamping ritual.** The entire `[Unreleased]` block from Sprint 1 through Sprint 7 push 5 is promoted to `[0.9.0-beta1] - 2026-05-11`. A fresh `[Unreleased]` block opens at the top with the five empty sub-sections per `docs/RELEASE_NOTES_TEMPLATE.md`.
- **`.github/workflows/release.yml`** — triggered by tags matching `v*`. Builds three artefacts in parallel:
  - **source tarball** via `git archive` (reproducible by construction).
  - **Linux x86_64 CLI tarball** — Release-build `souxmar` + `souxmar-conformance` + `souxmar-eval` binaries plus the in-tree example plugins staged as a sibling `plugins/` directory (so `SOUXMAR_PLUGIN_PATH=<install>/plugins` is a one-liner).
  - **Python sdist** via `python -m build --sdist` against `bindings/python/`.
  Composes a **draft** GitHub release (a release maintainer pastes in the body from `docs/RELEASE_NOTES_TEMPLATE.md` and publishes); attaches a `SHA256SUMS` file alongside. The workflow auto-detects pre-release tags (`-alpha` / `-beta` / `-rc`) and marks the release accordingly.
- **`docs/RELEASE_NOTES_TEMPLATE.md`** — pre-fills the structure: tag, ABI version, what landed, what's deliberately not here, ABI ratchet history, downloads + SHA-256 table, install snippets, known issues, contributors. Future release maintainers fill it in once per release.
- **`docs/retros/sprint-07.md`** — keep / fix / one ADR-worthy decision artefacts per `docs/SPRINT_PLAN.md` § Retro practice. Names the candidate decision (subprocess plugins as a first-class category, surfaced by ADR-0009) deferred to a future ADR-0010. Capacity for Sprint 8 revised down from 71 pts (85% of 84) to ~55 pts based on Sprint 7's measured rate.
- **README** — banner header announcing the `v0.9.0-beta1` tag + GitHub release link; runnable-today + not-yet-done sections refreshed to reflect Sprint 7's full delivery (9 in-tree plugins, 3 opt-in adapters, `souxmar-eval` CLI, frozen-final ABI v1.2, out-of-core mesh streaming, ADR-0009).
- **No frozen-header surface touched.** Pure release-ritual scaffolding. No ratchet marker needed.

This release is the contractual moment Sprints 5–7 have been building toward: every plugin author can now target a stable ABI with a published version number and a downloadable SDK. Sprint 8 starts on the OpenFOAM adapter + the Blender importer + the agent tool catalogue 12 → 18.

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## How to read this file

- `[Unreleased]` accumulates changes between releases. At release time, the section is renamed to `[X.Y.Z] - YYYY-MM-DD` and a fresh `[Unreleased]` is opened.
- Sections are added only when they have content; empty sections are omitted from a tagged release.
- Each entry is one line, present tense, references the PR or issue when meaningful.
- ABI / pipeline-format / agent-tool changes are called out explicitly with a `**[ABI v1]**` / `**[pipeline-format v1]**` / `**[agent-tool v1]**` prefix so plugin authors and integrators can scan the file for impact.
