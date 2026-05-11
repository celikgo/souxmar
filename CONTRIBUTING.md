# Contributing to souxmar

Thank you for considering a contribution. souxmar is built by a small senior team plus an open community of plugin authors, researchers, and engineers. We aim to make contributing **clear and unsurprising**.

This document is the on-ramp. The deeper contracts live in:

- [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md) — merge tiers, RFC process, maintainer roles
- [`docs/ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md) — Definition of Done, perf budgets, security baseline
- [`docs/TEAM_STRUCTURE.md`](docs/TEAM_STRUCTURE.md) — who owns what
- [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md) — for plugin authors specifically
- [`.claude/skills/onboarding-souxmar-contributor/SKILL.md`](.claude/skills/onboarding-souxmar-contributor/SKILL.md) — first-PR walkthrough
- [`.claude/skills/writing-souxmar-rfc/SKILL.md`](.claude/skills/writing-souxmar-rfc/SKILL.md) — for Tier-3 changes

## Quick start

```bash
# 1. Clone
git clone https://github.com/souxmar/souxmar.git
cd souxmar

# 2. Build (uses vcpkg in manifest mode; first run ~20–30 min)
export VCPKG_ROOT=$HOME/vcpkg     # or wherever you cloned vcpkg
cmake --preset dev
cmake --build --preset dev

# 3. Test
ctest --preset dev --output-on-failure
```

If that succeeds end-to-end on your machine, your environment is healthy.

## Code of Conduct

Participation in this project is governed by the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md).
Reports go to `conduct@souxmar.dev` (placeholder until org is provisioned).

## Developer Certificate of Origin (DCO)

Every commit must be signed off:

```bash
git commit -s -m "Your message"
```

This appends a `Signed-off-by: Your Name <your@email>` trailer that certifies the [Developer Certificate of Origin](https://developercertificate.org/). We do not require a CLA.

The DCO check runs on every PR; missing sign-offs block merging. To fix existing commits without a sign-off:

```bash
git rebase --signoff main..HEAD
```

## Choosing the right merge tier

souxmar uses three tiers (full detail in [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md)):

| Tier | Examples                                                | Approval needed                          |
| ---- | ------------------------------------------------------- | ---------------------------------------- |
| 1    | Doc typo, dependency bump already covered by CI         | One reviewer                             |
| 2    | Bug fix, in-tree plugin, refactor within a module       | Two reviewers from the affected module   |
| 3    | C ABI, on-disk format, agent tool contract, governance  | RFC + two maintainer approvals + 7-day comment window |

When in doubt, propose your tier in the PR description. A maintainer will confirm.

## The PR workflow

1. **Open an issue first** (or comment on an existing one) describing the change. Skip this only for true Tier-1 work.
2. **Branch from `main`:** `git checkout -b your-name/short-description`.
3. **Make the change**, including tests, docs, and benchmark updates per the Definition of Done.
4. **Push; open a PR;** fill in the template completely.
5. **CI runs** (~25 min for typical PRs). Fix anything red.
6. **Reviewers respond** within 1 working day; you respond to feedback within 1 working day.
7. **Merge** once approved + green. Maintainer squashes and merges by default.

## Definition of Done

Per [`docs/ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md), a change is done only when:

- Code merged through standard review
- Tests written / updated; coverage gate met
- Public-API changes documented in `docs/`
- Performance gates passing (no regression > 5% on tracked benchmarks)
- Determinism gate passing (if relevant)
- No new dependency without an ADR
- DCO sign-off on every commit

## What kind of contributions we want

Most welcome:

- **Bug fixes** with a regression test.
- **New in-tree plugins** (mesher, solver, element, postproc) following [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md).
- **New adapters** wrapping production-quality external tools.
- **Examples and tutorials** for `examples/`.
- **Out-of-tree plugins** (your own repo) — list them in [`docs/plugin-index.md`](docs/plugin-index.md).
- **Documentation improvements** — typos, clarifications, missing detail.
- **Performance improvements** with a benchmark proving the win.

What needs more discussion (open an issue or RFC first):

- New build-time dependencies.
- New surfaces (CLI sub-command, agent tool, public API endpoint).
- Changes to the C plugin ABI (`include/souxmar-c/`).
- Changes to the on-disk pipeline YAML/TOML schema.
- Changes to the agent tool contract.
- Changes touching governance, license, or release process.

For the last three, an RFC is required before code review begins. See [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md).

## Style

- C++ formatted by `clang-format-17` against `.clang-format`. CI verifies.
- C++ linted by `clang-tidy-17` against `.clang-tidy` (best-effort locally, advisory in CI for now).
- TypeScript / JS formatted by Prettier (config bundled when desktop-app sources land).
- Python formatted by `ruff format` (Sprint 4+).
- 2-space indent, 100-column max line.
- LF line endings everywhere except Windows-specific scripts (enforced by `.gitattributes`).

## Communication

- **Daily questions:** `#dev` on Discord (link in `README.md` once live).
- **Design discussions:** GitHub Discussions in the project repo.
- **Real-time review:** weekly RFC office hours (Wednesdays — calendar in the team Slack).
- **Security disclosures:** GitHub private security advisories. Do not file in public issues.

## What we do not do

- We do not require a CLA. DCO sign-off only.
- We do not unilaterally close issues marked `needs-info` until a 30-day stale window.
- We do not relicense the project. Apache 2.0 stays. See [`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md).
- We do not have daily standups across teams. Async coordination is the norm.

Welcome aboard.
