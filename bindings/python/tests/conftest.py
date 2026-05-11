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
