---
name: reviewing-abi-changes
description: Use when reviewing or proposing any change to include/souxmar-c/ (the C plugin ABI) or to the agent tool contract. Validates SemVer compliance, checks for binary-breaking changes, and surfaces RFC requirements. Triggers on "ABI change", "include/souxmar-c", "souxmar_plugin_register", "tool contract", "agent tool".
---

# Reviewing ABI changes

The C plugin ABI in `include/souxmar-c/` and the agent tool contract in `src/ai/tools/schema/` are both **frozen contracts** post-Sprint 7. Every change must be evaluated for whether it breaks existing plugins or agent integrations.

## When to use this skill

- Reviewing a PR that touches `include/souxmar-c/` or `src/plugin-host/`.
- Reviewing a PR that touches `src/ai/tools/schema/` or adds an agent tool.
- Drafting a Tier-3 RFC for an ABI change.
- Auditing a release candidate for ABI compliance.

## Hard rules (CI enforces these; this skill double-checks)

For ABI v1 (souxmar 1.x), the following are **forbidden** without a major-version bump:

- Removing any exported symbol.
- Changing the signature of an existing function (parameter types, return type, calling convention).
- Removing or reordering fields in a public struct.
- Changing the size or alignment of a public struct.
- Changing the value of any enum constant.
- Changing the meaning of any error code.
- Tightening any precondition (a previously-valid call becoming invalid).

The following are **allowed** in a minor version (additive only):

- Adding a new exported symbol.
- Adding a new struct.
- Adding a new enum value at the END of an enum (not in the middle — that re-numbers).
- Adding a new optional registration namespace (`writer.gltf` is fine; renaming `writer.vtu` is not).
- Loosening a precondition (a previously-invalid call becoming valid).

## Review checklist

For every PR touching the ABI:

1. **Diff `include/souxmar-c/` against the previous release tag.** Any of the forbidden changes above is a block.
2. **Run `tools/abi-check.sh`** (when available) — this calls `abidiff` against the published baseline ABI dump.
3. **Confirm the conformance suite still passes** for all in-tree reference plugins.
4. **Confirm the Python plugin shim still compiles** — it is a downstream consumer of the same ABI.
5. **Check the manifest schema** in `src/plugin-host/manifest.cpp` for any matching change. ABI and manifest schema move together.
6. **Confirm the change is documented** in `docs/PLUGIN_SDK.md` *in the same PR*.
7. **Confirm an ADR exists** for any non-trivial ABI shape decision.

## Agent tool contract

The agent tool contract is treated with the same discipline as the C ABI. The schema files in `src/ai/tools/schema/` define the shape of every tool the agent can call.

For changes there:

1. **Adding a new tool:** allowed in a minor version. Document in `docs/AI_INTEGRATION.md`. Add to the agent eval suite.
2. **Removing a tool:** forbidden without major version bump. If a tool needs to be deprecated, mark it with `deprecated: true` in the schema, keep it functional, and remove it in the next major.
3. **Changing a tool's input schema:** treat as ABI-breaking. Adding optional inputs is fine; making a previously-optional input required is breaking.
4. **Changing a tool's output schema:** treat as ABI-breaking. Adding fields is fine; removing or retyping fields is not.
5. **Changing the confirmation policy** of an existing tool from `auto` → `confirm-once` is a usability regression but not breaking; the reverse is a security regression and is forbidden without an RFC.

## RFC requirement

Any change that fails the "allowed in minor version" criteria above requires a Tier-3 RFC per `docs/GOVERNANCE.md`. Use the `writing-souxmar-rfc` skill.

The RFC must include:
- The exact ABI delta (annotated diff).
- A migration plan for existing plugins.
- Whether the souxmar shim layer can hide the change from old plugins (deferring the break) and at what cost.
- Pre-mortem.

## Common mistakes to flag in code review

- Adding a struct field in the middle of a public struct (resizes it, breaking layout).
- Adding an enum value in the middle of an enum (re-numbers everything after).
- Changing `int` to `int32_t` in a header (looks innocent; on some platforms `int` is `long`, and the binary signatures differ).
- Removing a `const` from a parameter (changes mangled name on some toolchains; for C ABI it changes calling convention semantics for some compilers).
- "Just a default value" for a struct field — there are no default values in C structs. The new field still needs a deallocator/destructor story for old plugins.
- Bumping the manifest schema version without bumping the ABI integer.

## Reference

- `docs/PLUGIN_SDK.md` — ABI specification.
- `docs/GOVERNANCE.md` — Tier-3 RFC process.
- `docs/adr/0001-c-abi-for-plugins.md` — why we chose C ABI; what the freeze commits us to.
- `tools/abi-check.sh` — the abidiff harness.
