# Top-level source aggregator.
# Each subdirectory builds an independent CMake target. Inter-target dependencies
# go through public headers + linked targets only — no path-based includes.

add_subdirectory(core)
add_subdirectory(plugin-host)
add_subdirectory(pipeline)
add_subdirectory(ai)
# Sprint 11 push 1 — shared crypto primitives (ed25519 verify, sha256,
# hex). Must be processed before souxmar/updater so updater can link
# against souxmar::crypto. See ADR-0015.
add_subdirectory(crypto)
# Sprint 10 push 4 — auto-updater (manifest parser + validator now;
# verifier + state machine + rollback land in subsequent pushes).
add_subdirectory(updater)

if(SOUXMAR_BUILD_CLI)
  add_subdirectory(cli)
endif()

# Sprint 4+ subdirectories will be added as they land:
#   add_subdirectory(adapters)      # Sprint 1+
#   add_subdirectory(plugins)       # Sprint 2+
#   add_subdirectory(bindings/python) # Sprint 4
#   add_subdirectory(desktop)       # Sprint 1 (separate build via Tauri)
