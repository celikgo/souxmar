# Governance

This document covers how souxmar accepts contributions, how upstream merges are gated, and how the project makes architectural decisions. It is deliberately lightweight at v1.0 — formal foundations come later if and when the community warrants them.

## Roles

- **Contributors** — anyone who opens an issue, comment, or pull request.
- **Reviewers** — recognised contributors with merge rights to a specific module (`mesh/`, `solver/`, `plugin-host/`, etc.). Two reviewer approvals merge a non-trivial PR.
- **Maintainers** — small group with write access to the full repo, the release pipeline, and the plugin index. Responsible for breaking-change decisions and for tie-breaking RFCs.
- **TSC (Technical Steering Committee)** — convened only when the maintainers cannot reach consensus on an RFC, or when a CoC issue requires escalation. Initially the TSC is the founding maintainers; an election process is added once the project has more than ten active maintainers.

Reviewer status is granted on demonstrated taste in a module after a handful of high-quality PRs in that module. There is no application form. Maintainer promotion is a maintainer decision and requires consensus among existing maintainers.

## Merge tiers

Not all changes carry the same risk. souxmar uses three tiers of merge gating, applied to the *change*, not the contributor.

### Tier 1 — Trivial

Documentation typos, comment fixes, dependency version bumps already covered by CI, test additions for existing behaviour. **One reviewer approval, CI green, merge.**

### Tier 2 — Standard

Bug fixes, new in-tree plugins, new tests, new examples, refactors that do not cross module boundaries, additive Python API surface. **Two reviewer approvals from the affected module, CI green including conformance suite, merge.**

### Tier 3 — Heavy

Anything touching:
- the C ABI in `include/souxmar-c/`,
- the on-disk pipeline format,
- the data model in `libsouxmar-core`,
- the threading or memory contracts,
- license, governance, or release process,
- a new build-time dependency.

These require an **RFC** (see below) before code review begins, and **two maintainer approvals plus a 7-day comment window** before merge. The bar is high deliberately: every Tier 3 change is a cost we levy on every plugin author, downstream user, and packager forever.

## RFC process

For a Tier 3 change, the contributor opens a PR against `docs/rfcs/` with a numbered Markdown file:

```
docs/rfcs/0007-streaming-large-meshes.md
```

The template:

```markdown
# RFC 0007: Streaming-mode buffers for large meshes

- Author: …
- Status: Draft | Final-comment | Accepted | Rejected | Superseded
- Tracking issue: #123
- Affects: ABI, on-disk format

## Summary
One paragraph.

## Motivation
What problem, what users, what evidence.

## Proposal
Concrete API, schema changes, examples.

## Alternatives considered
At least two, with why they were rejected.

## Drawbacks
Honest list. ABI churn cost. New surface area.

## Migration plan
What happens to existing plugins / pipelines / users.

## Open questions
```

Discussion happens in the RFC PR. Maintainers tag it `final-comment` once they believe consensus is reached; it merges seven days later if no new substantive objections are raised. The accepted RFC becomes documentation; the implementation PR references it.

This is the same approach used by Rust, Python (PEPs), and Kubernetes (KEPs). It is heavyweight on purpose — Tier 3 changes are rare and irreversible.

## Upstream merges from forks

souxmar is built to be forked. Companies and labs are expected to maintain internal forks with proprietary plugins, custom solvers, or experimental data-model extensions. The Apache 2.0 license permits this without restriction.

When a fork wants to send improvements upstream:

1. **Bug fixes and self-contained features** — open a PR against `main`, follow the tier rules above. Fork-of-origin is irrelevant.
2. **Large feature batches** — split into reviewable PRs, ideally one feature per PR, with a tracking issue listing the planned series. We will not merge a 30,000-line "kitchen sink" PR; it will be sent back for slicing.
3. **Data-model extensions** — file an RFC first. The data model is the central asset; unprincipled additions decay it.
4. **New adapters** — welcome. Each adapter is feature-gated by CMake and has its own owner; no global-quality bar applies as long as the adapter is opt-in at build time.

We do not require contributors to assign copyright. We do require a Developer Certificate of Origin (DCO) sign-off (`git commit -s`) on every commit. No CLA. This matches Linux kernel practice and minimises the friction for both individuals and corporations to contribute.

## Plugin index governance

Out-of-tree plugins live under their authors' control. The project maintains an *index* (a curated list of known plugins, with metadata, links, and conformance status) but does not host the binaries.

To list a plugin in the index:

1. Open a PR against `docs/plugin-index.md` with the manifest summary and a link to the source.
2. Plugins must be open-source under an OSI-approved license to appear in the index. Closed-source plugins are entirely valid and the SDK supports them; we just do not advertise them.
3. The index is split into "conformant" (passes the conformance suite in CI) and "community" (does not). No other quality gate.

We will not vet plugin behaviour, code, or supply chain. The index is a directory, not an endorsement.

## Code of Conduct

The project adopts the Contributor Covenant 2.1 verbatim. CoC reports go to a dedicated alias separate from the maintainer list, handled by a rotating subset of maintainers (so that a report about a maintainer is not handled by that maintainer).

## Release process

- **Cadence:** time-based, every eight weeks. No "blocking" features — anything not ready ships in the next train.
- **Versioning:** `MAJOR.MINOR.PATCH` for the project; ABI version (frozen integer) is independent and rarely incremented.
- **Branches:** `main` is always shippable. Releases are tagged on `main`; long-lived branches are avoided.
- **Backports:** security and severe correctness fixes are backported to the previous minor release. Older releases are best-effort.
- **Deprecation:** anything user-facing that gets removed is deprecated for at least one full minor cycle (eight weeks) with a migration note in the changelog.

## ABI freeze process

The plugin C ABI is the contract that lets out-of-tree binaries built today keep loading against souxmar releases years from now. We treat freezing it as a discrete, auditable event with three named states.

### State 1 — pre-freeze

The ABI version macro is set, the headers under `include/souxmar-c/` exist, but the contract is *not yet* binding. Any header in the v1 surface may change in any way until the candidacy is declared. This is the state up to and including Sprint 5 push 5.

### State 2 — frozen-candidate

A maintainer opens a PR that:

1. Defines `SOUXMAR_ABI_FREEZE_CANDIDATE 1` in `include/souxmar-c/abi.h`.
2. Lists every header file under freeze in the candidacy ADR (see ADR-0007 for the v1 instance).
3. Lands a status block in `docs/PLUGIN_SDK.md` and `include/souxmar-c/abi.h` naming the formal-freeze target date (two sprints out).
4. Opens a tracking issue for the soak period.

During soak the **ratchet rules** are absolute:

- **Additive minor surfaces are allowed.** A new function, a new struct, a new error code — provided unknown fields are zero-initialised by the host and unknown struct tail bytes are ignored by older plugins. Each such addition bumps `SOUXMAR_ABI_VERSION_MINOR`.
- **Breaking changes cancel the candidacy.** Renaming a function, changing a parameter type, reordering struct fields, or changing the numeric value of any `SOUXMAR_E_*` / `SOUXMAR_ET_*` / `SOUXMAR_VK_*` constant resets the soak to zero — a *new* candidacy PR is required.
- **Conformance suite v1 stays green** every night across all in-tree example plugins. Two consecutive nightly failures attributable to the soak surface cancel the candidacy.
- **Perf-nightly stays within threshold** on the bulk-buffer hot paths. A confirmed regression that is not a measurement artifact cancels the candidacy.

The maintainer who opens a candidacy PR is on the hook for the cancellation/exit decision. They do not freeze unilaterally — they propose, the soak runs, the gates clear (or don't), and the merge that flips State 2 → State 3 carries the same two-maintainer-approval bar as any Tier 3 change.

### State 3 — formally frozen

When the soak completes cleanly, a follow-on PR:

1. Removes the `SOUXMAR_ABI_FREEZE_CANDIDATE` macro.
2. Tags the repository with `abi-v1-frozen` (annotated tag, signed by a release maintainer).
3. Updates `docs/PLUGIN_SDK.md` and the candidacy ADR with the freeze date.
4. Locks the listed v1 headers via CI: subsequent PRs touching them require two maintainer approvals AND a justification block referencing the deprecation / minor-surface / major-bump path being taken.

From this point on, ABI v1 is the contract. Breaking it requires a major bump (souxmar 2.0) and the one-major-overlap deprecation cycle described in [`PLUGIN_SDK.md`](PLUGIN_SDK.md) § Versioning.

### Why the soak

Two sprints is short enough that we keep momentum and long enough that the conformance + perf nightlies have ~15 independent runs to surface a regression. We considered both a one-sprint and a four-sprint soak; one sprint felt too narrow against shared-runner noise floors, and four sprints would have pushed Sprint 7's planned freeze gate into Sprint 9 without buying meaningful additional confidence.

## Decision-making philosophy

Two principles guide every governance call:

1. **Stability is a feature paid for in caution.** A merged change is a contract with every downstream user, and the cost of breaking it later vastly exceeds the cost of debating it now.
2. **The pipeline belongs to its users, not to its maintainers.** When users and maintainers disagree on whether a change is worth its breakage, users win. The maintainers' job is to keep the project loadable, buildable, and trustworthy — not to optimise it for themselves.
