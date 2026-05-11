#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Cantilever beam in 20 lines of pysouxmar.

Run:
    SOUXMAR_PLUGINS=./build/dev-python/examples/plugins python examples/cantilever.py
"""

import os
import pathlib
import sys

import pysouxmar as sx

PLUGINS = pathlib.Path(os.environ.get("SOUXMAR_PLUGINS", "./build/dev-python/examples/plugins"))
OUT     = pathlib.Path("cantilever.vtu").resolve()

registry = sx.Registry()
loader   = sx.PluginLoader(registry, sx.version())
plugins  = [loader.load(p) for p in sx.discover_plugins([PLUGINS]).loaded]
print(f"loaded {len(plugins)} plugins; capabilities: {registry.list_capabilities()}")

result = sx.run_pipeline(
    sx.parse_pipeline(f"version: 1\nstages:\n  - {{id: mesh, plugin: mesher.tetra.hello}}\n  - {{id: write, plugin: writer.vtu, input: {{mesh: {{from: mesh}}, path: {OUT}}}}}\n"),
    sx.RegistryDispatcher(registry),
    sx.Cache(),
)
print(f"pipeline {result.status.name}; wrote {OUT}" if result.status == sx.RunStatus.Success else f"FAILED: {result.validation_errors or [s.error.message for s in result.stage_results if s.error]}")
sys.exit(0 if result.status == sx.RunStatus.Success else 1)
