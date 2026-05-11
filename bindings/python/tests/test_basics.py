# SPDX-License-Identifier: Apache-2.0
"""Pure-Python unit tests — exercise the binding surface without needing
any built plugins. These tests validate:

* version + abi_version round-trip
* pipeline parsing (good + error paths)
* Value tree ↔ Python conversion symmetry through Stage.input
* StageRef sugar via {'from': 'id'} dict shorthand
"""

from __future__ import annotations

import re

import pytest

import pysouxmar as sx


def test_version_string_is_semverish():
    v = sx.version()
    assert isinstance(v, str)
    assert re.match(r"^\d+\.\d+\.\d+$", v), f"unexpected version format: {v!r}"


def test_version_tuple_matches_string():
    s = sx.version()
    parts = tuple(int(p) for p in s.split("."))
    assert sx.version_tuple() == parts


def test_abi_version_is_one():
    # ABI v1 is frozen for the entire 1.x series; if this changes the
    # ABI itself broke compatibility — alarm.
    assert sx.abi_version() == 1


def test_parse_pipeline_yaml_smoke():
    yaml_src = """
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
    input:
      target_size: 0.05
  - id: write
    plugin: writer.vtu
    input:
      mesh: { from: mesh }
      path: out.vtu
"""
    p = sx.parse_pipeline(yaml_src)
    assert isinstance(p, sx.Pipeline)
    assert p.version == 1
    assert len(p) == 2
    mesh, write = p.stages
    assert mesh.id == "mesh"
    assert mesh.plugin == "mesher.tetra.hello"
    assert mesh.input == {"target_size": 0.05}

    assert write.id == "write"
    assert write.plugin == "writer.vtu"
    assert write.input["path"] == "out.vtu"
    # The {from: stage} shorthand parses into a StageRef.
    assert isinstance(write.input["mesh"], sx.StageRef)
    assert write.input["mesh"].stage_id == "mesh"


def test_parse_pipeline_rejects_garbage():
    with pytest.raises(ValueError):
        sx.parse_pipeline("this is not a pipeline file")


def test_parse_pipeline_rejects_unknown_version():
    with pytest.raises(ValueError):
        sx.parse_pipeline("version: 99\nstages: []\n")


def test_stage_ref_constructable_from_python():
    r = sx.StageRef("up")
    assert r.stage_id == "up"
    assert "up" in repr(r)


def test_registry_starts_empty():
    r = sx.Registry()
    assert len(r) == 0
    assert r.list_capabilities() == []
    assert r.list_capabilities_in_namespace("mesher.") == []


def test_disk_cache_default_dir_round_trips_override(tmp_path):
    chosen = sx.DiskCache.default_dir(tmp_path)
    assert chosen == tmp_path


def test_disk_cache_default_dir_no_override_returns_a_path():
    p = sx.DiskCache.default_dir()
    # We only assert the contract — a non-empty platform-appropriate path.
    assert p is not None
    assert str(p) != ""


def test_run_options_defaults():
    o = sx.RunOptions()
    assert o.use_cache is True
    assert o.stop_on_first_failure is True
    assert o.disk_cache_dir is None
