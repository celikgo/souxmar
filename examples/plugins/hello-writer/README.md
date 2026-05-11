# hello-writer

Minimal writer reference plugin. Reads the upstream Mesh through the C ABI accessors and writes a two-line text summary to the file path supplied via the `path` input:

```
num_nodes=<N>
num_cells=<M>
```

Used by the integration test in `tests/integration/test_pipeline_end_to_end.cpp` to prove the full pipeline (manifest → discovery → load → register → orchestrate → C ABI dispatch → write) works end-to-end.

A production VTU writer (`writer.vtu`) ships with the VTK adapter in a later sprint; this plugin's purpose is to verify the writer vtable and the value-bag input contract, not to produce a publication-ready file.
