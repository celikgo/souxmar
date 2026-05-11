# SPDX-License-Identifier: Apache-2.0
"""pysouxmar — Python bindings for the souxmar CAE platform.

The native extension `_pysouxmar` exposes the parser, plugin host, registry,
runner, and cache. This module re-exports the public names and adds a few
small Python-side helpers that would feel awkward in C++.

Quick start::

    import pysouxmar as sx

    pipeline = sx.parse_pipeline_file("cantilever.yaml")
    registry = sx.Registry()
    loader   = sx.PluginLoader(registry, sx.version())
    for plugin in sx.discover_plugins(["./build/dev/examples/plugins"]):
        loader.load(plugin)

    cache      = sx.Cache()
    dispatcher = sx.RegistryDispatcher(registry)
    result     = sx.run_pipeline(pipeline, dispatcher, cache)
    assert result.status == sx.RunStatus.Success

The C++-side ABI is independent of the Python package version. Check
`sx.abi_version()` if you need to negotiate plugin compatibility.
"""

from __future__ import annotations

from . import _pysouxmar as _ext

# Re-export the C++-side names so users can `from pysouxmar import X`.
version       = _ext.version
version_tuple = _ext.version_tuple
abi_version   = _ext.abi_version

# Pipeline + parser
parse_pipeline      = _ext.parse_pipeline
parse_pipeline_file = _ext.parse_pipeline_file
Pipeline            = _ext.Pipeline
Stage               = _ext.Stage
StageRef            = _ext.StageRef
# ParseError / LoadError surface as ValueError on the Python side. The
# names are exposed as aliases for documentation; `except ParseError:`
# behaves identically to `except ValueError:`.
ParseError          = ValueError
LoadError           = ValueError

# Plugin host
discover_plugins     = _ext.discover_plugins
default_search_paths = _ext.default_search_paths
DiscoveryOptions     = _ext.DiscoveryOptions
DiscoveredPlugin     = _ext.DiscoveredPlugin
DiscoveryRejection   = _ext.DiscoveryRejection
DiscoveryReport      = _ext.DiscoveryReport
Manifest             = _ext.Manifest
ThreadingModel       = _ext.ThreadingModel
Registry             = _ext.Registry
PluginLoader         = _ext.PluginLoader
LoadedPlugin         = _ext.LoadedPlugin
CapabilityKind       = _ext.CapabilityKind

# Cache + runner
Cache              = _ext.Cache
DiskCache          = _ext.DiskCache
ContentHash        = _ext.ContentHash
RegistryDispatcher = _ext.RegistryDispatcher
RunOptions         = _ext.RunOptions
RunResult          = _ext.RunResult
RunStatus          = _ext.RunStatus
StageRunResult     = _ext.StageRunResult
StageStatus        = _ext.StageStatus
DispatchError      = _ext.DispatchError
run_pipeline       = _ext.run_pipeline

# YAML helpers (Value ↔ Python).
parse_value_yaml = _ext.parse_value_yaml
emit_value_yaml  = _ext.emit_value_yaml

# Agent tool surface.
ai = _ext.ai

__all__ = [
    "version", "version_tuple", "abi_version",
    "parse_pipeline", "parse_pipeline_file",
    "Pipeline", "Stage", "StageRef", "ParseError",
    "discover_plugins", "default_search_paths",
    "DiscoveryOptions", "DiscoveredPlugin", "DiscoveryRejection",
    "DiscoveryReport", "Manifest", "ThreadingModel",
    "Registry", "PluginLoader", "LoadedPlugin", "LoadError",
    "CapabilityKind",
    "Cache", "DiskCache", "ContentHash",
    "RegistryDispatcher", "RunOptions", "RunResult", "RunStatus",
    "StageRunResult", "StageStatus", "DispatchError",
    "run_pipeline",
    "parse_value_yaml", "emit_value_yaml",
    "ai",
]
