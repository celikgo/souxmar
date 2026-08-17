# libsouxmar-core — the in-process data model and pipeline orchestrator.
# Sprint 1 lands the data model: Geometry, Topology, Mesh, Field. The pipeline
# orchestrator and LinearAlgebra abstraction land in Sprints 3 and 5.

# ---------------------------------------------------------------------------
# souxmar_core_c_abi — the C ABI bridge TUs, as an OBJECT library.
#
# DO NOT fold these back into souxmar_core's source list. The separation is
# load-bearing, and the failure it prevents is silent.
#
# WHY: these TUs implement the *host* side of include/souxmar-c/** — the
# symbols a dlopen()ed plugin resolves against the host process at load time.
# No host code calls most of them; only plugins do, and plugins are linked
# with `-undefined dynamic_lookup` (macOS) / undefined-permissive ELF rules,
# so the reference is invisible to the linker.
#
# When these TUs sit inside a static archive, a linker that pulls archive
# members lazily (all of them do) drops every member nothing in the host
# references. The host binary then ships without those symbols and every
# plugin that touches them dies at dlopen with, e.g.
#   symbol not found in flat namespace '_souxmar_value_as_number'
# That is exactly the bug this structure fixes — see the twin block in
# src/pipeline/CMakeLists.txt, which was the one actually being dropped.
#
# HOW: the objects are (a) linked into souxmar_core so libsouxmar-core.a
# keeps exactly the contents it always had, and (b) re-exported as
# INTERFACE sources via $<TARGET_OBJECTS:>, which lands them directly on the
# link line of every consumer — transitively, so executables that reach core
# through libsouxmar-pipeline get them too. Object files on a link line are
# never dropped, so the symbols survive. The archive members are simply not
# pulled a second time (their symbols are already defined), so there is no
# duplicate-symbol hazard on any platform.
#
# Portability: this is plain CMake target plumbing — identical on Linux,
# macOS and Windows. The Windows caveat is unchanged and lives in
# cmake/SouxmarPlugin.cmake: a Windows plugin cannot resolve against the host
# executable at all without an import library, so exporting the symbols is
# necessary but not yet sufficient there. See also CMAKE_ENABLE_EXPORTS in
# cmake/SouxmarOptions.cmake, which is the ELF half of the same fix.
# ---------------------------------------------------------------------------
add_library(souxmar_core_c_abi OBJECT
  # C ABI bridges (souxmar_mesh_*, souxmar_geometry_*, souxmar_field_*).
  # Live in core because that's where the implementation types are defined.
  c_abi_mesh.cpp
  c_abi_geometry.cpp
  c_abi_field.cpp
  # Sprint 5 push 4 — bulk-buffer ABI for large mesh transfer (ADR-0006).
  c_abi_buffer.cpp
  c_abi_surface_stream.cpp  # v1.4 — ADR-0037, RFC-001 (S25)
  c_abi_field_stream.cpp    # v1.5 — ADR-0038, RFC-002 (S27)
  c_abi_brep.cpp            # v1.6 + v1.8 — ADRs 0039 / 0041   [stub]
  c_abi_sketch.cpp          # v1.7 — ADR-0040, RFC-004 (S29)   [stub]
  c_abi_timeseries.cpp      # v1.9 — ADR-0042, RFC-006 (S32) — _open still
                            #         stubbed pending reader plugin (S32 PR 2)
)
add_library(souxmar::core_c_abi ALIAS souxmar_core_c_abi)

target_link_libraries(souxmar_core_c_abi
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
)

add_library(souxmar_core
  version.cpp
  geometry.cpp
  topology.cpp
  mesh.cpp
  field.cpp
  # Sprint 6 push 1 — mesh-quality metrics (Tet4 + Tri3 in v1).
  mesh_quality.cpp
  # Per-element-type local face tables — centralised host-side helper
  # consumed by surface_stream (and future host-side viz / BC helpers).
  # ADR-0012's pre-mortem named this centralisation as a follow-up.
  face_topology.cpp
  # Post-v1.0 block (Sprints 25-32) — ABI scaffold for the six minor
  # ratchets v1.4-v1.9. The .cpp files marked "stub" return
  # SOUXMAR_E_NOT_IMPLEMENTED; per-sprint PR 2 of each sprint replaces
  # each stub with the real backing. The matching c_abi_*.cpp bridges are
  # in souxmar_core_c_abi above.
  surface_stream.cpp        # v1.4 — real impl (S25 PR 1)
  field_stream.cpp          # v1.5 — real impl (S27 PR 1)
  time_series.cpp           # v1.9 — real LRU cache (S32 PR 1 partial)
  pvd.cpp                   # v1.9 — minimal PVD parser (S32 PR 1 partial)
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
    # Objects land in libsouxmar-core.a, so the archive is byte-for-byte the
    # same set of members it was before the OBJECT-library split.
    souxmar_core_c_abi
)

# ...and land directly on the link line of every consumer that produces a
# loadable image, so a lazy archive scan cannot drop them. Object files on a
# link line are always kept.
#
# The two genex guards are both deliberate:
#   * $<BUILD_INTERFACE:> — build-tree only. An *installed*
#     libsouxmar-core.a already contains these members, and handing
#     build-tree .o paths to an installed consumer would be broken.
#   * SOUXMAR_C_ABI_LOADABLE_CONSUMER — skip archive-producing consumers.
#     $<TARGET_PROPERTY:TYPE> in a usage requirement names the *consuming*
#     target, so libsouxmar-pipeline.a / -ai.a / -c-bridge.a keep exactly the
#     members they had before this split, while executables (and shared /
#     module libraries, which can host plugins too) get the objects. Without
#     the guard the same TU would be duplicated into half a dozen archives —
#     harmless with lazy archive scanning, a duplicate-symbol error the day
#     someone adds --whole-archive.
# Read the block at the top of this file before touching either.
target_sources(souxmar_core
  INTERFACE
    $<BUILD_INTERFACE:$<${SOUXMAR_C_ABI_LOADABLE_CONSUMER}:$<TARGET_OBJECTS:souxmar_core_c_abi>>>
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
