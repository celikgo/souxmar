# ADR-0025: `souxmar` CLI `--json` output shapes — schema=1 + typed Rust mirror

- **Status:** Accepted
- **Date:** 2026-05-15 (Sprint 17 push 1)
- **Author:** souxmar platform team
- **Deciders:** desktop, platform
- **Tier:** 2 (the `--json` output shapes are stable contracts the
  desktop's shell-out per ADR-0022 parses against)
- **Affects:** every `souxmar` subcommand with `--json` output;
  the souxmar-bridge crate gains a `cli_shapes` Rust mirror
  module; future CLI ratchets cite this ADR for the schema.

## Context

ADR-0022 named the MVC-via-subprocess pattern. Sprint 13's
`souxmar agent list --json`, Sprint 16 push 4's `souxmar plugin
install --json`, and Sprint 17+'s six-or-so more `--json`
subcommands all emit ad-hoc shapes today. Without a typed
contract, the shapes will drift; the desktop client will parse
them with hand-rolled JSON-shape assumptions that fall out of
sync.

The Sprint 16 retro queued this:

> ADR-0025 candidate (Sprint 17 push 3): "souxmar CLI --json
> output shapes — schema=1 discriminator + typed Rust mirror in
> souxmar-bridge crate."

(Sprint timing slid up — landing in Sprint 17 push 1 rather
than push 3 because the bootstrap PR carrying the typed mirror
also wants the schema decided first.)

## Decision

### Every `--json` output carries `schema = <int>`

```json
{
  "schema": 1,
  "kind":   "plugin_install_result",
  ...
}
```

- `schema` is mandatory + monotonically increasing per output
  shape. Bumping requires a Tier-2 deprecation cycle.
- `kind` is mandatory + names the output type (one per CLI
  subcommand). The Rust mirror keys its enum off this field.

### Typed Rust mirror lives in `souxmar-bridge::cli_shapes`

A new module in the bridge crate declares one struct per `kind`:

```rust
pub mod cli_shapes {
    use serde::{Deserialize, Serialize};

    #[derive(Debug, Clone, Serialize, Deserialize)]
    #[serde(tag = "kind")]
    pub enum CliOutput {
        #[serde(rename = "plugin_install_result")]
        PluginInstallResult {
            schema: u32,
            status: String,
            code:   String,
            id:     String,
            #[serde(default)]
            version: String,
            #[serde(default)]
            paid:    bool,
            #[serde(default)]
            license_supplied: bool,
            #[serde(default)]
            detail:  String,
        },
        // ... one variant per CLI subcommand
    }
}
```

The desktop's shell-outs parse `cli_shapes::CliOutput` via
serde_json. A missing field defaults via `#[serde(default)]` so
older CLI binaries (mid-rolling-upgrade) round-trip cleanly
against newer desktop clients — additive Tier-0 within a given
schema.

### Bumping the schema

The schema bumps when an existing field's *type* changes,
when a field is renamed, or when an existing variant is
removed. Adding a field is Tier-0 (additive); removing a
field is Tier-2 (deprecation cycle: the next schema=N+1 marks
it `#[serde(default)]` + `#[deprecated]`; schema=N+2 removes
it).

### What this ADR does NOT cover

- **Internal-only `--json` paths.** The eval runner's
  per-task latency JSON (Sprint 9 push 10) is consumed by CI
  reports, not the desktop client. It can keep its current
  shape; no `kind` discriminator required.
- **Non-CLI JSON output.** OpenAPI shapes for the four
  services (ADR-0019 / 0021 / 0023 / 0024) follow their own
  versioning per service; they don't share `cli_shapes`.

## Consequences

- Sprint 17 push 1 (this push) adds the `cli_shapes` module
  + retrofits `souxmar agent list --json` and `souxmar plugin
  install --json` to emit `schema=1 + kind=...`.
- Future `--json` outputs cite ADR-0025 for the schema
  discriminator pattern.
- The desktop crate's `commands.rs` parses outputs via
  `cli_shapes::CliOutput` rather than ad-hoc struct
  declarations.

— Sprint 17 push 1.
