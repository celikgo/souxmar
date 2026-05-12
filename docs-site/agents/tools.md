<!--
  This file is **generated** by scripts/docs-site/gen-agent-tools.py.
  Do not hand-edit — your changes will be overwritten on the next
  docs-site build. To change a tool's name / description / category,
  change it at the source (include/souxmar/ai/*.h + the matching
  src/ai/tools/*.cpp) and rebuild the docs site.
  Generator schema version: 1.

  Sprint 13 push 2: this file is checked into the repo as a
  placeholder pointing at the regeneration command. The first
  post-v0.9.0 docs-site CI run replaces it with real content
  generated against the v0.9.0 binary.
-->

# Tool catalogue

This page is **generated** from the engine binary itself by running
`souxmar agent list --json` through
[`scripts/docs-site/gen-agent-tools.py`](https://github.com/souxmar/souxmar/blob/master/scripts/docs-site/gen-agent-tools.py).
The first build of this page off the v0.9.0 binary will replace this
placeholder with the full 18-tool catalogue.

If you're reading this on docs.souxmar.dev, the CI workflow that
rebuilds this page lives at
[`.github/workflows/docs-site.yml`](https://github.com/souxmar/souxmar/blob/master/.github/workflows/docs-site.yml).
The placeholder was committed in Sprint 13 push 2; the contract for
the generated content lives in the script's header.

## How this page is generated

```sh
scripts/docs-site/gen-agent-tools.py \
    --engine build/dev/tools/souxmar/souxmar \
    --out    docs-site/agents/tools.md
```

Re-runs in CI on every master-push that touches `docs-site/` or
`scripts/docs-site/`. To verify it's in sync locally:

```sh
scripts/docs-site/gen-agent-tools.py \
    --engine build/dev/tools/souxmar/souxmar \
    --out    docs-site/agents/tools.md \
    --check-only
```

## The contract

- **18 tools**, frozen at v1 final (see [ADR-0011](https://github.com/souxmar/souxmar/blob/master/docs/adr/0011-tool-contract-v1-final-freeze.md)).
- Tools are grouped by category: Read, Mesh, BC, Material, Solve,
  Field, Pipeline, Discovery, Export, UI.
- Each tool has a confirmation policy — `auto`, `confirm-once`, or
  `confirm-always`. Read/listing tools are `auto`; mutating tools
  (mesh / solve / export / apply_pipeline_diff) are `confirm-once`
  or `confirm-always`.
- The full catalogue with per-tool descriptions appears after the
  first generated build replaces this page.

See the [agents overview](/agents/) for the conceptual reference
that is not auto-generated.
