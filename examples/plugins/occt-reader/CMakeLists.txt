# OCCT-backed reader.step / reader.iges plugin. Built only when
# SOUXMAR_WITH_OPENCASCADE=ON AND OpenCASCADE is actually installed.
#
# The find_package call is QUIET so that a developer building with
# SOUXMAR_WITH_OPENCASCADE=ON on a machine without OCCT gets a clean
# "occt-reader skipped: OpenCASCADE not found" message instead of a
# noisy cmake-time failure. Real CI configurations should fail loudly
# instead — flip the requirement to REQUIRED in that environment.

find_package(OpenCASCADE QUIET COMPONENTS
  TKBRep
  TKSTEP
  TKIGES
  TKMath
  TKernel
)

if(NOT OpenCASCADE_FOUND)
  message(STATUS
    "souxmar: occt-reader skipped — find_package(OpenCASCADE) didn't find a usable install. "
    "Provide OpenCASCADE_DIR / CMAKE_PREFIX_PATH and reconfigure.")
  return()
endif()

souxmar_add_plugin(occt_reader
  SOURCES   occt_reader.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "reader.step;reader.iges"
)

target_link_libraries(occt_reader PRIVATE
  TKBRep TKSTEP TKIGES TKMath TKernel
)

if(APPLE)
  set_target_properties(occt_reader PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(occt_reader PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(occt_reader PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "occt_reader")
endif()

set(SOUXMAR_OCCT_READER_DIR "$<TARGET_FILE_DIR:occt_reader>"
    CACHE INTERNAL "Built occt-reader plugin directory")
