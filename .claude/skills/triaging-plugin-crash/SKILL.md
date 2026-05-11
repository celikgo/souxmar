---
name: triaging-plugin-crash
description: Use when a plugin reports SOUXMAR_E_PLUGIN_FAULT, when a user reports a crash they suspect is plugin-caused, or when investigating a third-party plugin's stability. Triggers on "plugin crash", "SOUXMAR_E_PLUGIN_FAULT", "segfault in plugin", "plugin segv", "plugin host caught".
---

# Triaging a plugin crash

The souxmar plugin host wraps every plugin call in a signal/SEH frame so a plugin segfault is caught and reported as `SOUXMAR_E_PLUGIN_FAULT` rather than killing the host. This skill covers diagnosing those faults — whether the plugin is in-tree, third-party open-source, or commercial.

## When to use this skill

- Logs show `SOUXMAR_E_PLUGIN_FAULT` from any plugin invocation.
- Desktop app reports "the X plugin crashed" in the chat panel.
- User report: "souxmar froze" or "souxmar quit" near a known-plugin operation.
- Auditing a third-party plugin for the conformance badge.

## When NOT to use this skill

- Crash is in `souxmar` itself (libsouxmar-core, plugin host, pipeline orchestrator). That is a host bug; standard crash-triage flow.
- Crash is in the desktop app's UI (Tauri / React). That is a frontend bug; check Sentry.
- "Crash" is actually a non-crash error path returning a structured `souxmar_status_t`. That is normal failure; no host-level triage needed.

## Triage flow

### Step 1 — Confirm it is the plugin

Check the audit log:

```bash
tail .souxmar/chat/audit.log
```

A line like `tool=mesh status=plugin_fault input_hash=... runtime_ms=143 plugin=mesher.tetra.netgen reason=SIGSEGV` confirms the crash was caught at the plugin boundary.

If the crash is *not* in the audit log, it likely happened in the host (escalate as a host bug) or in a plugin invoked outside the AI surface (check the orchestrator log at `~/.local/share/souxmar/logs/`).

### Step 2 — Identify the plugin

The fault report includes:
- Plugin id (`mesher.tetra.netgen`).
- Plugin binary path.
- Plugin manifest (version, author, license, conformance badge status).
- Signal that fired (`SIGSEGV`, `SIGABRT`, `SIGBUS`, Windows EXCEPTION_*).
- Function vtable entry that was being called (`mesh_fn`, `solve_fn`, etc.).
- Input hash (so the user can reproduce; do NOT assume the input is small or non-sensitive).

### Step 3 — Reproduce (if possible)

```bash
souxmar plugin reproduce \
  --plugin mesher.tetra.netgen \
  --input-hash a93f... \
  --asan
```

Re-runs the same plugin call against the same cached input under ASAN, capturing a stack trace. Output goes to `.souxmar/cache/repro/`.

If reproduction succeeds, you have a stack trace to send to the plugin author. If not, the bug is intermittent (race, uninitialised memory) — flag it as such.

### Step 4 — Classify

| Pattern                                                                | Likely cause                                            |
| ---------------------------------------------------------------------- | ------------------------------------------------------- |
| Crashes only on macOS, not Linux                                       | Toolchain difference; often AVX assumption              |
| Crashes on first call after host startup, never on subsequent          | Plugin holds global state from a previous registration  |
| Crashes only with multi-threaded orchestrator (parallel DAG branches)  | Plugin declared `reentrant` but isn't                   |
| Crashes on input ≥ N cells                                             | Stack overflow; plugin recurses on cells                |
| Crashes intermittently with no obvious trigger                         | Race, use-after-free, uninitialised memory              |
| Crashes with `EXC_BAD_ACCESS` on macOS reading a string                | Dangling reference across the C ABI boundary            |

### Step 5 — Mitigate

- **For the user:** the host has already isolated the crash. The user can disable the offending plugin: `souxmar plugin disable mesher.tetra.netgen`, and continue using the rest of the app.
- **For the project:** if the plugin is in-tree, fix it (file a P1 bug). If third-party, file an issue against the plugin's repo with the reproduction artefact.

### Step 6 — File the bug

Use the plugin's preferred bug tracker (linked in its manifest's `homepage` field). Include:

- Souxmar version.
- Plugin id and version.
- Host OS and architecture.
- Stack trace from ASAN (if reproducible).
- Input artefact link (the cached input from `.souxmar/cache/repro/`) — verify with the user that this is shareable; for sensitive geometry, just include the manifest summary.
- Whether the plugin holds the conformance badge (this affects the user's expectation of stability).

### Step 7 — Update the plugin index

If a plugin has crashed twice for distinct users on distinct inputs:

- Mark its conformance status `degraded` in the index.
- Add a note pointing to the open bug.
- The user-facing plugin browser will show the warning.

## When the crash isolation itself fails

The crash isolation frame catches signals and SEH exceptions. It does NOT protect against:

- Memory corruption that takes effect later (the plugin scribbles on host memory; the host crashes minutes afterward).
- Deadlocks (no signal fires; the host hangs).
- Thread-safety violations in `internal-parallel` plugins that interfere with the host's own threads.

If the entire desktop app crashed (not "the plugin crashed"), suspect one of these. The mitigation discussed in `R-006` of the sprint plan is per-plugin out-of-process workers; if recurring, surface the proposal at the next architecture review.

## Common mistakes

- Assuming a crash is in souxmar. Always check the audit log first; the plugin host is good at attribution.
- Disabling the plugin globally without telling the user. The user may have other plugins from the same author.
- Updating the plugin's expected output to match the post-crash state. The crash invalidates the operation; do not "memorialise" the wrong answer.
- Forgetting to check whether the plugin author has already shipped a fix. Look at the plugin index for newer versions before opening a bug.
- Sharing the crash reproducer without checking whether the user's geometry is sensitive (defence, ITAR, NDA).

## Reference

- `docs/PLUGIN_SDK.md` — plugin ABI, error model, threading contracts.
- `docs/AI_INTEGRATION.md` — audit log format.
- `docs/SPRINT_PLAN.md` — risk R-006.
- `src/plugin-host/error_frame.cpp` — the crash isolation implementation.
