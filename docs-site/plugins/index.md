# Plugins

souxmar's plugin model is the contract third-party meshers,
solvers, readers, writers, and post-processors author against.
The ABI is **frozen final at v1.3** for the entire 1.x release
series.

## What plugins can do

A plugin claims one or more capabilities from the namespaces:

- `reader.*` — geometry sources (STEP, OBJ, STL, .blend, …)
- `mesher.*` — mesh generators (`mesher.tetra.hello`, `mesher.tetra.grid`, `mesher.tetra.gmsh`, …)
- `solver.*` — physics solvers (`solver.heat.linear`, `solver.elasticity.linear`, OpenFOAM, FEniCSx, …)
- `writer.*` — output formats (VTU, gmsh, custom)
- `postproc.*` — derived fields (mesh quality, scalar magnitude, custom)

The pipeline runner dispatches on the capability name; swapping
`mesher.tetra.grid` for `mesher.tetra.gmsh` is a one-line change.

## The ABI

C ABI (`include/souxmar-c/`). C is the lingua franca — plugins can
be written in any language with a C FFI (C, C++, Rust, Zig, …). The
ABI is documented in
[`PLUGIN_SDK.md`](https://github.com/souxmar/souxmar/blob/master/docs/PLUGIN_SDK.md);
the canonical entry-point macro is `souxmar_plugin_register_v1`.

Three ratchets across the v1 line:

- v1.0 → v1.1: `reader.*` capability namespace added (Sprint 6 push 4).
- v1.1 → v1.2: mmap-buffer ingest path added (Sprint 7 push 3).
- v1.2 → v1.3: per-face tags on `Mesh` (Sprint 9 push 2,
  [ADR-0012](https://github.com/souxmar/souxmar/blob/master/docs/adr/0012-per-face-tag-c-abi-ratchet.md)).

All ratchets are **additive minor surfaces** per
[ADR-0008](https://github.com/souxmar/souxmar/blob/master/docs/adr/0008-abi-v1-final-freeze.md);
no symbol has ever been removed or changed semantically. A v1.0
plugin runs on every v1.x release.

## Conformance

`souxmar-conformance <plugin-dir>` runs a battery of capability-
specific contract tests against your plugin. The marketplace
surfaces a green badge for plugins that pass.

Categories:

| Capability     | What conformance checks                                                  |
| -------------- | ------------------------------------------------------------------------ |
| `mesher.*`     | round-trip vertex/cell tables; non-zero element count; non-degenerate cells |
| `solver.*`     | linearity (zero input → zero output); patch test on canonical geometry  |
| `writer.*`     | byte-deterministic output across runs; readable by VTK / Gmsh / matching reader |
| `reader.*`     | identity round-trip against an in-tree fixture                          |
| `postproc.*`   | per-cell array shape matches the parent mesh's `num_cells`              |

The full suite + the "how to write a conforming X" recipes live
in [authoring](/plugins/first-plugin).

## Marketplace

The plugin index at
[`docs/plugin-index.toml`](https://github.com/souxmar/souxmar/blob/master/docs/plugin-index.toml)
is the canonical list. Adding a plugin is a PR against that file;
the CI workflow runs `souxmar-conformance` against your published
binary on each of the four CI platforms (Linux / macOS x86_64 /
macOS arm64 / Windows).

Sprint 16 adds the **paid** marketplace tier: plugin authors can
publish under a per-license-key model with Stripe integration. The
ABI contract is identical between free + paid plugins; the
difference is the distribution channel.

## Free vs paid plugin contract

| Concern               | Free                                | Paid (Sprint 16+)                 |
| --------------------- | ----------------------------------- | --------------------------------- |
| Index entry           | PR against `docs/plugin-index.toml` | Marketplace dashboard upload      |
| Conformance gate      | CI runs on every index PR           | Same suite + signing-key gate     |
| Distribution          | Linked from the index               | Hosted on the marketplace CDN     |
| Payments              | n/a                                 | Stripe; 90/10 split (author/souxmar) |
| Sig requirement       | optional (free badge)               | required (ed25519 per author key) |

See [the marketplace page](/plugins/marketplace) for details.
