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

Nothing yet.

## [0.9.0] - 2026-08-19

**First tagged release.** Everything in this section is the state of the tree
at the tag, not a diff against a predecessor — there is no predecessor.

**Tag:** `v0.9.0`. **Plugin C ABI:** v1 major frozen, minor v1.9. **Agent tool
contract:** v1 frozen, 24 tools. **Licence:** Apache-2.0. **CI:** the `CI`,
`Security` and `Visual regression` workflows were green on `master` at the
commit this tag points at.

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
