# swap-mesher

This example documents the **mesher swap-test** — Sprint 6's exit criterion for the second mesher landing:

> A user can swap `mesher.tetra.grid` for `mesher.tetra.gmsh` in pipeline YAML with no other changes; same result format.

The two pipeline YAML files here differ in exactly one line:

```diff
-    plugin: mesher.tetra.grid
+    plugin: mesher.tetra.gmsh
```

Everything else — the upstream geometry stage, the downstream postproc + write stages, the input keys, the result schema — is identical.

## What's runnable today

Both YAMLs reference `reader.step` for the geometry source, which is the **opt-in OCCT-backed reader** from Sprint 6 push 4 (`-DSOUXMAR_WITH_OPENCASCADE=ON`). Running them end-to-end requires OCCT on your machine; the gmsh variant additionally requires Gmsh.

```bash
# Always-on grid mesher, OCCT geometry source.
cmake --preset dev -DSOUXMAR_BUILD_EXAMPLES=ON -DSOUXMAR_WITH_OPENCASCADE=ON
cmake --build --preset dev
souxmar --plugin-path <build>/examples/plugins run grid.yaml

# Real Gmsh tetrahedralisation — same pipeline shape, swap one line.
cmake --preset dev -DSOUXMAR_BUILD_EXAMPLES=ON \
    -DSOUXMAR_WITH_OPENCASCADE=ON -DSOUXMAR_WITH_GMSH=ON
cmake --build --preset dev
souxmar --plugin-path <build>/examples/plugins run gmsh.yaml
```

The default-CI matrix doesn't ship OCCT or Gmsh, so this directory's gate test lives in `tests/integration/test_swap_mesher.cpp` — it builds a Geometry programmatically and exercises grid-mesher (always-on) end-to-end against the dispatcher. The opt-in gmsh path is documented but exercised only on nightly Gmsh-bearing runners.

## Why this matters

The whole point of the `mesher.tetra.*` namespace is that the dispatcher's contract is identical regardless of who implements it. Plugin authors get to compete on quality, locality (a corporate fork's in-house mesher), or licensing (Gmsh is GPL — see [`docs/GOVERNANCE.md`](../../docs/GOVERNANCE.md) on the process-isolation policy). Downstream stages — solvers, post-processors, writers — see the same `Mesh` regardless of which mesher produced it. That's the abstraction working.

## Files

- `grid.yaml` — grid mesher (always-on) over an OCCT-read STEP body.
- `gmsh.yaml` — Gmsh mesher (opt-in) over the same body; the one-line diff vs grid.yaml is the entire point of this example.
- `cube.step` — a tiny STEP body. **Not committed to the repo** because committing binary CAD fixtures wars with the docs/GOVERNANCE.md repo-hygiene rules; the README points at the OCCT documentation for generating one.
