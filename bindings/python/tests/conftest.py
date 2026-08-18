# SPDX-License-Identifier: Apache-2.0
"""pytest fixtures for pysouxmar.

The Python tests live next to a CMake-built extension. They need to know
where the in-tree example plugins (hello-mesher, vtu-writer) are so they
can pass that path to `discover_plugins`. Resolution order:

1. $SOUXMAR_TEST_PLUGINS_ROOT — set explicitly by the developer.
2. The CMake build dir at ../../build/dev-python/examples/plugins (the
   conventional layout for `cmake --preset dev-python`).
3. ../../build/dev/examples/plugins (the dev preset, which auto-builds
   examples but not pysouxmar).

If none of these exist, plugin-dependent tests are skipped — unit tests
that exercise pure parsing / value conversion still run.
"""

from __future__ import annotations

import os
from pathlib import Path

import pysouxmar as sx
import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]


def _candidate_plugin_roots() -> list[Path]:
    explicit = os.environ.get("SOUXMAR_TEST_PLUGINS_ROOT")
    if explicit:
        return [Path(explicit)]
    return [
        REPO_ROOT / "build" / "dev-python" / "examples" / "plugins",
        REPO_ROOT / "build" / "dev"        / "examples" / "plugins",
    ]


@pytest.fixture(scope="session")
def plugins_root() -> Path:
    for cand in _candidate_plugin_roots():
        if cand.is_dir() and any(cand.iterdir()):
            return cand
    pytest.skip(
        "no built example plugins found; build first with "
        "`cmake --preset dev-python && cmake --build --preset dev-python`"
    )


@pytest.fixture(scope="session")
def cantilever_pipeline() -> Path:
    return REPO_ROOT / "examples" / "cantilever-beam" / "pipeline.yaml"


@pytest.fixture(scope="session")
def loaded_registry(plugins_root):
    """A Registry with the in-tree example plugins loaded.

    Lives here rather than in test_end_to_end.py because two modules use
    it, and pytest only shares fixtures through conftest. It was defined
    in the other module for long enough that test_agent_tools.py's
    docstring claims to "reuse the loaded_registry fixture from
    test_end_to_end.py" — which pytest has never supported, so that test
    errored on collection every time it ran.
    """
    registry = sx.Registry()
    loader   = sx.PluginLoader(registry, sx.version())
    report   = sx.discover_plugins([plugins_root])
    assert report.loaded, (
        f"discovery returned no plugins in {plugins_root}; "
        f"rejected: {[(r.candidate_path, r.reason) for r in report.rejected]}"
    )
    # Hold the LoadedPlugin handles alongside the registry so they outlive
    # every test that uses the fixture.
    handles = [loader.load(p) for p in report.loaded]
    return registry, handles
