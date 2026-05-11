# Gmsh-backed tetrahedral mesher. Built only when SOUXMAR_WITH_GMSH=ON
# AND a usable Gmsh install is present. find_package is QUIET so a
# developer flipping the option without Gmsh installed gets a clean
# STATUS skip rather than a noisy cmake-time failure. CI environments
# that actually rely on Gmsh should run with -DSOUXMAR_WITH_GMSH=ON
# and a populated CMAKE_PREFIX_PATH; the missing-Gmsh case must fail
# loudly in that context — flip the QUIET to REQUIRED on those runners.

find_package(Gmsh QUIET)

if(NOT Gmsh_FOUND)
  message(STATUS
    "souxmar: gmsh-mesher skipped — find_package(Gmsh) didn't find a usable install. "
    "Provide Gmsh_DIR / CMAKE_PREFIX_PATH and reconfigure.")
  return()
endif()

souxmar_add_plugin(gmsh_mesher
  SOURCES   gmsh_mesher.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "mesher.tetra.gmsh"
)

target_link_libraries(gmsh_mesher PRIVATE Gmsh::Gmsh)

if(APPLE)
  set_target_properties(gmsh_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(gmsh_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(gmsh_mesher PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "gmsh_mesher")
endif()

set(SOUXMAR_GMSH_MESHER_DIR "$<TARGET_FILE_DIR:gmsh_mesher>"
    CACHE INTERNAL "Built gmsh-mesher plugin directory")
