# Engineering Practices

The contract every engineer accepts when joining souxmar. These are not aspirations; they are gates enforced by CI, CODEOWNERS, and merge policy. A PR that violates them does not merge — not because a reviewer says no, because the build refuses.

This document is the source of truth for the team's quality bar. RFCs change it.

## Definition of Done

A change is **done** only when *all* of the following are true:

1. **Code merged to `main`** through the standard review path.
2. **Tests written or updated** that exercise the change at the appropriate layer (see Test Pyramid below). Coverage gate: ≥ 85 % line, ≥ 70 % branch on changed files.
3. **Docs updated in the same PR** if the change is user-facing (public API, new tool, new plugin type, new CLI flag, new desktop UI surface).
4. **Performance gates passing** (see Performance Budgets).
5. **Security gates passing** (no new dependencies without ADR; no secret-shaped strings in code; SCA clean).
6. **Determinism gate passing** if the change touches the pipeline runner, any solver, or any mesher.
7. **Conformance suite passing** if the change touches a plugin or the plugin host.
8. **No nightly regression** in the 3 nightly CI runs after the merge.
9. **Linked to an issue or RFC.** PR descriptions without context get bounced.
10. **DCO sign-off on every commit** (`git commit -s`).

The author is responsible for hitting Done. The reviewer is responsible for confirming it.

## Code review

- **Two reviewers for Tier-2 changes** (standard); one for Tier-1 (trivial); two **maintainer** approvals for Tier-3 (heavy). See `GOVERNANCE.md`.
- **Reviewer SLA:** first comment within 1 working day; resolution within 3 working days. Slower than that and the author re-pings; slower than 5 days and it escalates to the weekly principal sync.
- **Reviewers leave specific feedback.** "I don't love this" is not feedback. "Move this to `Foo::bar` because it duplicates the lifetime check there" is.
- **Authors do not relitigate accepted feedback** in the same PR. If they disagree, they file a follow-up issue or an RFC.
- **No drive-by refactors.** A PR that adds a feature does not also rename three modules. Refactor PRs are separate, smaller, and reviewed for scope first.
- **Reviewers run the change locally** for any PR that touches a user-facing surface. Lint passes are not enough.

## Test pyramid

| Layer                      | Tooling                | Runs on             | Time budget      | What it covers                                                    |
| -------------------------- | ---------------------- | ------------------- | ---------------- | ----------------------------------------------------------------- |
| Unit                       | gtest (C++) / vitest (TS) | Every PR        | < 90 s total     | Pure functions, type invariants, single-class behaviour.          |
| Integration                | gtest fixtures         | Every PR            | < 5 min          | Multi-module flows, plugin host loading, pipeline runs.           |
| Conformance (plugin)       | `souxmar-conformance`  | Every PR + on-demand for external authors | < 3 min | Plugin ABI compliance, memory rules, threading.        |
| End-to-end (CLI)           | bash + golden files    | Every PR            | < 8 min          | Cantilever beam, thermal fin, pipe-bend CFD pipelines.            |
| End-to-end (desktop)       | Playwright + Tauri WebDriver | Every PR      | < 12 min         | App launches, viewport renders, chat completes a tool sequence.   |
| Determinism                | hash-compare across OSes | Every PR (matrix-fanout) | < end-to-end + 5 min | Same pipeline → byte-identical results on Linux/mac/Win.    |
| Performance benchmarks     | google-benchmark + custom UI bench | Every PR | < 6 min       | Tracked benchmarks must not regress > 5 %.                        |
| Agent eval                 | Custom harness         | Weekly + on agent-tool changes | < 30 min | 60-task canonical eval; pass-rate gate at 90 %.                |
| Fuzzing                    | libFuzzer / cargo-fuzz | Nightly             | 4 h budget       | Manifest parser, pipeline YAML parser, ABI surface.               |
| Sanitizers                 | ASAN / TSAN / UBSAN    | Nightly             | 30 min           | Memory and concurrency invariants on the integration suite.       |

Pre-merge CI total wall time target: **< 25 min** on a typical PR. Beyond that, developers context-switch and lose the thread.

## Performance budgets

Tracked in `benchmarks/`. Regressions > 5 % block the merge. Improvements are celebrated in the changelog. Numbers are reference-machine (M2 Pro / Ryzen 7 7700X).

| Surface                                        | Budget                              |
| ---------------------------------------------- | ----------------------------------- |
| Cold app launch → interactive                  | < 1.5 s                             |
| Open a 1 M-cell mesh in the viewport           | < 2.0 s                             |
| Viewport rotate (5 M-cell mesh, 60 fps)        | frame time < 16.7 ms p95            |
| First chat token (BYOK direct)                 | < 800 ms p95                        |
| First chat token (managed AI proxy)            | < 1200 ms p95                       |
| Pipeline stage cache hit                       | < 50 ms                             |
| Cantilever benchmark (mesh 50k cells, FEM solve) | < 2.5 s end-to-end                |
| Plugin discovery (10 plugins on disk)          | < 60 ms                             |
| Plugin call overhead (no-op tool)              | < 20 µs (warm)                      |
| Anthropic prompt-cache hit rate (typical session) | > 70 %                           |

When a budget is regressed, the PR cannot merge until either (a) the regression is fixed or (b) an RFC documents the new budget and the trade-off. We do not silently slip budgets.

## Security baseline

- **No secret-shaped strings in source.** A pre-commit hook + CI scan rejects anything matching `sk-`, `xoxb-`, AKIA[0-9A-Z]{16}, etc.
- **All credentials in OS keychain** in the desktop app; never in project files, never in cache, never in git history. See `AI_INTEGRATION.md`.
- **Dependency additions require an ADR** (or a one-line "covered by ADR-N"). Each ADR documents: license, supply-chain provenance, why this is preferable to a thinner alternative.
- **License scan in CI.** Every dependency's license is in `THIRD_PARTY_LICENSES.md`. A new dep with an unknown or incompatible license fails the build.
- **CVE scanning weekly** via `osv-scanner` or equivalent; high-severity CVE in a direct dep gets a P1 ticket.
- **GPL-licensed adapters (OpenFOAM) are subprocess-isolated.** Never linked into souxmar binaries. Enforced by CMake target boundary; documented in ADR-0003 and the OpenFOAM adapter README.
- **Plugin host signal/SEH frame** wraps every plugin call. Tested by a deliberate-segfault plugin in CI from S5 onward.
- **No `eval`, no `exec` of untrusted input** anywhere in the desktop app or AI agent. Tool inputs are typed and validated server-side.
- **HTTPS pinning** for managed-AI proxy calls (Pro tier, post-S14).
- **Tauri commands are explicit allow-list.** No remote-code execution path from frontend to native.
- **Notarised + signed** on every published release. Bypass = revoked release.

## Observability

The free desktop app has **zero telemetry by default**. This is a load-bearing trust commitment.

What we *do* have:

- **Structured logs** to `~/.local/share/souxmar/logs/` (Linux), `~/Library/Logs/souxmar/` (macOS), `%APPDATA%\souxmar\logs\` (Windows). User-readable. Auto-rotated.
- **Crash reports** opt-in at first launch; revocable. When enabled, sends a stack-only minidump (no project data) via Sentry to a souxmar-controlled bucket.
- **Performance traces** locally enabled with `souxmar --trace`; produces a Chrome trace JSON the user can inspect.
- **Audit log for AI tool invocations** at `.souxmar/chat/audit.log` per-project. User-visible, exportable, wipeable.

Pro/Team/Enterprise tiers may opt into additional usage analytics for billing reconciliation; documented per service in `BUSINESS_MODEL.md`.

## Branching, merging, releasing

- **`main` is always shippable.** No long-lived feature branches.
- **Merges are squash-by-default.** Multi-commit merges only for cohesive feature branches with already-clean histories.
- **Pre-merge:** CI green, two reviewers (or one for Tier-1), CODEOWNERS approval, no requested changes outstanding.
- **Versioning:** SemVer for the project; ABI version is independent and increments only on breaking ABI change. Pipeline format and agent tool contract have their own version integers (frozen at v1 post-Sprint 8).
- **Release cadence:** time-based, every 8 weeks. No "blocking" features — they ship in the next train.
- **Backports:** P0 and P1 fixes to the previous minor; older releases are best-effort.
- **Deprecations:** ≥ 1 minor cycle (8 weeks) of warning before removal.

## Incident response (post-public-alpha)

1. **Detection.** Alert from monitoring or external report.
2. **Acknowledge** (P0: < 30 min; P1: < 4 h).
3. **Mitigate first, fix second.** Roll back if needed; pulled releases get a security advisory.
4. **Communicate.** Status page + Discord + GitHub if user-visible.
5. **Post-mortem within 5 working days.** Blameless. Output: timeline, root cause, action items with owners and deadlines, runbook update.
6. **Action items tracked in Linear** with the same priority as feature work.

Pre-public-alpha incidents follow the same template, just informally — there is no status page yet.

## Architecture Decision Records (ADRs)

Every non-trivial architectural decision gets an ADR in `docs/adr/`. ADRs are short (1–2 pages), numbered, immutable once accepted, and linked from the code that implements them.

Triggers for a new ADR:

- A new dependency added to vcpkg.
- A non-trivial public API choice.
- Picking among multiple viable approaches (the *why* of one over the others).
- A decision that overrides a previously-stated default in the design docs.

Template in `docs/adr/0000-template.md`. The first three filled in: ADR-0001 (C ABI for plugins), ADR-0002 (Tauri over Electron), ADR-0003 (BYOK as default for AI).

## Pre-mortems

Before any Tier-3 RFC merges, the author writes a 1-page pre-mortem: "It is one year from now. This decision has gone badly. What happened?" This forces the author to enumerate concrete failure modes, not generic ones. The pre-mortem lives in the RFC and is referenced at the 6- and 12-month checkpoints.

We use pre-mortems sparingly — they are expensive to do well — but for big bets (the C ABI, the Tauri choice, the BYOK default, the open-source desktop) they are mandatory.

## Documentation as code

- Public docs source-of-truth lives in `docs/`. They render to the public docs site; they are *also* the canonical reference internally.
- Code examples in docs are extracted and built in CI. A doc that has a broken example fails the build.
- Generated API reference (Doxygen for C++, Sphinx for Python) is regenerated per release; deltas summarised in the changelog.
- The docs site is generated from source on every release; preview builds for every PR.

## Anti-patterns we explicitly avoid

These show up in retrospectives and in code review feedback. We do not adopt them, even when convenient.

- **Hidden global state.** Nothing useful lives in a process-global. All state hangs off an explicit context handle.
- **Reflection / metaprogramming as a shortcut.** Plain C structs at the ABI; plain C++ inside.
- **Macros to "save typing."** Macros are reserved for things only macros can do (token pasting, header guards, conformance helpers).
- **Conditional compilation as the API.** `#ifdef`-tangles obscure the contract; we feature-gate at the CMake target level.
- **"Temporary" code paths that survive a year.** Anything tagged TODO/FIXME with no owner is a CI warning after 90 days.
- **Tests that only the author understands.** A failing test must produce a message that a reviewer can act on without paging the author.
- **Snapshot tests as the only coverage.** Snapshots catch regressions; they do not document intent. We pair them with assertions on what specifically should be true.
- **Optimisation without measurement.** No "this should be faster" merges. Either there's a benchmark showing the win, or the change waits.
- **Architectural changes via clever PR titles.** Tier-3 changes go through RFC. Always.
