souxmar_add_plugin(fem_elasticity
  SOURCES   fem_elasticity.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.elasticity.fem"
)

# Floating-point contraction off, for this target only.
#
# The determinism gate requires byte-identical output on Linux, macOS and
# Windows. With contraction enabled the compiler may fuse `a*b + c` into a
# single FMA — one rounding instead of two, so a *different* (in fact more
# accurate) result. arm64 has an FMA instruction and takes it; a baseline
# x86-64 build usually does not. That is a macOS-vs-Linux divergence
# manufactured by the optimiser, in a plugin whose whole numerical
# argument (see the determinism note in fem_elasticity.cpp) is that every
# operation is a correctly-rounded IEEE-754 one.
#
# Scoped to this target rather than added to the project-wide flags: the
# closed-form plugins do not iterate, so contraction cannot change their
# iteration counts, and a global flag would be a performance decision taken
# on behalf of code that never asked.
if(MSVC)
  target_compile_options(fem_elasticity PRIVATE /fp:precise)
else()
  target_compile_options(fem_elasticity PRIVATE -ffp-contract=off)
endif()

if(APPLE)
  set_target_properties(fem_elasticity PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(fem_elasticity PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(fem_elasticity PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "fem_elasticity")
endif()

set(SOUXMAR_FEM_ELASTICITY_DIR "$<TARGET_FILE_DIR:fem_elasticity>"
    CACHE INTERNAL "Built fem-elasticity plugin directory")
