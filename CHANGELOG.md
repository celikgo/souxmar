# Changelog

All notable changes to souxmar are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

`v0.9.0` is the project's **first release**. The version history that used to
precede it in this file was a development log, not a release history — no tag
or artefact ever existed for any of those versions. It is preserved verbatim
in [`docs/HISTORY.md`](docs/HISTORY.md).

The plugin C ABI version is tracked separately and is independent of the
project version. **ABI v1 is frozen final**, currently at minor **v1.9**; see
[ADR-0008](docs/adr/0008-abi-v1-final-freeze.md) for the freeze,
[ADR-0012](docs/adr/0012-per-face-tag-c-abi-ratchet.md) for the first minor
ratchet and [ADR-0042](docs/adr/0042-abi-v1-9-timeseries-ratchet.md) for the
most recent one. The **agent tool contract v1 is frozen final**
([ADR-0011](docs/adr/0011-tool-contract-v1-final-freeze.md)); the default
catalogue stands at **24 tools**, most recently
[ADR-0045](docs/adr/0045-agent-tool-contract-am-ratchet.md).

## [Unreleased]

### Added

- **`solver.elasticity.fem` — the first real solve in the default build.**
  Small-strain linear isotropic elasticity by isoparametric FEM: Tet4
  (one-point) and Hex8 (2x2x2 Gauss) elements, assembled into a CSR stiffness
  matrix, Dirichlet conditions applied by symmetric elimination, consistent
  Neumann surface tractions, and a Jacobi-preconditioned conjugate-gradient
  solve. No external dependency and always-on, so all four engine legs and the
  cross-platform determinism gate exercise it — unlike `solver.heat.fenicsx`,
  the only other discretised solver in the tree, which is behind a build flag
  no workflow sets. It passes the constant-strain patch test *exactly* on both
  element types, and reproduces `solver.elasticity.linear`'s closed form to
  6e-14 relative, closing the loop that plugin's own header asks for.
  `docs/PHYSICS.md`'s headline claim that nothing in the repository
  discretises anything has been corrected accordingly. Shear locking,
  volumetric locking as ν → 0.5, and the absence of stress output are
  documented in `docs/CAPABILITIES.md` and the plugin header. Determinism
  rests on the solver calling no libm function at all — every operation is a
  correctly-rounded IEEE-754 add, multiply or divide, the residual test
  compares squared norms so not even `sqrt` is needed, and the target is built
  with `-ffp-contract=off` so an FMA cannot fuse on arm64 and not on x86-64.
- **`examples/fem-cantilever/`** — the first example that solves something,
  and the fifth of ten to produce a determinism fingerprint rather than
  `EXIT-70`. The gate's `--min-hashed` floor ratchets 4 → 5 with it.

- **`security-ok`, the second required status check.** `needs:` cannot cross
  workflows, so `ci-ok` structurally cannot cover CodeQL, the SCA scans,
  dependency review or the licence inventory — while `docs/CI.md` marked four
  of them "Blocking: Yes" *and* said to require exactly one check, which
  cannot both be true. `security.yml` also gains the `merge_group:` trigger
  `ci.yml` already had, without which a required check never reports inside a
  merge queue and blocks it rather than gating it. `docs/CI.md` now carries
  the exact `gh api` payload, and states plainly that **no branch protection
  is enabled today**, so every gate in this repository is currently advisory.
  `.github/CODEOWNERS` no longer claims to gate merges; it assigns reviewers.

- **`scripts/check-live-references.py`**, wired into `ci.yml`. The existing
  `check-doc-links.py` is offline and cannot see two defects this repository
  has shipped: an install command for a package that is not on the registry,
  and a documentation site that stopped deploying. This one resolves every
  documented `pip`/`npm`/`cargo install` of a first-party package against its
  registry, and every promised URL over the network. An unpublished package
  may still be *named* — saying `pip install pysouxmar` does not work is the
  honest thing to write — but only in prose that says so, or behind an
  explicit `<!-- unpublished-ok: NAME -->` marker; inside a fenced code block
  it is always an error. It also asserts that the README links the docs site
  and that the repository `homepage` field points at it. A definite 404 fails
  the build; an unreachable network warns and skips (`--strict` to fail).

### Fixed

- **Five of the ten shipped examples could not run, and it was one bug.**
  `am-marine-propeller`, `am-polymer-auv-fairing`,
  `am-submarine-pressure-hull`, `pipe-bend` and `stl-cube` each name an input
  file relatively (`path: cube.stl`), and the engine resolved that against the
  process working directory rather than against the pipeline file. Every one
  of those files sits beside its own `pipeline.yaml`, so each example worked
  only after `cd`-ing into its directory — while the repo-root invocation that
  `docs/MARINE.md` and `examples/pipe-bend/README.md` document could never
  work. The determinism gate runs each pipeline from a scratch directory,
  which is why all five fingerprinted as `EXIT-70` on all three platforms.

  A **reader's** relative `path` now resolves against the directory its
  pipeline file came from. A **writer's** does not, and the asymmetry is the
  point: rebasing output paths would write build products into the source tree
  on every run, would defeat the determinism harness's scratch-cwd isolation,
  and would falsify the `export_results` agent tool's frozen documented
  behaviour ("relative resolves to CWD"). Absolute paths are untouched.
  Resolution happens at dispatch, not at parse — `runner.cpp:62` hashes the
  stage input tree for the content-addressed cache *before* dispatch and the
  CLI prints that hash, so rewriting the value earlier would have embedded a
  machine-specific absolute path in every fingerprint and turned five
  obviously-broken examples into ten mysteriously non-deterministic ones.
  Verified: the same pipeline copied to three different absolute locations
  produces byte-identical stage hashes.

  The determinism corpus goes from **5 of 10 hashing to 10 of 10**, and the
  gate switches from `--min-hashed <n>` to a new `--require-all`, which
  asserts every pipeline hashes. A numeric floor could not notice an
  *eleventh* example landing broken — the same blind spot that let these five
  survive. The old floor's committed comment blamed `reader.step` and
  OpenCASCADE; no example in the corpus uses either, and that misattribution
  is plausibly why five one-line bugs read as an unfixable dependency problem.

  `tests/integration/test_cli_smoke.cpp` gains a corpus-wide regression test
  that runs every `examples/*/pipeline.yaml` from a foreign working directory.
  It fails on exactly those five before the fix. Nothing caught this before
  because every reader test builds its YAML with an absolute path interpolated
  in, making the relative-path branch unreachable from any test, and the one
  test that ran a real example copied it into the working directory first —
  collapsing the very distinction the bug lived in.

- **`writer.vtu` discarded every field.** The writer vtable's `field`
  parameter was unnamed and never read, so no solver's results ever reached a
  `.vtu` file, and `ROADMAP.md` Phase 2's definition of done — "a `.vtu` that
  opens in ParaView and shows a recognisably correct stress field" — was
  unreachable for that reason alone, independently of whether any solver
  computed one. Nodal fields now emit as `<PointData>` and cell fields as
  `<CellData>`, one `<DataArray>` per time step, at `max_digits10` precision
  so a solved value survives the round trip through text. Face and
  Gauss-point fields are still dropped, deliberately: neither has a VTU
  equivalent, and interpolating them here would invent numbers the solver
  never produced.
- **`mesher.tetra.grid` emitted a hex decomposition that did not tile the
  hex.** Its five-tetrahedron table had three inverted tets and one degenerate
  one — corners 0, 2, 5 and 7 of the unit cube are coplanar, because (1,1,1)
  is exactly (0,1,0) + (1,0,1) — with signed volumes summing to −2/6 instead
  of +6/6. Replaced with Kuhn's six-tet subdivision: positively oriented,
  tiles exactly, and conforming between neighbouring hexes without the
  checkerboard parity rule a correct five-tet split would need. Nothing caught
  this because until `solver.elasticity.fem` no consumer in the tree formed a
  Jacobian; `test_swap_mesher.cpp` now asserts per-cell positive volume and
  total volume against the bounding box, which a cell-count assertion cannot
  see.
- **The `Benchmarks (advisory)` job has never compiled, so the perf
  comparison it exists for has never run once.** One
  `-Werror=useless-cast` (`bench_mesh_construction.cpp:48`, which fires only
  on Linux, where `size_t` *is* `uint64_t`) and five
  `-Werror=deprecated-declarations` from google-benchmark's const-ref
  `DoNotOptimize` overloads, across four files — two of which CI had never
  reached, because ninja stopped at the first. Fixed in the sources; no
  warning was silenced, and the const-ref deprecation is real (the barrier
  lets the optimiser elide the work being measured). `compare.py` no longer
  returns 0 when *nothing* was compared: the "new benchmark, no baseline yet"
  escape hatch is kept, but a non-empty baseline directory with zero current
  reports is now exit 2. The job stays advisory — the committed baselines
  were recorded on reference hardware that `gh api .../actions/runners` shows
  has never existed.
- **The synthetic-load harness could not run a single target.** A relative
  `--engine` stopped resolving once the example loop `pushd`'d into its
  scratch directory (`rc=127`); `souxmar-eval` was handed a single YAML where
  it requires a directory plus `--only <id>` (exit 2); `--plugin-path` was
  given leaf plugin directories where discovery scans a search path's
  *immediate subdirectories*, so zero plugins loaded; and the eval leg passed
  no plugin path at all. Two further defects would have surfaced the moment
  the corpus was seeded: `corpus_lookup`'s awk called `exit` without clearing
  its state, so the END rule fired again and every fingerprint came back
  twice, and neither fingerprint source was deterministic — `souxmar-eval`
  prints an unnormalised wall-clock latency block, and `souxmar run` prints
  `[CACHED  ]` instead of `[OK      ]` on a machine with a warm disk cache.
  All four targets now produce stable, byte-identical fingerprints across
  repeat runs, which is the state `docs/INFRA_STATUS.md`'s bootstrap PR needs
  and has never had.
- **Three visual-regression specs selected UI the app stopped rendering
  months ago.** The earlier diagnosis that the app renders nothing was wrong:
  the Vite build succeeds and Playwright drives a fully-rendered React app.
  A renamed BYOK heading, a redesigned workbench empty state and a
  `chat_send` mock still returning a bare string where `Chat.tsx` has
  expected a typed `ChatSummary` since Sprint 14. The mock table is extended
  to the commands the visual surfaces actually reach and typed against
  `bridge.ts`, so the next contract change breaks at `tsc` time rather than
  as a missing element. `docs/INFRA_STATUS.md`'s bootstrap precondition is
  rewritten: it waited for a *green* run, and a baseline-less Playwright
  suite is red by definition on its first one.
- **The ABI-freeze gate had drifted past five of the twenty frozen headers.**
  `check-frozen-headers.sh` carried a hand-maintained list of 15 paths;
  `brep.h`, `field_stream.h`, `sketch.h`, `surface_stream.h` and
  `timeseries.h` could be edited with no ratchet marker, so the gate answered
  "no v1 ABI surface touched" for a real v1 ABI change — worse than no gate,
  because it is a positive assertion of safety. The list is now derived from
  the tree.
- **`ci-ok` could report green with zero compilation.** `skipped` counts as a
  pass, and the `changes` paths-filter job — whose failure skips every
  downstream job — was not in its `needs`. Added. The filter also had no
  `scripts/**` entry, so a PR editing a CI-critical script skipped every job
  that runs it, including the determinism gate's own fingerprint script.
- **The nightly had no gate at all.** Its `report` job was `if: always()` and
  wrote a markdown table, so the workflow's conclusion was decoupled from its
  jobs: a sanitizer could report a use-after-free and the run still finished
  green. It now runs the same `NEEDS_JSON` check `ci-ok` does, exempting
  `synth-load`, `agent-eval-llm` and `fuzz` **by name** with a stated end
  condition for each — never by reading `continue-on-error:`, which means
  "do not fail the run" and is exactly the decision this step exists to make.
  The `fuzz` job's crash-artifact upload was also unreachable: `|| true` on
  the run step meant `if: failure()` could never fire, so a libFuzzer
  reproducer would have been thrown away with the runner.
- **The determinism gate was vacuous on more than half its corpus.** Five of
  nine example pipelines fingerprinted as `EXIT-70` on all three platforms
  while the gate announced "3 platforms agree on every pipeline". Both counts
  are now reported and `--min-hashed` makes the floor a committed ratchet.

- `docs/RELEASE_NOTES_TEMPLATE.md` no longer carries a copyable
  `pip install pysouxmar==<version>` line, which 404s — it relied on a comment
  asking the release manager to delete it. The gate now enforces this.
- The README now links <https://celikgo.github.io/souxmar/>; the docs site was
  live but unreachable from the repository's front page.

### Changed

- **Linux CI runners move to `ubuntu-24.04`, and nothing in this repository
  talks to Launchpad any more.** `gcc-13` is not in jammy's archive at all, so
  `add-apt-repository -y ppa:ubuntu-toolchain-r/test` was not a fallback — it
  was the sole source of the project's compiler, on the critical path of every
  Linux job in every workflow, reached fresh on each job with no cache. On
  2026-08-24 Launchpad's REST API answered HTTP 500
  (`GPGKeyTemporarilyNotFoundError`) and took out the nightly run. ubuntu-24.04
  preinstalls GCC 13.3.0 and Clang 17.0.6 — exactly the versions the presets
  pin — so the toolchain step now touches the network zero times, three
  `libstdc++6`-upgrade workarounds in `ci.yml` are deleted, and the migration
  lands ahead of the 2026-09-17 start of the ubuntu-22.04 deprecation.

  **This changes the published Linux artefact's glibc floor from 2.35 to
  2.39.** The practical support floor does not move: `souxmar-0.9.0-linux-x64`
  already required `GLIBCXX_3.4.32` (GCC 13.2's libstdc++, which jammy does not
  ship), so it could not run on Ubuntu 22.04 — the platform it was built on —
  and `docs/DESKTOP_APP.md` advertised a floor the binary never met. That row
  now says Ubuntu 24.04 and records why. Restoring 22.04 support means
  statically linking libstdc++ and libgcc into the release artefact, which
  changes how plugins share a C++ runtime with the host and has not been done.
  RHEL 9 (glibc 2.34) is the one platform this genuinely drops.

- **The nightly sanitizers build with the compiler they were meant to use.**
  `CMakePresets.json`'s `asan` and `tsan` presets inherit only `base` and pin
  no compiler, so on a Linux runner CMake picked `/usr/bin/c++` = GCC 11.4.0
  while every other Linux job used gcc-13. GCC 11's `-Wuseless-cast` fires on
  three portability casts GCC 13's does not — `pipeline/cache.h:145`,
  `pipeline/cache.cpp:66`, `pipeline/registry_dispatcher.cpp:478`, all of them
  narrowing or widening between `size_t` and `uint64_t`, which is a no-op only
  on LP64 — and `-Werror` made that the reason the nightly was red every night
  for months. The casts are correct; the presets were wrong. New
  `ci-linux-asan` / `ci-linux-tsan` presets pin gcc-13 and carry a
  `hostSystemName == Linux` condition, leaving the local `asan`/`tsan` presets
  portable for macOS and Windows contributors. Note that compiling is not
  passing: `ctest --preset ci-linux-asan` has never executed once in CI
  history.

- **`VERSION` is now literally the single source of truth.** The desktop app
  (`0.9.0-beta3`), its Tauri config, the `souxmar-bridge` crate (`0.9.1-dev`)
  and the Python bindings (`0.0.1`) each stated a different version from the
  `0.9.0` in `VERSION`; all now state `VERSION`, lockfiles included.
  `scripts/check-version-consistency.py` was widened to cover them and no
  longer holds a hand-maintained list: it discovers every version-bearing
  manifest tracked by git and **fails on one it has not been told about**, so
  a new package must be declared either as shipping with souxmar (and carry
  `VERSION`) or as independently versioned (with the reason). `release.yml`
  now runs the same gate instead of its own narrower inline tag check.

## [0.9.0] - 2026-08-19

**First tagged release.** Everything in this section is the state of the tree
at the tag, not a diff against a predecessor — there is no predecessor.

**Tag:** `v0.9.0`. **Plugin C ABI:** v1 major frozen, minor v1.9. **Agent tool
contract:** v1 frozen, 24 tools. **Licence:** Apache-2.0. **CI:** the `CI`,
`Security` and `Docs site` workflows were green on `master` at the commit this
tag points at.

**What the download gives you.** Unsigned CLI tarballs for linux-x64,
macos-arm64 and windows-x64, containing `souxmar` and the static libraries.
They contain **no plugins** and not the plugin SDK headers, and every
capability souxmar has lives in a plugin — so a downloaded build can print its
version and its help but cannot run a pipeline. Build from source for anything
real. Nothing is signed: verify the SHA-256 against the `SHA256SUMS-*.txt`
beside each artefact and the release's build-provenance attestation, which
establish which workflow run produced the bytes, not that the project signed
them.

### What is in it

- **A stable C plugin ABI** (`include/souxmar-c/`) with a frozen v1 major,
  a plugin host with fault isolation and heap accounting, and a conformance
  suite. 25 in-tree plugins provide 38 capabilities across meshing, reading,
  solving, post-processing and writing.
- **A pipeline runner** — YAML pipelines with a DAG scheduler, a content
  cache, typed `--json` CLI output, and a determinism gate that holds output
  byte-identical across Linux, macOS and Windows.
- **An agentic AI layer** with a frozen 24-tool contract, BYOK credentials
  (Anthropic / OpenAI / local Ollama), a session budget, an audit log, and a
  scripted eval suite under `evals/v1/`.
- **A Tauri + React desktop app** — chat panel, pipeline editor, inspector,
  and a viewport panel whose renderer is not yet implemented.
- **Additive-manufacturing and marine physics** — eight always-on plugins
  providing 18 capabilities: layered build meshing, parametric lattices, LPBF
  thermal history and melt pool, inherent-strain distortion and residual
  stress, FFF interlayer bonding, DfAM overhang/printability/build-cost,
  slicing to G-code, CLI and Markdown build reports, and
  hydrostatic/hull-collapse/corrosion/qualification-dossier for marine parts.
  **Every one is a closed-form or heuristic model with a cited literature
  source** — screening and preliminary-sizing aids, not calibrated process
  simulations. See [`docs/PHYSICS.md`](docs/PHYSICS.md).
- **740 gtest cases across 67 files**, ~66,800 tracked lines of C/C++, 45 ADRs
  and 9 RFCs.

### Known limitations at this tag

These are stated in full in [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md),
which is generated from the tree:

- No CAD kernel. `brep.h` / `sketch.h` are ABI surface; the in-core backing
  returns `NOT_IMPLEMENTED` and no `cad.*` plugin exists.
- `solver.heat.linear`, `solver.elasticity.linear`, `solver.modal.linear` and
  `solver.cfd.simple` are closed-form demonstration stubs, not FEM or CFD.
  The one real discretised solve, `solver.heat.fenicsx`, is opt-in.
- The desktop viewport does not render.
- The Pro-tier services under `services/` are API scaffolds with nothing
  deployed. Every `*.souxmar.invalid` hostname is a placeholder on the
  RFC 6761 reserved TLD.
- `pysouxmar` is not published to PyPI. Build it from source with the
  `dev-python` preset.

### Added in the run-up to this release

- **Manufacturing + marine capability block — eight always-on in-tree
  plugins, 18 new capabilities.** Every id rides the existing prefix
  dispatch (`mesher.` / `reader.` / `solver.` / `writer.` / `postproc.`),
  so the plugin host, the pipeline runner and `include/souxmar-c/` are
  untouched: **no ABI ratchet in this block.** All eighteen are
  **closed-form or heuristic engineering models with a cited literature
  source in every source header** — they are screening and
  preliminary-sizing aids, not calibrated process simulations.
  - `am-layered-mesher` — `mesher.am.layered`: layer-aligned Hex8 build
    mesh, cell tag = layer index, outer boundary faces tagged 10–15 per
    side.
  - `lattice-reader` — `reader.lattice`: parametric strut lattice
    (`cubic` / `bcc` / `fcc` / `octet` / `diamond`) from a spec file into
    an Edge2 beam mesh, cell tag = strut family.
  - `am-thermal` — `solver.am.thermal.lpbf` + `postproc.am.melt_pool`:
    layer-wise LPBF thermal history from the Rosenthal moving point
    source; melt-pool depth, normalised enthalpy, and a
    lack-of-fusion / keyhole porosity risk score.
  - `am-distortion` — `solver.am.distortion.inherent_strain` +
    `postproc.am.residual_stress`: part-scale distortion by the
    Keller–Ploshikhin inherent-strain method with Stoney-type layer-wise
    curvature accumulation, plus a von-Mises-equivalent residual stress
    read-back. The `strain_calibration` input must be calibrated against
    a measured part before the numbers mean anything.
  - `am-polymer` — `solver.am.polymer.fff` + `postproc.am.bond_strength`:
    lumped-capacitance FFF/FDM interlayer thermal cycling and
    Yang–Pitchumani reptation healing → degree of healing, Z-strength
    fraction, seconds above Tg.
  - `am-manufacturability` — `solver.am.overhang`,
    `solver.am.printability`, `solver.am.buildtime`: DfAM downskin-tilt /
    support-need per cell, a documented composite printability heuristic
    with a limiting-factor code, and per-layer build time / energy /
    mass / cost.
  - `am-slicer` — `writer.am.gcode`, `writer.am.cli`, `writer.am.report`:
    planar slicing of the mesh boundary into FFF G-code, Common Layer
    Interface ASCII, and a Markdown build report / traveller sheet with a
    non-cryptographic FNV-1a content digest for traceability.
  - `marine` — `solver.marine.hydrostatic`, `solver.marine.hull_collapse`,
    `solver.marine.corrosion`, `writer.marine.qualification_report`:
    depth-factored seawater pressure load cases; Windenburg–Trilling
    interframe / membrane-yield / sphere-buckling collapse margin with
    imperfection and AM-anisotropy knockdowns; PREN + galvanic-series
    corrosion indicators; and an **advisory-only** AM-part qualification
    dossier. souxmar is not a classification society and nothing the
    marine plugin emits is a class calculation or an approval.
- **[ADR-0044](docs/adr/0044-manufacturing-capability-namespaces.md)** —
  manufacturing capability namespaces. Records the two structural
  constraints the block hit: `postproc.*` hard-requires an input `field`,
  so mesh-only analyses (overhang, printability, build time) register as
  `solver.*`; and meshers get no value bag, so fully-parametric geometry
  generation goes through `reader.*`. (ADR-0043 was already claimed by
  RFC-0011's CalculiX work.)
- **[ADR-0045](docs/adr/0045-agent-tool-contract-am-ratchet.md)** — the
  additive agent-tool ratchet for tools 19–24. Existing 18 tools and
  their order are untouched; the default catalogue is now **24 tools**.
  Landed with the `Ratchet: additive tool (ADR-0010)` marker.
- **Six new agent tools** in `src/ai/tools/`: `propose_am_setup`
  (Pipeline), `check_printability` (Mesh), `set_build_orientation` (BC,
  confirm-once), `estimate_build_cost` (Mesh), `apply_hydrostatic_load`
  (BC, confirm-once), `check_marine_integrity` (Field). Session state
  reuses the existing `boundary_conditions` vocabulary with a
  discriminating `type`, plus a new `manufacturing` key.
- **[RFC-0012](docs/rfcs/0012-am-process-simulation.md)** — AM
  process-simulation contract: what the closed-form models cover, what
  they deliberately do not, and the calibrated-solver path that Sprints
  41–44 pick up.
- **Two desktop workbench panels** — `ManufacturingPanel.tsx` and
  `MarinePanel.tsx` under `src/desktop/src/workbench/`, registered in
  `YamlViewer.tsx` alongside the other pipeline-editor panels, with the
  same algorithms mirrored into `scripts/sim-pipeline-flow.mjs`.
- **Four runnable examples** — `examples/am-lpbf-bracket/` (316L LPBF
  bracket: layered mesh → thermal → melt pool → distortion → residual
  stress → overhang → report), `examples/am-marine-propeller/`
  (nickel-aluminium-bronze blade: distortion → printability → corrosion →
  qualification report), `examples/am-submarine-pressure-hull/` (316L
  hull ring + lattice buoyancy core: hydrostatic → collapse margin →
  qualification report), `examples/am-polymer-auv-fairing/` (PEKK /
  PA12-CF fairing: FFF thermal → bond strength → printability → G-code +
  build report). Plus `examples/materials/am-marine.toml`, a curated
  AM + marine material library with a source column on every number.
- **Docs** — `docs/MANUFACTURING.md`, `docs/MARINE.md`, a manufacturing
  section on the docs site, and eight new `docs/plugin-index.toml`
  listings (`conformance = "not_run"` — no conformance run has happened
  for these yet).

### Changed

- (None this release.)

### Fixed

Five defects that the manufacturing block surfaced. The first three made the
plugin system unusable outside Linux, which is why they are fixed here rather
than deferred: nothing in this block could be demonstrated without them.

- **Plugin discovery rejected every in-tree plugin on macOS and Windows.**
  Every manifest declares `[plugin.binary] file = "lib<x>.so"`, but the built
  artefact is `.dylib` on macOS and `.dll` on Windows, so `discover_plugins`
  reported `binary_not_found` for all of them and `souxmar plugin list` came
  back empty. Discovery now falls back to the host platform's canonical
  extension when the declared name is absent; the declared name stays
  authoritative when it exists, and the rejection code is unchanged when
  nothing resolves. Documented in [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md).
- **Host executables did not carry the C ABI symbols plugins resolve
  against.** `souxmar_value_*` lives in `src/pipeline/c_abi_value.cpp`; no
  host translation unit referenced it, so the linker dropped that archive
  member and every plugin that reads its stage input bag failed to load with
  `symbol not found in flat namespace '_souxmar_value_as_number'`. The C-ABI
  translation units are now object libraries whose objects propagate into
  every consumer, and host executables set `ENABLE_EXPORTS`.
- **The pipeline parser ignored YAML quoting.** Any quoted scalar whose
  characters were all numeric-ish became a `Number`, so `alloy: "2507"`
  reached the plugin as `2507`, the string-typed lookup fell back to its
  default, and the corrosion model reported confident numbers for the wrong
  material (316L instead of 2507 super-duplex). Quoted scalars are now
  strings, per the YAML spec; plain scalars are sniffed exactly as before.
  Covered by two new parser unit tests.
- **`test_mesh_quality_plugin.cpp` asserted success on a stage the dispatcher
  rejects** — a `postproc.*` stage with only `mesh:` and no `field:`, which
  `registry_dispatcher.cpp` has always refused. The test now supplies an
  upstream field and says why.
- **The unit + integration suites are green for the first time on this
  toolchain: 672/672.** Once the three files that would not compile were
  fixed, 57 latent failures surfaced. Beyond the plugin-discovery and
  symbol-export fixes above, the causes were:
  - **toml++ packaged as a shared library broke every typed parse error.** In
    that mode its exception typeinfo is hidden inside the dylib, so
    `catch (const toml::parse_error&)` did not match the thrown object: the
    manifest, plugin-index, budget-config, provider-config and update-manifest
    parsers all stopped returning a typed error with a line and column, and an
    uncaught exception escaped `souxmar plugin list` on any malformed
    manifest. `cmake/SouxmarFindToml.cmake` now pins toml++ to header-only —
    its default, and what vcpkg ships — and the parse sites keep a
    `std::exception` arm so a shared-library build degrades to a typed error
    instead of terminating.
  - **Test helpers returned references into temporaries.** `expect_ok`,
    `expect_err`, `expect_apply` and `expect_refusal` returned a reference into
    a variant, so `const auto& m = expect_ok(f(...))` bound to a temporary that
    died at the end of the full expression. Tests then read freed memory —
    some passed by luck, others reported garbage strings. The helpers now
    return by value.
  - **Stale test expectations** that had drifted from the code they cover: the
    manifest fixtures used `id = "x"`, which the reverse-DNS id check rejects
    before reaching the behaviour under test; the tamper test's off-by-one
    corrupted the version into `0.910`, so the manifest failed schema
    validation and its signature was never checked; the `update apply` test
    still asserted a placeholder message from before apply was implemented;
    and the two bridge-ABI tests asserted 1 and 2 after the surface reached 3.
  - **The VTU conformance test never checked its own invariant.** It looked
    for the `<Points>` DataArray by `Name`, but that array is correctly
    unnamed in VTU, so the extractor always returned nothing and the point
    array parsed as zero floats.
- **The bridge's stub provider answered every chat message with an error.**
  It constructed a `StubProvider` with an empty reply table, and an
  unprogrammed request is a deliberate `ProtocolMismatch` — which defeated the
  reason the stub is wired into the bridge at all, namely letting the desktop
  Chat panel exercise the full path before a real provider is configured. It
  now programs a catch-all reply that says plainly what it is.
- **The scripted agent-eval suite was almost entirely broken and nothing
  noticed.** 16 of 44 tasks failed: three assertion kinds the tasks used
  (`tool_data_contains`, `error_code_contains`, `step_outcome`) were never
  implemented by the runner, a dozen tasks named input keys and result paths
  the tools do not have (`flow_regime` for `goal`, `bc_plan` for `plan`,
  `format` for `capability_id`, `issue_count`, `plugin_count`,
  `changes_applied`), and `diff-02-dangling-rejected.yaml` could not even be
  loaded because its unquoted description contains `{from: ...}`, which
  yaml-cpp reads as a flow mapping. The tasks now assert against the real tool
  contracts, `tool_data_contains` is implemented (documented in
  `evals/v1/README.md`), and the suite runs **49/49**.
- **`query_mesh_quality` could never succeed.** It dispatched
  `postproc.mesh_quality` with only a `mesh:` input, and the dispatcher
  requires every postproc stage to name an upstream `field:` — so one of the
  eighteen frozen v1 tools failed on every invocation. It now supplies a
  one-value placeholder field (which `postproc.mesh_quality` ignores, as its
  own signature shows) purely to satisfy the contract.
- **`apply_pipeline_diff` never validated its result.** It re-parsed the
  diffed pipeline but parsing does not resolve `{from: <id>}` references, so a
  `remove` that orphaned a downstream stage was returned to the caller as a
  success and only failed later inside the runner — despite the tool's own
  error string promising to diagnose exactly that. It now runs
  `pipeline::validate` and reports a dangling reference or cycle as
  `INVALID_ARGUMENT`.
- **Three test files did not compile on AppleClang 21**, so neither test
  binary could be built and no test had run on macOS: a test-local
  `clock_t` helper collided with the POSIX type, an unused helper tripped
  `-Werror=unused-function`, and `test_ai_tools.cpp` had void-expression
  errors. With those fixed the suite builds and 672 tests run.

### Removed

- (None this release.)

### Security

- (None this release.)

---


---

## Earlier history

Development before `v0.9.0` was never released. The full log — nineteen
unreleased development milestones, sprint by sprint — is preserved in
[`docs/HISTORY.md`](docs/HISTORY.md), which also explains the shape of this
repository's commit history.
