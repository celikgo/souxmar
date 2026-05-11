# SPDX-License-Identifier: Apache-2.0
"""End-to-end pipeline runs from Python.

These tests need the in-tree example plugins (`hello-mesher`, `vtu-writer`)
to be built — see conftest.py for resolution. They are skipped if no built
plugins are found, so a `pip install` of pysouxmar in a clean checkout
still passes the unit tests above without manual setup.
"""

from __future__ import annotations

from pathlib import Path

import pytest

import pysouxmar as sx


@pytest.fixture
def loaded_registry(plugins_root):
    registry = sx.Registry()
    loader   = sx.PluginLoader(registry, sx.version())
    report   = sx.discover_plugins([plugins_root])
    assert report.loaded, (
        f"discovery returned no plugins in {plugins_root}; "
        f"rejected: {[(r.candidate_path, r.reason) for r in report.rejected]}"
    )
    # Hold the LoadedPlugin handles in a list bound to the registry's
    # lifetime so they outlive every test that uses the fixture.
    handles = [loader.load(p) for p in report.loaded]
    return registry, handles


def test_discovery_finds_the_in_tree_plugins(plugins_root):
    report = sx.discover_plugins([plugins_root])
    ids = {d.manifest.id for d in report.loaded}
    assert "dev.souxmar.examples.hello-mesher" in ids
    assert "dev.souxmar.examples.vtu-writer" in ids


def test_load_registers_capabilities(loaded_registry):
    registry, _handles = loaded_registry
    caps = set(registry.list_capabilities())
    assert "mesher.tetra.hello" in caps
    assert "writer.vtu" in caps
    assert registry.list_capabilities_in_namespace("writer.") == ["writer.vtu"]


def test_run_pipeline_writes_vtu(tmp_path, loaded_registry):
    registry, _handles = loaded_registry

    # Write the pipeline YAML to a tmp dir so the relative `path:` resolves
    # to a tmp output we can inspect + clean up automatically.
    out = tmp_path / "cantilever.vtu"
    pipeline_text = f"""
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
  - id: write
    plugin: writer.vtu
    input:
      mesh: {{ from: mesh }}
      path: {out}
"""
    pipeline = sx.parse_pipeline(pipeline_text)
    cache    = sx.Cache()
    dispatcher = sx.RegistryDispatcher(registry)
    result = sx.run_pipeline(pipeline, dispatcher, cache)

    assert result.status == sx.RunStatus.Success, (
        f"validation_errors={result.validation_errors}, "
        f"failures={[(s.stage_id, s.error.message if s.error else None) for s in result.stage_results if s.error]}"
    )
    assert {s.stage_id for s in result.stage_results} == {"mesh", "write"}
    for s in result.stage_results:
        assert s.status == sx.StageStatus.Executed

    # The writer's stage output exposes its on-disk path.
    write_out = result.outputs["write"]
    assert write_out["kind"] == "path"
    assert Path(write_out["path"]) == out

    assert out.exists()
    contents = out.read_text()
    assert "<VTKFile" in contents
    assert 'NumberOfPoints="4"' in contents
    assert 'NumberOfCells="1"'  in contents


def test_disk_cache_hit_on_rerun(tmp_path, loaded_registry):
    """With disk_cache_dir set, the writer's Path output round-trips
    across separate Cache instances — proves the on-disk hash → blob
    layer wires up from Python the same way it does from the C++ runner."""
    registry, _handles = loaded_registry

    out = tmp_path / "cached.vtu"
    cache_dir = tmp_path / "souxmar-cache"
    pipeline_text = f"""
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
  - id: write
    plugin: writer.vtu
    input:
      mesh: {{ from: mesh }}
      path: {out}
"""
    pipeline   = sx.parse_pipeline(pipeline_text)
    dispatcher = sx.RegistryDispatcher(registry)

    opts = sx.RunOptions()
    opts.disk_cache_dir = cache_dir

    # Run 1: warm the cache.
    cache1 = sx.Cache()
    r1 = sx.run_pipeline(pipeline, dispatcher, cache1, opts)
    assert r1.status == sx.RunStatus.Success
    write1 = next(s for s in r1.stage_results if s.stage_id == "write")
    assert write1.status == sx.StageStatus.Executed

    # Run 2 with a fresh in-memory cache — disk should serve the writer.
    cache2 = sx.Cache()
    r2 = sx.run_pipeline(pipeline, dispatcher, cache2, opts)
    assert r2.status == sx.RunStatus.Success
    write2 = next(s for s in r2.stage_results if s.stage_id == "write")
    assert write2.status == sx.StageStatus.Cached, (
        f"expected writer to hit disk cache, got {write2.status}"
    )


def test_parallel_run_succeeds(tmp_path, loaded_registry):
    """Run the same pipeline with max_workers > 1. Real plugins for the
    in-tree mesher / writer are SingleThreaded — the parallel runner serialises
    each plugin's stages, but the pipeline still completes successfully."""
    registry, _handles = loaded_registry

    out = tmp_path / "parallel.vtu"
    pipeline_text = f"""
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
  - id: write
    plugin: writer.vtu
    input:
      mesh: {{ from: mesh }}
      path: {out}
"""
    pipeline   = sx.parse_pipeline(pipeline_text)
    dispatcher = sx.RegistryDispatcher(registry)
    cache      = sx.Cache()
    opts       = sx.RunOptions()
    opts.max_workers = 4

    result = sx.run_pipeline(pipeline, dispatcher, cache, opts)
    assert result.status == sx.RunStatus.Success, (
        f"validation_errors={result.validation_errors}, "
        f"failures={[(s.stage_id, s.error.message if s.error else None) for s in result.stage_results if s.error]}"
    )
    assert out.exists()
    # stage_results stays in declaration order regardless of completion order.
    assert [s.stage_id for s in result.stage_results] == ["mesh", "write"]


def test_dispatch_failure_surfaces_message(loaded_registry):
    """A pipeline that names a non-existent capability fails cleanly;
    the StageRunResult carries the dispatcher's error message."""
    registry, _handles = loaded_registry
    pipeline = sx.parse_pipeline("""
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.does-not-exist
""")
    dispatcher = sx.RegistryDispatcher(registry)
    result = sx.run_pipeline(pipeline, dispatcher, sx.Cache())
    assert result.status == sx.RunStatus.StageFailed
    assert len(result.stage_results) == 1
    sr = result.stage_results[0]
    assert sr.status == sx.StageStatus.Failed
    assert sr.error is not None
    assert "does-not-exist" in sr.error.message
