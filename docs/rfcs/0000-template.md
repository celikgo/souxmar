# RFC NNNN: Title

<!--
  Copy this file to docs/rfcs/NNNN-short-name.md (next free number) and edit.
  See docs/GOVERNANCE.md for the full RFC process and the
  `writing-souxmar-rfc` skill for guidance.
-->

- **Author:** Your name
- **Status:** Draft | Final-comment | Accepted | Rejected | Superseded by RFC-XXXX
- **Tracking issue:** #NNNN
- **Affects:** ABI | data model | pipeline format | agent tool contract | governance | other
- **Tier:** 3
- **Date opened:** YYYY-MM-DD
- **Date `final-comment` started:** YYYY-MM-DD (set when a maintainer flips status)
- **Date accepted / rejected:** YYYY-MM-DD

## Summary

One paragraph. The decision in plain language.

## Motivation

What problem are we solving? Who is affected? What is the evidence that this matters now?

Quote specific user reports, performance measurements, or observed pain. Generic motivation produces generic decisions; be concrete.

## Proposal

The concrete change.

- For **ABI changes**: the exact header diff in a fenced code block.
- For **pipeline-format changes**: the exact YAML/TOML schema delta with a before/after example.
- For **agent-tool changes**: the exact JSON Schema for inputs and outputs.
- For **governance / process changes**: the exact text being added, modified, or removed in `docs/GOVERNANCE.md` (or the relevant doc).

Include code examples. Include migration examples.

## Alternatives considered

At least two alternatives, each with:

- A one-paragraph description.
- Why it was rejected. Be honest and specific — "considered X but the build cost is N% higher" beats "considered X but it's worse."

If the only alternative you considered is "do nothing," that is a sign the proposal is not yet baked.

### Alternative A: …

### Alternative B: …

### (Considered and rejected: do nothing)

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

It is one year from today. This decision has gone badly. Write a paragraph in past tense: what specifically went wrong?

Then list the leading indicators we should watch for in the first 6 months to catch this early.

## Open questions

What is still unresolved? What needs to be decided before merge? What can be deferred to follow-up RFCs?

## Implementation plan

Sketch of the implementation, broken into PRs. Not a contract — a forcing function so reviewers know what they are committing to when they approve.

- [ ] PR 1: …
- [ ] PR 2: …
- [ ] Documentation update in `docs/…`
- [ ] ADR filed at `docs/adr/NNNN-…`

## References

- Linked design docs.
- Linked PRs / issues.
- Linked ADRs.
- Linked external precedent (other projects' RFCs that informed this).
