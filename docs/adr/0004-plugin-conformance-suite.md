# ADR-0004: Plugin conformance suite v1 + ABI freeze-candidate process

- **Status:** Accepted
- **Date:** 2026-05-11
- **Author:** souxmar core team
- **Deciders:** core, plugin-host, DX
- **Tier:** 2 (standard)
- **Affects:** ABI, plugin SDK, governance, CI

## Context

Sprint 5 commits to declaring `include/souxmar-c/` a **frozen-candidate v1**
ABI: a two-sprint soak during which only bug-fixes are permitted, after
which the headers are pinned for the entire 1.x release line. The freeze
needs a gate — something we can run against any plugin (in-tree or
third-party) that confirms the plugin actually honors the v1 contract.

Pre-Sprint-5 we relied on two ad-hoc signals: the integration test
`test_pipeline_end_to_end.cpp` (which proves _one_ pipeline runs), and
the assertion that the registry's vtable validation rejects bad input.
Neither is a contract document. A third-party plugin author has no way
to know if their build conforms beyond "we ran the dev preset and it
seemed to work."

We also can't ship the ABI freeze without a regression alarm. Once the
headers are pinned, _any_ change that breaks a passing plugin is a
backward-compatibility break — discovering it via a customer issue is
the wrong place to find out.

## Decision

We ship a **plugin conformance suite v1** as both a library entry point
(`souxmar::plugin::run_conformance`) and a runnable binary
(`souxmar-conformance <search-dir>`). The suite contains 10 checks
(C001–C010) covering the manifest, the load chain, the
manifest↔registry mapping, the threading-model contract, and the
load/unload cycle.

The same suite is the **freeze gate**: when `tests/integration/
test_conformance.cpp` is green for every in-tree plugin across three
consecutive nightly runs, the souxmar-c headers are marked
"frozen-candidate v1". The two-sprint soak begins on that date.

### The 10 v1 checks

| ID    | Contract                                                              |
| ----- | --------------------------------------------------------------------- |
| C001  | Manifest's `abi` field matches `SOUXMAR_ABI_VERSION_MAJOR`            |
| C002  | Manifest's `binary` file resolves to an existing path on disk         |
| C003  | Plugin binary loads (dlopen / LoadLibraryExW succeeds)                |
| C004  | `souxmar_plugin_register_v1` is exported                              |
| C005  | Registration returns success                                          |
| C006  | Every capability announced in the manifest is registered              |
| C007  | No unannounced capabilities are registered                            |
| C008  | Each registered capability's threading model matches the manifest    |
| C009  | Plugin unload removes every capability owned by this plugin           |
| C010  | Three load/unload cycles leave the registry at the same baseline      |

Each check has a stable id. CIs and dashboards pivot on the id; the
human-readable description is informational.

### Ratchet policy

New checks may join the suite without bumping the suite version **iff**
they cannot make a previously-passing plugin fail. In practice this
means: new checks either tighten an underspecified contract (where the
old behaviour was a bug) or detect a violation that any sane plugin
would already avoid. Anything stricter than that waits for a future
versioned suite — the next major version is `C100+`.

## Alternatives considered

### Make the integration tests the contract

Pro: zero new infrastructure. Con: integration tests verify _one_
pipeline runs; they don't enumerate the contract a plugin must honor.
A third-party plugin author would have to read host source to figure
out what "passing" means.

### Run conformance only inside CI; no binary

Pro: shorter Sprint 5. Con: plugin authors can't self-check before
publishing. A standalone binary they can drop into their CI is the
single biggest delta between "platform with a stable ABI" and "platform
that says it has a stable ABI."

### JSON-schema-only conformance (no execution)

Pro: deterministic, no plugin process model needed. Con: half the
contract is _execution semantics_ (the threading model, the load/unload
cycle). A schema check can't catch C008–C010.

## Consequences

### Positive

- Plugin authors get a single `souxmar-conformance <dir>` invocation
  that's part of their PR template.
- The freeze gate is mechanical: green for three nights ⇒ ABI marked
  frozen-candidate. No human judgement on "is this stable enough."
- Adding a check is a small PR; removing one is an ADR. The ratchet is
  one-way, deliberately.

### Negative

- A bug in the conformance runner itself can block a release (false
  failure). Mitigation: the runner is unit-tested alongside the
  in-tree plugins, and the ratchet policy forbids checks that could
  regress a previously-passing plugin.
- Plugin authors targeting the unstable head will see new checks
  appear. Mitigation: every new check ships with a CHANGELOG entry
  describing the contract it enforces.

### Risks

- **Risk:** A new in-tree plugin (e.g. the Gmsh adapter in Sprint 6)
  fails a check on day one, blocking the merge. **Mitigation:** Run
  the suite in the plugin's own PR, not just in the gate test. CI's
  per-PR conformance run catches this before main.
- **Risk:** We add a check during the freeze period that breaks a
  third-party plugin. **Mitigation:** Ratchet policy forbids this; if
  caught, the check is reverted to the next major version.

## References

- `include/souxmar/plugin/conformance.h` — public surface.
- `src/plugin-host/conformance.cpp` — check implementations.
- `tools/conformance/main.cpp` — `souxmar-conformance` binary.
- `tests/integration/test_conformance.cpp` — gate test.
- `docs/PLUGIN_SDK.md` § Conformance — author-facing documentation.
- ADR-0001 — C ABI for plugins (the contract this suite enforces).

## History

- 2026-05-11: Proposed, accepted (Sprint 5 push 1).
