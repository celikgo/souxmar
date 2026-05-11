---
name: writing-souxmar-rfc
description: Use when proposing a Tier-3 architectural change that requires an RFC per docs/GOVERNANCE.md. Walks through the template, the comment-window process, and the maintainer review path. Triggers on "RFC", "Tier 3", "ABI change", "data model change", "pipeline format", "agent tool contract".
---

# Writing a souxmar RFC

Tier-3 changes (touching the C ABI, the on-disk pipeline format, the data model in `libsouxmar-core`, the agent tool contract, the threading or memory contracts, or governance / license / release process) require an RFC before code review begins.

## When to use this skill

- You are proposing a change to `include/souxmar-c/`.
- You are proposing a change to the on-disk pipeline YAML/TOML schema.
- You are proposing a new agent tool, or modifying an existing tool's contract.
- You are proposing a new build-time dependency.
- You are proposing changes to the release process, licensing, or governance.

## When NOT to use this skill

- Tier-1 (trivial) and Tier-2 (standard) changes do not need an RFC. Use a regular PR.
- Adding an in-tree plugin: that is Tier-2, not Tier-3.
- Adding a new adapter (e.g. a new mesher backend): Tier-2 unless it requires a build-time dependency change.

If unsure, ask a maintainer in the weekly RFC office hours rather than guessing.

## The process

1. **Open a PR against `docs/rfcs/`** with a numbered Markdown file, e.g. `docs/rfcs/0007-streaming-large-meshes.md`. Use the next free RFC number.
2. **Write the RFC using the template below.**
3. **Discussion happens in the PR thread.** Reviewers leave specific feedback.
4. **A maintainer marks the RFC `final-comment`** when consensus is reached.
5. **Seven-day comment window begins.** No new substantive objections → it merges as Accepted.
6. **Implementation PR follows**, references the accepted RFC.

## Template

Copy this template into your RFC file:

```markdown
# RFC NNNN: Title

- **Author:** Your name
- **Status:** Draft
- **Tracking issue:** #NNNN
- **Affects:** ABI | data model | pipeline format | agent tool contract | governance | other

## Summary

One paragraph. The decision in plain language.

## Motivation

What problem are we solving? Who is affected? What is the evidence that this matters now? Quote specific user reports, performance measurements, or observed pain.

Generic motivation produces generic decisions. Be specific.

## Proposal

The concrete change. For ABI: the exact header diff. For pipeline format: the exact schema delta with example before/after. For agent tools: the exact JSON Schema for inputs and outputs.

Include code examples. Include migration examples.

## Alternatives considered

At least two alternatives, each with:
- A one-paragraph description.
- Why it was rejected. Be honest and specific — "considered X but the resource cost is N% higher" beats "considered X but it's worse."

If the only alternative you considered is "do nothing," that is a sign the proposal is not yet bake.

## Drawbacks

The honest list. ABI churn cost. Plugin author migration burden. New surface area to maintain. Skill-set narrowing. Dependency footprint growth.

## Migration plan

What happens to:
- Existing in-tree plugins?
- Out-of-tree plugins shipped against the previous version?
- Existing pipeline files on disk?
- Saved chat sessions / agent histories?
- Documentation that references the old behaviour?

For an ABI change, this section also describes the shim layer (if any) that lets old plugins keep working in the next major.

## Pre-mortem

It is one year from today. This decision has gone badly. Write a paragraph in past tense: what specifically went wrong? Then list the leading indicators we should watch for in the first 6 months to catch this early.

## Open questions

What is still unresolved? What needs to be decided before merge? What can be deferred to follow-up RFCs?

## References

- Linked design docs.
- Linked PRs / issues.
- Linked ADRs.
- Linked external precedent (other projects' RFCs that informed this).
```

## Quality bar

A maintainer should reject an RFC that has any of these:

- A "Proposal" section that is three bullet points. Tier-3 changes deserve concrete, code-level specification.
- An "Alternatives" section with one alternative or a strawman. Two real alternatives, honestly evaluated.
- No migration plan when the change is breaking. Hand-waving "we'll figure it out" is not acceptable.
- No pre-mortem. The pre-mortem is what prevents the team from sleepwalking into a regret.
- No clear linkage to existing design docs. The RFC must fit into the existing architecture or explicitly redesign the relevant section.

## Common mistakes

- Conflating "I want to fix X" with "the architecture must change." Many fixes do not require an RFC.
- Bundling multiple decisions into one RFC. One decision per RFC. Cross-link related RFCs.
- Treating the RFC as a signed contract instead of a proposal. Reviewers should push back; authors should expect to revise.
- Writing the RFC after writing the implementation. The RFC is a forcing function for thinking — implementing first defeats the purpose.

## Reference

- `docs/GOVERNANCE.md` — full RFC process and merge tiers.
- `docs/SPRINT_PLAN.md` — when RFC office hours run.
- `docs/adr/` — accepted decisions, for precedent on tone and depth.
- `docs/rfcs/` (when present) — accepted RFCs, for precedent on structure.
