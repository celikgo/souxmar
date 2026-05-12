# ADR-0022: MVC-via-subprocess — read through FFI, apply through subprocess

- **Status:** Accepted
- **Date:** 2026-05-14 (Sprint 16 push 1)
- **Author:** souxmar platform team
- **Deciders:** desktop, platform
- **Tier:** 1 (process — names a project-wide pattern for how
  the desktop client invokes engine-side state-machine
  surfaces)
- **Affects:** any future FFI surface whose underlying state
  machine is on a frozen contract or carries a single-process
  invariant; specifically Sprint 16's plugin install path +
  Sprint 17's account-portal token-refresh path.

## Context

Sprint 15 push 4 introduced the `auto_updater_menu` FFI
surface as **read-only** — the desktop's Inspector / Settings
panel reads the current installed version through FFI; the
"Apply update" button shells out to `souxmar update apply` on
the host process. The decision was made inline; Sprint 15's
retro queued the ADR.

Sprint 16's plugin marketplace push ratchets the same shape:
the desktop's "Install plugin" click would naturally route
through FFI for symmetry with the chat / pipeline surfaces.
Without a named pattern, every future state-machine FFI
re-litigates "read + apply, or read-only + shell-out?" The
ADR exists to close that question with a rule.

## Decision

**The C++ side's state-machine surfaces expose READ through
FFI; APPLY through subprocess shell-out.** Three rules feed
the principle:

1. **Frozen-contract rule.** When the C++ side is on a
   Tier-2+ frozen contract (plugin ABI v1.3, agent tool
   contract v1, update state schema=1, manifest schema=1),
   the apply path stays single-implementation. Re-
   implementing through FFI duplicates the frozen surface;
   the shell-out keeps it single-source-of-truth.

2. **Single-process-invariant rule.** When the apply path
   carries an invariant the FFI would break — rollback-log
   append-only ordering, install-layout atomic-swap
   serialisation, plugin-index signature-verify-then-commit
   — the apply stays in one process. The shell-out runs the
   same code path the CLI exercises; the desktop polls for
   completion via the next FFI read.

3. **No-difference-in-UX rule.** When the user-visible
   outcome doesn't differ between FFI and subprocess
   ("install completed" / "update applied" / "license
   verified"), the subprocess wins on cost. The desktop
   client spawns the CLI with `--json` for parseable output;
   the user sees the same OK / FAIL state either way.

**All three rules must hold** for a state-machine surface to
be subprocess-only. If any one fails, the surface is a
candidate for FFI apply (with an ADR-NNNN documenting the
break).

### Concrete invocation pattern

The desktop client invokes the CLI as:

```rust
std::process::Command::new("souxmar")
    .arg("plugin")
    .arg("install")
    .arg("--id")
    .arg(plugin_id)
    .arg("--json")
    .arg("--no-confirm")
    .output()
```

- `--json` ensures parseable output the desktop can render
  into its menu state.
- `--no-confirm` skips the CLI's interactive prompts (the
  desktop's UI handled the confirmation already).
- The desktop polls the corresponding FFI read surface
  (`pipeline_summary` / `update_menu_status` /
  `plugin_install_status`) after the subprocess exits to
  refresh its rendering.

### Surfaces covered by this ADR today

| Surface             | Read FFI                         | Apply path                |
| ------------------- | -------------------------------- | ------------------------- |
| Pipeline state      | `pipeline_summary` (Sprint 13)   | `souxmar run` subprocess   |
| Provider call       | `chat_send` (Sprint 14)           | (no apply — chat is RPC)   |
| Update state        | `update_menu_status` (Sprint 15) | `souxmar update apply`     |
| Plugin install      | `plugin_install_status` (Sprint 16) | `souxmar plugin install` |
| Account / quota     | `quota_status` (Sprint 17)        | `souxmar account refresh`  |
| Cloud sync          | `sync_status` (Sprint 17)         | `souxmar sync` subprocess  |

### What this ADR is NOT for

- **RPC-style FFI** — chat-send, viewport-render, anything
  whose "apply" is not durable state. Those stay full
  round-trip FFI.
- **In-process state the FFI must mutate** — the
  BridgeFeatureSet's `viewport_renderer` flag's Three.js
  hook is FFI all the way down; the Tauri side owns the
  GPU surface.

## Consequences

- Future state-machine FFI surfaces (Sprint 16 plugin
  install, Sprint 17 account portal) cite this ADR; no
  per-surface re-litigation.
- The CLI's `--json` output is the load-bearing contract
  for the desktop client. Sprint 16+ ratchets `--json` on
  every `souxmar` subcommand that this ADR routes through.
- The desktop bundle ships the `souxmar` CLI binary
  alongside the Tauri binary (Sprint 22+ public-beta
  packaging concern); a desktop install without the CLI
  cannot perform any apply.

## Alternatives rejected

- **Full FFI for everything.** Duplicates the frozen-
  contract surfaces; introduces double-bookkeeping for
  single-process invariants. Rejected.
- **Full subprocess for everything.** Loses the low-latency
  read surface the chat panel / inspector need. The poll-
  loop UX is much worse than direct FFI read. Rejected.

— Sprint 16 push 1.
