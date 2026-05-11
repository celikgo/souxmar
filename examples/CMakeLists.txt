# Examples build only when SOUXMAR_BUILD_EXAMPLES=ON.
# Plugin examples use the souxmar_add_plugin CMake macro.

include(SouxmarPlugin)

add_subdirectory(plugins/hello-mesher)
add_subdirectory(plugins/hello-writer)
add_subdirectory(plugins/vtu-writer)
add_subdirectory(plugins/heat-solver)
add_subdirectory(plugins/scalar-magnitude)
add_subdirectory(plugins/mesh-quality)
# Sprint 6 push 4 — reader.* C ABI surface (always-on STL reader).
add_subdirectory(plugins/stl-reader)
# Sprint 6 push 4 — opt-in OCCT-backed STEP reader. The option is
# defined in cmake/SouxmarOptions.cmake; the plugin's own CMakeLists
# additionally calls find_package(OpenCASCADE) and silently skips the
# target if OCCT isn't installed, so flipping the option on a system
# without OCCT produces a clear "OCCT not found" message rather than a
# linker-time failure.
if(SOUXMAR_WITH_OPENCASCADE)
  add_subdirectory(plugins/occt-reader)
endif()
# Sprint 6 push 5 — second tetrahedral mesher proving the swap-test
# contract. Always-on grid-mesher; opt-in Gmsh-backed counterpart.
add_subdirectory(plugins/grid-mesher)
if(SOUXMAR_WITH_GMSH)
  add_subdirectory(plugins/gmsh-mesher)
endif()
# Sprint 7 push 2 — second solver. Always-on elasticity-stub (closed
# form); opt-in DOLFINx-backed fenicsx-solver.
add_subdirectory(plugins/elasticity-stub)
if(SOUXMAR_WITH_FENICSX)
  add_subdirectory(plugins/fenicsx-solver)
endif()
# Sprint 8 push 2 — CFD. Always-on cfd-stub (closed form);
# opt-in OpenFOAM subprocess adapter per ADR-0009.
add_subdirectory(plugins/cfd-stub)
if(SOUXMAR_WITH_OPENFOAM)
  add_subdirectory(plugins/openfoam-solver)
endif()
# Sprint 8 push 3 — concept geometry readers. Always-on obj-reader
# (Wavefront OBJ → Tri3); opt-in blender-reader (subprocess invocation
# of `blender -b --python-expr ...` exports the .blend to OBJ first).
# Blender is GPL-2.0+, so the same ADR-0009 process-isolation rule
# the OpenFOAM adapter follows applies to the blender adapter too.
add_subdirectory(plugins/obj-reader)
if(SOUXMAR_WITH_BLENDER)
  add_subdirectory(plugins/blender-reader)
endif()
