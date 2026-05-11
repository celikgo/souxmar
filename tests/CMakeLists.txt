# Test root. Each subdirectory is an independent test binary registered with CTest.

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_subdirectory(unit)

# Integration tests run only when the example plugins are built (since they
# load real plugin binaries). The top-level CMakeLists auto-enables examples
# whenever tests are on, so this is normally true; gate it anyway in case
# downstream callers turn examples off explicitly.
if(SOUXMAR_BUILD_EXAMPLES AND TARGET hello_mesher)
  add_subdirectory(integration)
endif()

# Sprint 3+ test directories:
#   add_subdirectory(plugin-conformance) # Sprint 5
#   add_subdirectory(validation)         # Sprint 3+
#   add_subdirectory(determinism)        # Sprint 5
