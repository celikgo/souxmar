# Community

souxmar's community-facing surfaces and the support / triage commitments behind them.

This document is the public contract for users reporting bugs, requesting features, or asking for help. The internal counterpart — who is on triage rotation when, escalation paths, the triage runbook — lives at `docs/internal/TRIAGE.md` (not committed; lives in the team's private ops repo).

## Where to go

| If you want to…                                       | Use                                                                |
| ----------------------------------------------------- | ------------------------------------------------------------------ |
| Report a bug                                          | [GitHub Issue → bug template](../../issues/new?template=bug.yml)   |
| Request a feature                                     | [GitHub Issue → feature template](../../issues/new?template=feature.yml) |
| Propose a Tier-3 architectural change                 | [GitHub Issue → RFC template](../../issues/new?template=rfc.yml) + RFC doc in `docs/rfcs/` |
| Ask a question / discuss design                       | [GitHub Discussions](../../discussions)                            |
| Chat real-time with other users + maintainers         | [Discord — invite at souxmar.dev/community](https://souxmar.dev/community) |
| Report a security vulnerability                       | **Email security@souxmar.dev** — do not open a public issue        |
| Get help authoring a plugin                           | [Discussions → Plugins](../../discussions/categories/plugins) or [#plugins on Discord](https://souxmar.dev/community) |

GitHub Discussions are the primary forum for design-shaped conversations. Discord is the primary forum for synchronous help, livestream Q&A, and the announcement channel for new releases. The two are not redundant — Discussions threads are searchable forever; Discord is for the volatile shape of conversation.

## Response SLAs

| Channel                              | First response               | Resolution / next-step               |
| ------------------------------------ | ---------------------------- | ------------------------------------ |
| Security disclosure (email)          | **24 hours**                 | Coordinated disclosure timeline within 72h, fix within 30 days for critical issues |
| GitHub bug report (P0 — data loss, crash) | **24 hours**             | Status update within 7 days; fix scheduled or root-caused |
| GitHub bug report (P1 — feature broken, no workaround) | **48 hours**     | Status update within 14 days |
| GitHub bug report (P2 — minor / has workaround) | **5 business days**   | Acknowledged + labelled within the window; no fix-time commitment |
| GitHub bug report (P3 — cosmetic / docs) | **2 weeks**              | Acknowledged + labelled |
| Feature request                      | **5 business days**          | Acknowledged + roadmapped (or declined with rationale) within 30 days |
| RFC                                  | **7 days**                   | Comment window opens within 14 days per `docs/GOVERNANCE.md` |
| Discussion / question                | best-effort                  | best-effort (community-driven; maintainers participate but don't gate) |
| Discord                              | best-effort                  | best-effort; not a support channel of record |

These SLAs are commitments the maintainer rotation makes during the public-alpha and beta windows. Once the v1.0 stable lands and the community is larger, the SLAs may tighten (formal triage rotation, on-call) or loosen (community-driven label workflow). They will never silently drop — any change to this table requires a PR + 14-day comment window per the docs-as-contract rule.

## Triage priorities

Issues are auto-labelled `needs-triage` on creation. The maintainer on triage rotation classifies each within the SLA window:

| Priority | Definition                                                                                                          |
| -------- | ------------------------------------------------------------------------------------------------------------------- |
| **P0**   | Data loss, hang, crash on every run, or security issue not yet privately disclosed. Drop everything.                |
| **P1**   | A documented feature is broken and there is no workaround. Schedule into the current sprint.                        |
| **P2**   | A feature is broken but a workaround exists, or the issue affects a small subset of users. Backlog with sprint hint. |
| **P3**   | Cosmetic, docs-only, or "nice to have" — backlog; resolved opportunistically.                                       |

Priority is the maintainer's call, not the reporter's. If you disagree with a priority assignment, leave a comment with the reasoning; the maintainer either re-labels or explains why the original stands.

When a bug is fixed and the fix lands in a release, the resolving PR adds a short record under [`docs/bug-reports/`](bug-reports/) — date-stamped, with symptom + root cause + what the fix covers + what it intentionally doesn't. Cross-referenced from the closing PR; ratifies the ADR-0017 triage convention. See [`docs/bug-reports/2026-05-12-cfd-stub-bcs.md`](bug-reports/2026-05-12-cfd-stub-bcs.md) for the first such record.

## Public alpha (v0.9.0) — what to expect

Through the v0.9.x beta and the v0.9.0 alpha window:

- **Things will break.** This is software in active development. The auto-updater + the plugin C ABI freeze (v1.3) + the agent tool contract (v1, 18 tools) are the bits we've committed to *not* breaking; everything around them may shift sprint-to-sprint.
- **Bug reports are the best contribution.** Every bug filed against the public alpha helps shape the v1.0 release. We prioritise reports from people running real analyses over synthetic minimisations; either is welcome, but the former is more useful.
- **The desktop app is still scaffolded.** The onboarding wizard works; the workbench shell exists; viewport / chat / inspector all wait on the `souxmar-bridge` FFI crate which lands in Sprint 12 push 2. If you're trying the desktop app and a panel says "Sprint 12+", that's the honest state — not a bug.
- **No telemetry.** souxmar collects no usage telemetry beyond optional crash reports the user enables in Settings → Privacy. Filing a bug means typing it into a form, not enabling auto-collection.

## Maintainer rotation (current)

| Week        | On triage (primary) | On triage (backup)  |
| ----------- | ------------------- | ------------------- |
| 2026-05-12  | TBA                 | TBA                 |
| 2026-05-19  | TBA                 | TBA                 |

The rotation is owned by the platform team lead. Maintainers who can't cover a week swap with the next person in the rotation and update this table in a PR. The rotation will be filled in as the team builds; for v0.9.0 alpha the maintainers cover collectively.

## Communications policy

- **Public channels are public.** Anything posted to GitHub Issues, Discussions, or Discord is visible to everyone forever. Do not post API keys, customer data, or proprietary geometry.
- **NDA-covered cases.** If your bug involves geometry or analysis results you cannot post publicly, mark the issue title `[private]` and request a private channel; the maintainer will reply with a one-time-use upload link.
- **Code of conduct applies.** See `CODE_OF_CONDUCT.md`. Maintainers reserve the right to lock threads or revoke access for violations.

## Where this document changes

This document is a contract. Substantive changes (new SLA window, new channel) require a PR + 14-day comment window. Cosmetic changes (typo fixes, link updates) ship directly.
