# Top-level source aggregator.
# Each subdirectory builds an independent CMake target. Inter-target dependencies
# go through public headers + linked targets only — no path-based includes.

add_subdirectory(core)
add_subdirectory(plugin-host)
add_subdirectory(pipeline)

# Sprint 3+ subdirectories will be added as they land:
#   add_subdirectory(adapters)      # Sprint 1+
#   add_subdirectory(plugins)       # Sprint 2+
#   add_subdirectory(cli)           # Sprint 3
#   add_subdirectory(bindings/python) # Sprint 4
#   add_subdirectory(ai)            # Sprint 4+
#   add_subdirectory(desktop)       # Sprint 1 (separate build via Tauri)
