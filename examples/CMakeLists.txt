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
