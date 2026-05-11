<!--
  Thank you for contributing to souxmar! Please fill this in completely.
  PRs without context get bounced — see docs/CONTRIBUTING.md.
-->

## Summary

<!-- One paragraph: what changes, why now. Skip the "what" the diff already shows. -->

## Linked issue / RFC

<!-- e.g. "Closes #123" or "Implements RFC-0007". Required. -->

## Tier

<!-- See docs/GOVERNANCE.md. Pick one and delete the others. -->

- [ ] Tier 1 — trivial (doc typo, dependency bump covered by CI)
- [ ] Tier 2 — standard (bug fix, in-tree plugin, refactor within a module)
- [ ] Tier 3 — heavy (touches ABI, on-disk format, agent tool contract, data model, governance) — **RFC required**

## Areas affected

<!-- Tick every team whose area this touches. Their reviewer is required. -->

- [ ] Core / Kernel
- [ ] Plugin Host (C ABI)
- [ ] Adapters / Solvers
- [ ] Desktop / UI
- [ ] AI Integration
- [ ] Platform / Release
- [ ] DX / Docs

## Definition of Done — author checklist

<!-- Per docs/ENGINEERING_PRACTICES.md. Every box must be ticked or explicitly N/A. -->

- [ ] Tests written or updated; coverage gate met (≥85% line on changed files)
- [ ] Public API or behaviour change reflected in `docs/`
- [ ] Performance benchmarks unchanged (or regression documented)
- [ ] Determinism gate passing (PR matrix)
- [ ] Plugin conformance suite passing (if plugin host or in-tree plugin touched)
- [ ] No new dependency without an ADR
- [ ] No secrets / credentials in source or fixtures
- [ ] DCO sign-off on every commit (`git commit -s`)
- [ ] ADR filed in `docs/adr/` if architecturally non-trivial

## Test plan

<!-- How a reviewer can validate this works. Specific commands, specific files. -->

```
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## Screenshots / output

<!-- Required for UI changes. Optional otherwise. Show before/after. -->

## Risks and rollback

<!-- What could break? What's the rollback if this lands and turns out wrong? -->
