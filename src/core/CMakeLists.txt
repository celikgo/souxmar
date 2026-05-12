# libsouxmar-core — the in-process data model and pipeline orchestrator.
# Sprint 1 lands the data model: Geometry, Topology, Mesh, Field. The pipeline
# orchestrator and LinearAlgebra abstraction land in Sprints 3 and 5.

add_library(souxmar_core
  version.cpp
  geometry.cpp
  topology.cpp
  mesh.cpp
  field.cpp
  # C ABI bridges (souxmar_mesh_*, souxmar_geometry_*, souxmar_field_*).
  # Live in core because that's where the implementation types are defined.
  c_abi_mesh.cpp
  c_abi_geometry.cpp
  c_abi_field.cpp
  # Sprint 5 push 4 — bulk-buffer ABI for large mesh transfer (ADR-0006).
  c_abi_buffer.cpp
  # Sprint 6 push 1 — mesh-quality metrics (Tet4 + Tri3 in v1).
  mesh_quality.cpp
  # Per-element-type local face tables — centralised host-side helper
  # consumed by surface_stream (and future host-side viz / BC helpers).
  # ADR-0012's pre-mortem named this centralisation as a follow-up.
  face_topology.cpp
  # Post-v1.0 block (Sprints 25-32) — ABI scaffold for the six minor
  # ratchets v1.4-v1.9. The .cpp files marked "stub" return
  # SOUXMAR_E_NOT_IMPLEMENTED; per-sprint PR 2 of each sprint replaces
  # each stub with the real backing.
  surface_stream.cpp        # v1.4 — real impl (S25 PR 1)
  c_abi_surface_stream.cpp  # v1.4 — ADR-0037, RFC-001 (S25)
  field_stream.cpp          # v1.5 — real impl (S27 PR 1)
  c_abi_field_stream.cpp    # v1.5 — ADR-0038, RFC-002 (S27)
  c_abi_brep.cpp            # v1.6 + v1.8 — ADRs 0039 / 0041   [stub]
  c_abi_sketch.cpp          # v1.7 — ADR-0040, RFC-004 (S29)   [stub]
  time_series.cpp           # v1.9 — real LRU cache (S32 PR 1 partial)
  pvd.cpp                   # v1.9 — minimal PVD parser (S32 PR 1 partial)
  c_abi_timeseries.cpp      # v1.9 — ADR-0042, RFC-006 (S32) — _open still
                            #         stubbed pending reader plugin (S32 PR 2)
)
add_library(souxmar::core ALIAS souxmar_core)

set_target_properties(souxmar_core PROPERTIES
  OUTPUT_NAME    souxmar-core
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    core
)

target_link_libraries(souxmar_core
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
)

# Generated build_config.h is consumed only inside this TU.
target_include_directories(souxmar_core
  PRIVATE
    ${CMAKE_BINARY_DIR}/generated/include
)

install(TARGETS souxmar_core
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
