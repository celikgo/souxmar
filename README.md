# souxmar

An open-source CAE platform: parametric CAD, mesh generation, FEM and CFD, post-processing — wrapped in a cross-platform desktop app with an agentic AI chat that can drive the entire pipeline.

For mechanical, structural, aerospace, and architectural engineers who want a Cursor-style experience for simulation work: open the app, describe the problem in chat, watch it mesh and solve, inspect results in a built-in viewport. C++20 core, Python bindings, stable C plugin ABI, Tauri + React desktop app, Apache 2.0.

souxmar does not replace FreeCAD, Gmsh, FEniCSx, OpenFOAM, Blender, or ParaView. It unifies them under a shared data model and a stable plugin ABI, then puts a modern UI and an agentic AI on top.

## Why

Today, going from a CAD model to results means stitching half a dozen projects together with bespoke Python glue, four file formats, four release cycles, and at least one dropped boundary tag. Reproducing a colleague's setup is hard. Trying a new meshing algorithm against a real industrial model is harder. souxmar treats the pipeline itself — and the user-facing app around it — as the product.

Read [`docs/VISION.md`](docs/VISION.md) for the long version.

## Surfaces

- **Desktop app** (macOS / Windows / Linux) — chat panel, 3D viewport, pipeline editor, inspector. The primary surface for engineers.
- **CLI** (`souxmar`) — for CI, batch runs, scripting.
- **Python** (`pysouxmar`) — for notebooks and embedding in larger tools.
- **Plugin SDK** — stable C ABI for shipping your own meshers, solvers, elements, readers, writers as out-of-tree binaries.

## How it's funded (open-core)

The library, plugin SDK, CLI, Python bindings, **and the desktop app itself** are open source under Apache 2.0 — no crippled "community edition." Optional managed services (managed AI, cloud sync, hosted compute, team features) are commercial. See [`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md).

The free tier is the full product. You bring your own Anthropic / OpenAI / local-Ollama key, your own compute, and you owe us nothing.

## Status

🎉 **Stable — `v1.0.0` tagged 2026-05-22.** First stable release. End of the v0.x window. Plugin C ABI **v1.3 FINAL** (frozen forever within v1.x); agent tool contract **v1 FINAL** at 18 tools; on-disk pipeline format **v1 FINAL**; update manifest **v1 FINAL**; bridge ABI v3 (additive Tier-0 evolution allowed through v1.x). 24 sprints, ~150 pushes, 36 ADRs. Apache-2.0. BYOK default; Pro tier opt-in. Six Pro-tier services live (managed-AI proxy, cloud sync, plugin marketplace, billing, account portal, hosted-compute offload). Three desktop FFI surfaces structural (pipeline_introspection, provider_call, auto_updater_menu); viewport_renderer lands in v1.1.0. Sprint 24 retro: [`docs/retros/sprint-24.md`](docs/retros/sprint-24.md). [ADR-0036](docs/adr/0036-v1-final-freeze.md) names the final freeze.

**v1.x roadmap (per ADR-0036):** v1.0.1 (Enterprise E2E cloud sync), v1.0.2 (paid plugins + publisher onboarding), v1.1.0 (Three.js + VTK.js viewport rendering). The next ABI break is v2.0 — post-v1.0 future-sprint scope.

What changed since v0.9.5 (Sprint 18 in full):

- **ADR-0028 — Pipeline ExecutionRouter** (Sprint 18 push 1). Picks Option 2: routing decided at parse time, per-stage dispatcher selected. Sprint 19 push 1 lands the implementation alongside the OffloadingRouter consumer.
- **ADR-0029 — Multi-window + per-project AI isolation** (Sprint 18 push 2). One project per window; five-rule data-isolation invariant (window-local Zustand store, per-call project-id, per-project BYOK keychain namespace, per-call project.ai.toml lookup, window-local session budget).

**Older banner (v0.9.5, 2026-05-15):** the six-service Pro-tier surface is **complete in scaffold** across [ADR-0019](docs/adr/0019-managed-ai-proxy-architecture.md)–[ADR-0027](docs/adr/0027-compute-offload.md) — managed-AI proxy, cloud-sync, plugin marketplace, billing, account portal, hosted-compute offload. Typed CLI `--json` shape contract ratified ([ADR-0025](docs/adr/0025-cli-json-output-shapes.md)) with a Rust mirror in `souxmar-bridge::cli_shapes`. Show HN + 5 partner outreach emails (ADR-0017 launch-comms amendment). Sprint 17 retro: [`docs/retros/sprint-17.md`](docs/retros/sprint-17.md). **External bug reports remain the most useful contribution.**

Older history — Sprint 17 + earlier:

- **Account portal scaffold + [ADR-0026](docs/adr/0026-account-portal.md)** (Sprint 17 push 2). 5th Pro-tier service. `account.souxmar.dev` as the source of truth for souxmar identities + the three token namespaces (`sxm_pro_*` proxy, `sxm_lic_*` plugin license, `sxm_sync_*` cloud sync). Stripe.js checkout iframe + email-link sign-in (no passwords).
- **Compute-offload scaffold + [ADR-0027](docs/adr/0027-compute-offload.md)** (Sprint 17 push 3). 6th + last Pro-tier service. Per-stage routing via `execution.target: "managed"` in pipeline YAML; per-tier core-hour quotas aligned with the billing service.
- **ADR-0025 typed CLI shapes** (Sprint 17 push 1). Every `souxmar` `--json` output now carries `schema` + `kind` discriminators; `souxmar-bridge::cli_shapes` provides a typed Rust mirror with serde-default forward compat. Three unit tests in the mirror cover the round-trip / forward-compat / unknown-kind cases.
- **Launch-comms execution** (Sprint 17 push 1). ADR-0017's amendment from Sprints 15-16 finally executes — Show HN post + 5 partner outreach emails today; metrics reportable in Sprint 21's retro per ADR-0017's "≥ 20 reports / month" target.

What changed in Sprint 16:

- **ADR-0022 — MVC-via-subprocess** (Sprint 16 push 1). State-machine surfaces expose READ through FFI; APPLY through subprocess shell-out. Three rules feed the principle: frozen-contract, single-process-invariant, no-difference-in-UX. Sprint 16 push 4's plugin install ratchets the same shape; Sprint 17's account-portal refresh follows.
- **Plugin marketplace v1 scaffold + [ADR-0023](docs/adr/0023-plugin-marketplace-v1.md)** (Sprint 16 push 2). New `services/plugin-marketplace/` axum binary; v1 endpoints for plugin metadata + license issuance + license check + publisher key fingerprints. Paid plugins follow the same ed25519-detached signature surface as free plugins; 70/30 publisher/platform revenue split. 90-day entitlement-receipt cache so re-installs don't require online check.
- **Stripe billing integration POC + [ADR-0024](docs/adr/0024-stripe-billing-integration.md)** (Sprint 16 push 3). New `services/billing/` mediates Stripe for both proxy quota and marketplace licensing. Three operating modes (disabled / test / live); mode/key consistency check at startup refuses mode=live with sk_test_* (or vice versa). Subscription plans codified (Pro $30/mo, Team $100/seat/mo, Enterprise custom).
- **`souxmar plugin install`** (Sprint 16 push 4). CLI subcommand with `--license <sxm_lic_*>` + `--json`. CLI surface ratchets; the actual marketplace download + signature-verify + extract loop is queued for Sprint 17 push 2 alongside the marketplace service's real `/v1/license/check` response.
- **INFRA_STATUS stale-counter** (Sprint 16 push 1). Each wired-but-empty gate tracks how long it has been waiting on its first real data. Threshold of 5 sprints escalates to "what to fix"; none have hit it yet.

Older history — Sprint 15 + earlier:

- **ADR-0020 + per-project `project.ai.toml`** (Sprint 15 push 2). Sibling file (gitignored by default) selects per-project AI provider. Schema=1 + typed loader; the bridge consults it on every chat call. Real Ollama integration wired through; BYOK Anthropic + Managed surface honest NOT_CONFIGURED until forwarders land.
- **Anthropic forwarder for the managed-AI proxy** (Sprint 15 push 3). `/v1/chat` is no longer a 503 stub; it POSTs to api.anthropic.com via reqwest, returns the typed ChatResponse. Operator-supplied `SOUXMAR_PROXY_ANTHROPIC_KEY` env var; the user-facing `sxm_pro_*` token never reaches Anthropic.
- **Cloud-sync MVP scaffold + [ADR-0021](docs/adr/0021-cloud-sync-architecture.md)** (Sprint 15 push 3). Architecture decided: last-write-wins + per-tier encryption (Pro/Team encrypted-at-rest, Enterprise E2E in Sprint 20), separate Rust service. MVP returns honest 503s; Sprint 16 wires the S3 backend.
- **Third real FFI — `auto_updater_menu`** (Sprint 15 push 4). Bridge ABI bumps 2 → 3. Read-only surface; apply / rollback shell out to the CLI per the MVC-via-subprocess pattern (ADR-0022 queued for Sprint 16).
- **services-build.yml + INFRA_STATUS.md** (Sprint 15 push 1). Every Rust service under `services/*/` compiles in CI; a cross-cutting status doc names which gates are wired-but-empty.

Older history — Sprint 14 + earlier:

- **Managed-AI proxy MVP scaffold + [ADR-0019](docs/adr/0019-managed-ai-proxy-architecture.md)** (Sprint 14 push 3). New `services/managed-ai-proxy/` directory tree — axum + tokio binary scaffold serving `/v1/chat`, `/v1/account`, `/v1/quota` with honest 503s. Architecture: stateless proxy, opaque souxmar tokens (sxm_pro_<32-hex>), pre-charge per token with in-flight holds (catches "out of Pro quota" *before* the upstream API spend), separate Rust binary outside the desktop workspace, mutually exclusive coexistence with BYOK per-project.
- **Second real FFI — `provider_call`** (Sprint 14 push 4). New `include/souxmar-c-bridge/provider.h` + impl. The desktop Chat panel now calls through `libsouxmar-c-bridge` into the engine's Provider abstraction; `BridgeFeatureSet::provider_call` flips structural when the build is real-ffi. Bridge ABI bumps to v2 — additive surface growth, old clients refuse mismatched calls cleanly via the per-call ABI-byte cross-check. Two of six BridgeFeatureSet flags are now structural.
- **synth-load `--bootstrap` + per-platform VR matrix** (Sprint 14 push 1). The harness now ships with the mechanism to seed itself in one maintainer-reviewed pass; new `visual-regression.yml` matrix workflow runs the Playwright suite on Linux + macOS + Windows, each producing its own per-platform baselines directory. Sprint 15 commits the initial corpora.
- **`desktop-ffi.yml` workflow** (Sprint 14 push 2). Validates the C bridge + Rust souxmar-bridge build with `--features real-ffi` on every relevant PR. R-016 closes.

Older history — Sprint 13 + earlier:

- **First real FFI: `pipeline_introspection`** ([ADR-0018](docs/adr/0018-c-bridge-ffi-surface.md), Sprint 13 push 3). New `libsouxmar-c-bridge.a` static library exposes a six-function schema=1 C ABI; the Rust `souxmar-bridge` crate's `ffi` module + `build.rs` + `real-ffi` cargo feature wire it through; the BridgeFeatureSet's first contract flag flips from "always false" to "true when the build was real-ffi." Inspector panel renders the real stage list parsed through the C bridge. ABI-version byte cross-checked on every FFI call so partial-upgrade scenarios fail loudly.
- **ADR-0017 + synthetic-load harness** (Sprint 13 push 1). Picks Option B of the three plausible bug-discovery models the Sprint 12 retro queued — external-first stays the headline strategy, with `scripts/synth-load/run.sh` providing breadth-coverage regression-net on every commit. Same triage SLA matrix for external and synthetic; rotation doesn't distinguish at triage time.
- **Auto-generated agent tool docs** (Sprint 13 push 2). `souxmar agent list --json` → `scripts/docs-site/gen-agent-tools.py` → `docs-site/agents/tools.md`. eval-nightly verifies drift with `--check-only`. Plus VTU consumer-conformance test + visual-regression baselines policy doc (both Sprint 11/12 carry-overs).
- **cfd-stub per-patch BC routing** (Sprint 13 push 4). Closes the Sprint 10 carry-over. Honours `patches: [{ name, tag, bc }, ...]` with `wall > inlet > outlet > bulk` precedence. First entry in `docs/bug-reports/` codifies the ADR-0017 triage-record format.

What changed in Sprint 12 (older history, but still relevant):

- **Public docs site at docs.souxmar.dev** (Sprint 12 push 3). Vitepress 1.5 with landing + install + first-pipeline + agent reference + plugin authoring + business-model pages. CI publishes to GitHub Pages on every master push touching `docs-site/`. DNS CNAME is pending out-of-band setup.
- **Bug-triage workflow + COMMUNITY.md** (Sprint 12 push 1). Public response-SLA contract (P0 24h / P1 48h / P2 5bd / P3 2w). Auto-acknowledgement bot replies to every new issue with the matching SLA + auto-labels by surface.
- **souxmar-bridge FFI skeleton + BridgeFeatureSet** ([ADR-0016](docs/adr/0016-bridge-feature-set-contract.md), Sprint 12 push 2). New Rust crate exposing a 6-field struct that workbench panels query to decide "real vs scaffolding." Sprint 13 flipped the first flag (`pipeline_introspection`); three more follow through Sprint 17.

Older history — Sprint 11 + earlier:

- **libsouxmar-crypto extraction** ([ADR-0015](docs/adr/0015-libsouxmar-crypto-extraction.md), Sprint 11 push 1). `souxmar::crypto::{ed25519_verify, hex_encode, hex_decode, sha256_hex}` moves into `src/crypto/`; `souxmar::update::*` forwarders preserve the existing call sites. The auto-updater is the first consumer; plugin marketplace (S16) + cloud sync (S15) build on the same primitives.
- **`souxmar update fetch`** (Sprint 11 push 2). HTTPS downloader for the auto-updater's manifest + .sig pair, backed by curl-as-subprocess. `--manifest-url <https-url>` + `--out-dir` + `--insecure`. Plus cmake-configure-time auto-regen of the in-tree dev signing key when missing.
- **Agent eval suite expansion to 43 tasks** (Sprint 11 push 3). Six new categories (export, listing, cfd, multistep, recovery, screenshot); 13 new YAML tasks exercising chained tool sequences + session-state-after-error patterns. Nightly eval workflow gates at `--min-pass-rate 0.90` per SPRINT_PLAN.md § Sprint 11.
- **Desktop workbench shell scaffold** (Sprint 11 push 4). Three-panel layout (viewport + inspector + chat) under `src/desktop/src/workbench/`. Each panel's empty state is honest about the Sprint 12+ FFI dependency; the chat surface is fully exercisable in `tauri dev` via a stubbed `chat_send` command.
- **Playwright visual-regression harness** (Sprint 11 push 1 + 4). `tests/visual/` covers the four onboarding-wizard steps + two workbench states; baselines commit from CI artefacts on first run per `tests/visual/README.md`.

What changed in Sprint 10 (still the most recent feature-heavy sprint):

- **Auto-updater XL story closed across pushes 4–8.** ADR-0013 locks the signed-manifest format + ed25519 detached signature scheme; the verifier (push 5, libsodium PRIVATE-linked), the apply-gate state machine (push 6, 9-value `RefusalReason` enum, pure logic + injectable `TimeSource`), the install layout + atomic apply/rollback (push 7, marker-file approach over symlinks for cross-OS uniformity), and the release-signing automation (push 8, notarytool + signtool + GPG + manifest-signing scripts + ADR-0014 yearly key rotation procedure + the embedded `SOUXMAR_RELEASE_PUBKEY_HEX` cache var). The trust path is end-to-end: bytes get hashed against `manifest.artifact.sha256` before staging; the rollback log records the SignatureStatus verbatim so a refused update is queryable.
- **Perf-baseline rotation closes R-011** (Sprint 10 push 1). The five-binary perf suite from Sprint 9 now gates against real baselines; per-PR runs detect regressions > 5 % cleanly.
- **Plugin index v0 + `souxmar plugin search`** (Sprint 10 push 2). Static `docs/plugin-index.toml` + the `souxmar::plugin::IndexEntry` data model + a substring/capability-filter CLI search surface.
- **Plugin-index publication workflow** (Sprint 10 push 3). PR-gated; `.github/workflows/plugin-index.yml` runs `souxmar-conformance` against each new listing's published binary on all four CI platforms; the conformance badge surfaces in `souxmar plugin search`.
- **`souxmar::ai::Provider` abstraction + OllamaProvider** (Sprint 10 push 9). Synchronous `chat_completion()` returning a typed result; `StubProvider` for CI; `OllamaProvider` via curl-as-subprocess (reuses Sprint 8 push 1's harness — no in-process HTTP client dep). New `souxmar-eval-llm` runner separate from the scripted `souxmar-eval`; per-model pass-rate matrix in `docs/ai-providers/ollama-compatibility.md` covers Llama-3.x / Qwen-2.x / Mistral-Nemo.
- **Desktop onboarding wizard + Tauri 2 scaffold** (Sprint 10 push 10). `src/desktop/` from scratch: Tauri 2 Rust shell + React 18 + TS + Vite frontend; four-step wizard with token-driven dim-theme styling; five `#[tauri::command]` entry points (onboarding bit, BYOK keychain write via the `keyring` crate, sample-project copy). The workbench shell beyond the wizard is still empty — Sprint 11 dogfood week is the next exit criterion.
- **Mesh-algorithm comparison study** (Sprint 10 push 11). `examples/mesh-comparison/` runs both `mesher.tetra.grid` (always-on) and `mesher.tetra.gmsh` (opt-in) against the same `cube.step`, hand-parses per-cell quality DataArrays out of the resulting VTUs (no vtk-python dep), renders a markdown report with inline-PNG histograms. Sets up the plugin-marketplace "does this mesher actually deliver?" evidence shape.

The ABI v1 soak that ran across Sprints 5–7 picked up its third additive minor ratchet in Sprint 9 push 2 (per-face tags, v1.2 → v1.3) — handled cleanly via the `Ratchet: additive minor surface (ADR-0008)` marker, no breaking changes. The freeze is permanent for the entire 1.x release series; `scripts/check-frozen-headers.sh` enforces the ABI ratchet and `scripts/check-tool-contract.sh` enforces the matching agent-tool-contract ratchet (Sprint 9 push 1 flipped that gate blocking-by-default). Both surfaces are under blocking lockdown on every PR.

Runnable today:

- **CLI**: `souxmar run <pipeline.yaml>`, `souxmar plugin {list,search,validate-index}`, `souxmar update {check,apply,rollback}` (Sprint 10 pushes 6+7 — signature-verifying auto-updater against signed manifests, atomic-swap apply, audit-logged rollback), `souxmar agent {list,invoke}` (with `--audit-log`, `--budget-config`, `--yes`), `souxmar-conformance <dir>`, `souxmar-eval <evals-dir>`, `souxmar-eval-llm <evals-dir>` (Sprint 10 push 9 — LLM-driven compatibility-matrix generator against Ollama).
- **Python**: `pip install pysouxmar` → parser, registry, loader, runner, cache, **18 agent tools**, audit log, first-class `SessionBudget.on_threshold` callback, `.souxmar/budget.toml` loader.
- **Plugin SDK**: **frozen-final C ABI v1.3** across six capability namespaces (`reader.*`, `mesher.*`, `solver.*`, `writer.*`, `postproc.*`, plus the bulk-buffer ingest path), now with **per-face tags** on `souxmar_mesh_*` (ADR-0012); `souxmar_add_plugin` CMake macro; conformance suite + CI lockdown gate; host-side **subprocess harness** for plugins that drive external binaries.
- **Eleven in-tree reference plugins** (always-on): hello-mesher, grid-mesher, hello-writer, vtu-writer, heat-solver, elasticity-stub, cfd-stub, scalar-magnitude, mesh-quality, stl-reader, obj-reader.
- **Five opt-in external adapters**: `occt-reader` (`-DSOUXMAR_WITH_OPENCASCADE=ON`, STEP / IGES), `gmsh-mesher` (`-DSOUXMAR_WITH_GMSH=ON`), `fenicsx-solver` (`-DSOUXMAR_WITH_FENICSX=ON`, FEM Poisson), `openfoam-solver` (`-DSOUXMAR_WITH_OPENFOAM=ON`, three CFD capabilities), `blender-reader` (`-DSOUXMAR_WITH_BLENDER=ON`, `.blend` import).
- **Five runnable examples**: `examples/cantilever-beam/`, `examples/thermal-fin/`, `examples/stl-cube/`, `examples/pipe-bend/`, and the new `examples/mesh-comparison/` (Sprint 10 push 11 — runs both meshers, renders a comparison report). Plus the `examples/swap-mesher/` documentation set showing the one-line `grid → gmsh` swap.
- **Out-of-core mesh streaming**: mmap-backed `souxmar_buffer_t` v2. `souxmar_mesh_from_buffers` routes transparently to heap or mmap.
- **Parallel runner**: `RunOptions::max_workers > 1` schedules independent DAG branches with per-plugin reentrancy guards.
- **Agent tool surface v1 (frozen final, ADR-0011)**: 18 tools across categories Read / Mesh / BC / CFD / Material / Solve / Field / Pipeline / Discovery / Export / UI. Structured audit log, per-project token budget config. **30-task agent eval suite** runs nightly; per-provider scores (Anthropic 94 %, OpenAI 92 %, Ollama 89 %) cleared the freeze gate.
- **Perf-regression CI at 5 % per-PR** ([`ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md) § Performance budgets matched). Five benchmark binaries: `bench_mesh_construction`, `bench_mmap_buffer`, `bench_face_tag`, `bench_plugin_dispatch` (< 20 µs warm dispatch budget), `bench_heap_accountant` (< 1 µs always-on accounting). Self-contained HTML dashboard generated per release.
- **Eval suite v1 latency capture**: `souxmar-eval --latency-output` emits per-tool + aggregate p50/p95/p99/mean/max JSON; `--max-p95-ms` gate carries the future BYOK first-token budget (< 800 ms p95 per ENGINEERING_PRACTICES.md).
- **Audit log carries heap deltas** on Linux + glibc ≥ 2.33 — per-tool `heap_bytes_delta` field surfaces leak indicators and per-call cost in the agent UI.

Not yet done — deliberately scoped out of `0.9.0-beta4`:

- **Desktop workbench shell is empty** — Sprint 10 push 10 lands the Tauri scaffold + the four-step onboarding wizard against the dim-theme tokens, but the post-onboarding workbench (viewport + chat + inspector) is still a Sprint 11+ deliverable. The wizard itself isn't yet visually-regression-tested; that landed as Sprint 10's R-012 risk and closes in Sprint 11 push 1.
- **HTTPS fetcher for `souxmar update apply` not wired** — today `--artifact <local-path>` is the boundary; the desktop app (or a future `souxmar update fetch`) is expected to download separately. The trust path is honest (bytes get hashed against `manifest.artifact.sha256` regardless of source); the auto-updater is *complete but incomplete*. Sprint 11+ wires `souxmar update fetch` against the curl-subprocess pattern push 9 validated.
- **AnthropicProvider / OpenAIProvider not implemented** — the abstraction (Sprint 10 push 9) is ready; the OllamaProvider exercises the full Provider surface; the BYOK managed-AI path lands in Sprint 14 alongside the Pro-tier proxy.
- **No production-grade FEM solver yet** — `solver.heat.linear` + `solver.elasticity.linear` are closed-form demonstrations; the FEniCSx adapter (opt-in, Poisson only in v1) handles the real case.
- **No per-patch CFD BC routing for the always-on `cfd-stub`** — the OpenFOAM adapter does this since Sprint 9 push 3 but the stub remains single-patch; `apply_inlet`/`apply_wall`/`apply_outlet` stage BCs through the v1.3 face-tag surface.

## Building

Prerequisites:

- CMake ≥ 3.25, Ninja, a C++20 compiler (GCC 13 / Clang 17 / AppleClang / MSVC 19.36+)
- [vcpkg](https://github.com/microsoft/vcpkg) — cloned and `VCPKG_ROOT` exported

```bash
git clone https://github.com/souxmar/souxmar.git
cd souxmar

export VCPKG_ROOT="$HOME/vcpkg"   # or wherever you cloned vcpkg

cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Other presets (see [`CMakePresets.json`](CMakePresets.json)): `ci-linux-gcc`, `ci-linux-clang`, `ci-macos`, `ci-windows`, `asan`, `tsan`.

The first `cmake --preset` invocation builds vcpkg dependencies from source (~5 min for the default feature set; longer if heavy adapters are enabled). Subsequent runs use the vcpkg binary cache.

## Documents

Product & architecture:

- [`docs/VISION.md`](docs/VISION.md) — purpose, target users, scope, non-goals
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — layered architecture, data model, dependencies
- [`docs/DESKTOP_APP.md`](docs/DESKTOP_APP.md) — Tauri + React stack, layout, performance budgets
- [`docs/AI_INTEGRATION.md`](docs/AI_INTEGRATION.md) — agentic chat, BYOK credentials, tool surface
- [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) — design system using the Twitter dim palette
- [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md) — C ABI, plugin lifecycle, versioning
- [`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md) — open-core model, tiers, marketplace

Engineering & process:

- [`docs/SPRINT_PLAN.md`](docs/SPRINT_PLAN.md) — 24-sprint plan with team commitments, exit criteria, risks
- [`docs/TEAM_STRUCTURE.md`](docs/TEAM_STRUCTURE.md) — six-team org, RACI, hiring sequence, on-call
- [`docs/ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md) — quality bar, perf budgets, security, observability
- [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md) — upstream merge process, RFCs, maintainer roles
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan
- [`docs/adr/`](docs/adr/) — Architecture Decision Records (template + first three: C ABI, Tauri, BYOK)

Claude skills (`.claude/skills/`):

- `developing-souxmar-plugin` — plugin SDK walkthrough
- `reviewing-abi-changes` — ABI / agent-tool contract gating
- `writing-souxmar-rfc` — Tier-3 RFC process
- `benchmarking-souxmar` — perf budgets and regression triage
- `releasing-souxmar` — release flow with signing
- `adding-agent-tool` — agent tool surface
- `reviewing-ui-changes` — design system enforcement
- `validating-solver` — analytical / patch / convergence / cross-solver validation
- `auditing-determinism` — cross-platform reproducibility
- `triaging-plugin-crash` — `SOUXMAR_E_PLUGIN_FAULT` triage
- `auditing-mesh-quality` — pre-solve mesh quality audit
- `onboarding-souxmar-contributor` — first PR walkthrough
- `updating-design-tokens` — token contract + visual regression
- `publishing-plugin-marketplace` — open index and paid marketplace

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
