# ADR-0009: OpenFOAM process isolation

- **Status:** Accepted (Sprint 7 push 5). Sprint 8 push 1 lands the implementation.
- **Date:** 2026-05-11
- **Author:** souxmar core team
- **Deciders:** core, adapters, plugin-host, platform, founders (legal sign-off)
- **Tier:** 3 (heavy / requires RFC)
- **Affects:** plugin SDK, governance, release process, the Sprint 8 OpenFOAM adapter
- **Closes:** Risk R-003 ("OpenFOAM is GPL — adapter must be process-isolated to avoid taint")

## Context

OpenFOAM is the de-facto open-source CFD engine. The Sprint 8 plan
commits to shipping a `solver.cfd.openfoam` plugin — a pipe-bend
example, a `interFoam`-driven free-surface example, and the eventual
public-beta demos. None of that is optional: souxmar's stated value
prop ("unify CAD + meshing + FEM + CFD under a stable plugin ABI")
falls over without CFD.

OpenFOAM is licensed under **GPL v3**. souxmar is licensed under
**Apache 2.0**. The Free Software Foundation's longstanding
interpretation — affirmed by OpenFOAM's own FAQ — is that **linking
GPL'd code into a non-GPL host creates a derivative work that the
host's license must accommodate**. souxmar cannot accommodate it: the
project's whole reason for picking Apache 2.0 was permissive use by
commercial integrators (the BUSINESS_MODEL.md tier list assumes
that). Re-licensing the open core to GPL is not on the table.

The canonical workaround for this exact situation — used by Salome,
SimScale, paraFoam itself, half the CFD-adjacent toolchain — is
**process isolation**: the host invokes the GPL'd binary as a child
process via `exec(2)`, communicates with it through files on disk
(or pipes carrying non-GPL data), and never links a single GPL'd
symbol into its own address space.

The FSF's GPL FAQ confirms: "If a program is just a separate
process, with no linking, then the two are separate works."
OpenFOAM's own licensing FAQ confirms: "You can use OpenFOAM as a
standalone solver invoked by other code, including proprietary code,
provided you do not statically or dynamically link our libraries."

This ADR locks in **subprocess-only invocation** as the architectural
contract for every OpenFOAM-touching component of souxmar, forever.

## Decision

### 1. Subprocess only — no linking, no embedding

The `solver.cfd.openfoam` plugin (Sprint 8 push 1) **never** links
against `libOpenFOAM.so`, `libfiniteVolume.so`, or any other OpenFOAM
library. Not statically, not dynamically, not via `dlopen`. The
plugin's binary itself contains zero GPL'd object code.

The plugin invokes the OpenFOAM tooling via `exec(3)`-family calls
to the platform-installed `simpleFoam` / `pimpleFoam` / `interFoam` /
`foamToVTK` binaries.

The CMake build gate is **`SOUXMAR_WITH_OPENFOAM=ON`** plus a
`find_program(OPENFOAM_SIMPLEFOAM simpleFoam REQUIRED)` check — not
`find_package`. We resolve binaries on `$PATH`, never headers + libs.

### 2. File-system IPC

The adapter's wire format is **plain files on disk**, written into a
per-run working directory:

```
<work_dir>/
  constant/
    polyMesh/             # mesh, in OpenFOAM's native PolyMesh form
      points
      faces
      owner
      neighbour
      boundary
    transportProperties   # generated from souxmar materials
  system/
    controlDict           # generated from solver options (steady/transient, time bounds)
    fvSchemes             # adapter ships per-solver presets; user-overridable
    fvSolution
  0/
    U                     # generated from souxmar BCs (velocity field IC + BCs)
    p
    ...                   # one file per solved field
```

The adapter writes these files, `exec`s the OpenFOAM solver, blocks
on `waitpid` for completion, then reads back via `foamToVTK` (which
emits a directory of `.vtu` files) or by parsing the time-step
directories OpenFOAM writes into `<work_dir>/<time>/`.

Nothing in the wire format carries any OpenFOAM data type across the
linker boundary. The files are formatted text (the canonical
OpenFOAM dictionary format) plus binary scalars. The adapter knows
how to write them; OpenFOAM knows how to read them; the souxmar
process never instantiates an OpenFOAM C++ object.

### 3. Standard `solver.*` C ABI on the souxmar side

To the host, the OpenFOAM plugin is just another `solver.*` capability:

```c
souxmar_status_t openfoam_solve(
    const souxmar_mesh_t*           mesh,
    const souxmar_value_t*          inputs,
    const souxmar_solver_options_t* options,
    souxmar_field_t**               out_field,
    void*                           user_data);
```

The plugin's implementation of that vtable function:

1. Writes the case directory from `mesh` + `inputs` (BCs / materials /
   solver name / time bounds).
2. Spawns the OpenFOAM binary; collects stdout/stderr into the souxmar
   audit log (Sprint 5 push 2 surface).
3. On clean exit, parses the result files into a souxmar `Field` and
   transfers ownership to the host.

The host's call site is identical to a call into the FEniCSx adapter
or the in-tree heat solver. **The `solver.*` C ABI itself remains
unchanged**; the GPL boundary lives entirely below it.

### 4. Crash + timeout isolation

The subprocess boundary gives us crash isolation **for free**: a
segfault in `simpleFoam` is a child-process exit, not a host crash.
The adapter:

- Translates non-zero exit codes into `SOUXMAR_E_PLUGIN_REJECTED`.
- Captures the OpenFOAM `log` file content into the audit-log record
  so the agent (and the user) sees why a run failed.
- Enforces a per-run wall-clock timeout via `alarm(2)` + `SIGKILL`
  on the child group, defaulting to whatever the pipeline stage
  specifies (and rejecting unbounded runs by default — see the
  pre-mortem below).

### 5. Manifest + threading model

The plugin's `souxmar-plugin.toml` declares:

```toml
[plugin]
id = "dev.souxmar.cfd.openfoam"
license = "Apache-2.0"   # OUR plugin's license, NOT OpenFOAM's
description = "Invokes OpenFOAM as a subprocess. OpenFOAM (GPLv3) must be installed separately."

[plugin.threading]
model = "single-threaded"

[plugin.capabilities]
provides = ["solver.cfd.openfoam.simple", "solver.cfd.openfoam.pimple", "solver.cfd.openfoam.inter"]
```

`single-threaded` is conservative — two pipeline stages targeting
the same plugin can't fork OpenFOAM concurrently against the same
working directory. The reentrancy guard from Sprint 4 push 2
enforces this; parallel CFD runs across **different** work_dirs are
supported by running them through separate pipelines.

### 6. Per-release pinning

The Sprint 8 push 1 implementation will pin a specific
**OpenFOAM Foundation v12** as the supported version. The plugin's
manifest records the supported range:

```toml
[plugin.openfoam_required]
version_range = ">=11.0,<13.0"
```

The adapter probes via `simpleFoam -version`. Refusing to run
against an unsupported version is preferable to running with
silently-broken behaviour. ESI's `foam-extend` fork is **not**
supported in v1; its dictionary semantics drift in subtle ways that
would force adapter-internal branching we don't want to debug
remotely.

## Alternatives considered

### Link against `libOpenFOAM.so` like an in-process FEM solver

Pro: simplest from a code-architecture perspective; mirrors the
FEniCSx adapter (which uses DOLFINx as a linked library). Con:
**incompatible with souxmar's Apache 2.0 license.** The GPL viral
clause attaches to every byte of host code that runs in the same
process as the GPL'd library — the entire souxmar runtime would
need to be GPL'd, transitively re-licensing every dependent
project that consumes souxmar as a library. This is not a
defensible reading of "we're an open-core CAE platform with paid
commercial tiers." Rejected on legal grounds before any technical
discussion.

### Wrap OpenFOAM in a thin GPL-licensed shim that souxmar `dlopen`s

The shim links OpenFOAM, exposes a souxmar-shaped C API.
Hypothetically, if the shim is GPL, the rest of souxmar stays
Apache — only the shim gets the viral attachment.

Pro: avoids subprocess overhead. Con: still creates a GPL'd binary
souxmar ships and distributes, which raises packaging and
distribution questions (the souxmar installer would need a "GPL
component included" indicator; commercial Pro tier customers would
ask whether their use is encumbered). The FSF's interpretation
*allows* this configuration but the legal advice we paid for
recommended against it on the grounds of "minimise the GPL
surface; the subprocess boundary is settled law, dlopen-shim is
not." We listened.

### Re-implement OpenFOAM-equivalent CFD inside souxmar

Pro: no GPL, no subprocess overhead, no version pinning headaches.
Con: 30+ years of OpenFOAM development and validation — the
project would be re-doing what an entire research community
already did. Out of scope for a v1.0 commitment; possibly re-visited
in the long-term roadmap (post-1.0) if and when a commercial
sponsor underwrites it.

### Support only `foam-extend` (BSD-style fork)

Pro: would lift the licensing constraint entirely. Con: foam-extend's
user community is roughly an order of magnitude smaller than
OpenFOAM Foundation's; the validation case library is more limited;
many widely-cited research papers don't reproduce on foam-extend.
We adopt the canonical fork because that's where the users are. The
adapter is structured so a future `foam-extend` adapter is a
sibling plugin, not a fork of the OpenFOAM one.

### Skip CFD entirely; ship souxmar as FEM-only

Pro: avoids this entire conversation. Con: the project's stated
scope (`docs/VISION.md`) explicitly names CFD as a Phase 0
deliverable; the cantilever / pipe-bend / thermal-fin example
sequence is what the launch demos hang on. Cutting CFD is a
business-defining decision, not a technical one. Rejected.

## Consequences

### Positive

- **License hygiene.** souxmar's Apache 2.0 surface remains pristine.
  No GPL'd bytes land in the souxmar binary; no commercial integrator
  needs a "is your CFD path GPL-tainted?" call with their counsel.
- **Crash isolation by construction.** A solver segfault is a
  child-process exit. The host never crashes; the adapter routes the
  failure through the standard `solver.*` error path. The plugin host's
  in-process `guard_call` (Sprint 2) doesn't even need to engage on
  this code path — the OS process boundary is the guard.
- **Versioning is cheap.** Different runs can target different
  OpenFOAM versions by pointing `$PATH` at different installs. The
  adapter does not link to any specific version's headers.
- **Operations is easier.** OpenFOAM crashes / hangs / runs out of
  memory live in their own process; `kill` the child, the host is
  fine. Nightly CFD-bearing CI can run with aggressive timeouts.

### Negative

- **Performance ceiling on small jobs.** Process startup is on the
  order of tens of milliseconds — meaningless for a 10-minute pipe-bend
  CFD run, dominant for a 5-second toy case. We document this in the
  plugin's README and route the agent eval suite's "tiny CFD" tasks
  toward the in-tree analytical stub (Sprint 8 push 2 deliverable)
  rather than OpenFOAM proper.
- **Sub-process boundary serialisation cost.** The adapter writes
  the mesh to OpenFOAM's PolyMesh format, OpenFOAM writes the result
  to its own time-directory format, the adapter reads it back. For
  100M-cell meshes this is non-trivial I/O — but it's the cost we
  pay for the licensing posture, and the bulk-buffer ABI (ADR-0006)
  + mmap backing (Sprint 7 push 3) is exactly the surface that makes
  it tractable.
- **No streaming.** A real-time chat-driven "show me the velocity
  field at t=5s" interaction goes through a complete OpenFOAM
  step + file write + adapter read. Future v2 work might add a
  pipe-based streaming protocol; v1 doesn't need it.

### Risks

- **Risk:** OpenFOAM upstream breaks the dictionary file format in a
  major version. **Mitigation:** pinned per-release version range
  (manifest field); CI runs a smoke test against the supported
  versions; ESI's stable LTS branch is the project's actual production
  target for the demos.
- **Risk:** Subprocess hangs forever (no progress, stuck in a Krylov
  solve that won't converge). **Mitigation:** mandatory per-run
  wall-clock timeout; default conservative; the agent's `solve` tool
  surfaces "timed out" as `SOUXMAR_E_TIMEOUT` rather than a hard fail.
- **Risk:** A user's `$PATH` resolves `simpleFoam` to a totally
  unrelated binary called `simpleFoam`. **Mitigation:** the version
  probe (`simpleFoam -version`) acts as a sanity check; we reject
  output that doesn't look like an OpenFOAM banner.
- **Risk:** The agent invents a wildly-wrong case-file dictionary and
  OpenFOAM dumps a five-page error to stderr that overwhelms the
  audit log. **Mitigation:** truncate the captured stderr at 64 KiB
  in the audit record; the full `log` file stays on disk in the
  work_dir for forensic use.

## Pre-mortem (one year from today)

It is 2027-05-11 and the OpenFOAM adapter has gone badly. The most
likely failure mode: a non-trivial fraction of agent CFD tasks fail
because the agent doesn't author a valid `fvSchemes` / `fvSolution`
pair for the problem at hand — case authoring is genuinely hard,
and 12B-parameter LLMs without targeted training data make
plausible-but-wrong dictionaries. The adapter rejects them clean,
the user gets a confused-looking error, the chat round-trip wastes
budget.

Leading indicators we should watch:

- **Agent eval failure rate on the CFD bucket** (Sprint 8 push 2
  adds these tasks to the catalogue). If > 30% fail consistently
  on case authoring, the agent surface is the problem, not the
  adapter.
- **Filed bugs about "OpenFOAM didn't run" with stderr buried in
  the audit log.** Mitigation: surface OpenFOAM's stderr in the
  chat UI's tool-card error block, not just the audit log.
- **Plugin-version drift.** If the supported version range
  (currently `>=11.0,<13.0`) stops covering the version OpenFOAM
  Foundation ships, we have to chase it.

Off-ramps if the adapter genuinely doesn't work:

- **Ship `foam-extend` as a fallback** — smaller user base, more
  permissive license, less chasing GPL boundary questions.
- **Lean on commercial CFD solvers via the plugin SDK** — let
  paying integrators bring their own STAR-CCM+ / Fluent adapter
  through the same `solver.*` interface. The adapter contract is
  vendor-agnostic by design.

## Implementation hand-off to Sprint 8 push 1

This ADR's acceptance is the precondition for the Sprint 8 push 1
implementation. That push will deliver:

1. `examples/plugins/openfoam-solver/` — the subprocess-driver plugin.
   Manifest declares `solver.cfd.openfoam.simple` + `.pimple` + `.inter`.
   Build-gated by `SOUXMAR_WITH_OPENFOAM=ON` + `find_program(simpleFoam)`.
2. `examples/pipe-bend/` — the canonical CFD example: STEP geometry →
   tetrahedral mesh → OpenFOAM case generation → `simpleFoam` solve →
   VTU writer.
3. A `tools/openfoam-shim/` C++ library inside the plugin that
   encapsulates the case-directory generation + `exec` driver. This
   library can be re-used by adapters of OpenFOAM forks (`foam-extend`)
   that want the same wire format.
4. CI: an opt-in `cfd-nightly.yml` workflow that runs against a
   Docker-shipped `opencfd/openfoam-default` image so the demo's CFD
   step actually executes on every nightly run.

## References

- ADR-0001 — C ABI for plugins.
- ADR-0007 → ADR-0008 — ABI v1 freeze (locked).
- ADR-0006 — bulk buffer protocol (the mesh-transfer mechanism the
  case-directory generator relies on).
- `docs/SPRINT_PLAN.md` § Risk register entry R-003.
- `docs/GOVERNANCE.md` § Plugin index governance — Apache-licensed
  index entries only.
- OpenFOAM Foundation licensing FAQ
  (https://openfoam.org/governance/policies/legal/).
- GNU GPL v3 FAQ on subprocess vs linking
  (https://www.gnu.org/licenses/gpl-faq.html).

## History

- 2026-05-11 (Sprint 7 push 5): proposed, accepted.
  Implementation deferred to Sprint 8 push 1; risk R-003 closes when
  that push lands.
