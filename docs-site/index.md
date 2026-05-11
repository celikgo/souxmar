---
layout: home

hero:
  name: souxmar
  text: Open-source CAE with an agentic AI chat
  tagline: CAD → mesh → FEM/CFD → post-processing. Local-first. Plugin-extensible. Apache 2.0.
  actions:
    - theme: brand
      text: Install souxmar
      link: /guide/install
    - theme: alt
      text: First pipeline
      link: /guide/first-pipeline
    - theme: alt
      text: GitHub →
      link: https://github.com/souxmar/souxmar

features:
  - title: Single pipeline, three surfaces
    details: The same C++ engine drives the CLI, the Python library, and the desktop app. Whatever the AI agent does, you can also do directly.
  - title: Agentic AI chat, BYOK
    details: Bring your own Anthropic / OpenAI / Ollama key. Your geometry never leaves your machine unless you ask it to.
  - title: Plugin ABI frozen at v1.3
    details: Third-party meshers, solvers, readers, and writers compile once and run on every souxmar release in the 1.x line.
  - title: Signed auto-update
    details: ed25519-signed release manifests + atomic apply + one-command rollback. Trust path documented top to bottom.
---

## What is souxmar?

souxmar is an **open-source CAE platform** — CAD modelling + mesh
generation + finite-element + CFD + post-processing — wrapped in a
**cross-platform desktop app** with an **agentic AI chat** that can
drive the entire pipeline.

The free tier is the full product. You bring your own AI provider
key (or run locally against Ollama), your own compute, and you owe
us nothing. The Pro tier adds managed AI + cloud sync; see
[the business-model page](/business/).

## Why it's different

- **Local-first.** Your geometry, your simulation results, your AI
  prompts — all on your machine unless you opt into Pro features.
  No telemetry beyond optional crash reports.
- **Three peer surfaces.** The desktop app, the CLI (`souxmar`),
  and the Python library (`pysouxmar`) all sit on top of the same
  C++ engine. The AI agent uses the same tool surface anyone can
  call directly.
- **Plugin-extensible at the ABI.** A stable C ABI (v1.3 frozen
  final for the 1.x line) lets meshers, solvers, readers, and
  writers ship as `.so`/`.dylib`/`.dll` files. Drop one in
  `~/.local/share/souxmar/plugins`; it works on every souxmar
  release.
- **Auditable trust path.** Releases are signed with an ed25519
  key whose rotation procedure is documented in
  [ADR-0014](https://github.com/souxmar/souxmar/blob/master/docs/adr/0014-release-signing-key-rotation.md);
  the embedded trust store + the `souxmar update` flow are
  documented in [the updates guide](/guide/updates).

## Status

**Public alpha — `v0.9.0`.** The plugin C ABI is frozen final at
v1.3. The agent tool contract is frozen final at v1 with 18 tools.
The desktop workbench shell scaffolds but the viewport / chat /
inspector wait on the `souxmar-bridge` FFI crate (landing across
Sprint 13). The CLI and Python paths are fully wired.

See the [install guide](/guide/install) for download links + system
requirements.
