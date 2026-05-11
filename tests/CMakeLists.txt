# Test root. Each subdirectory is an independent test binary registered with CTest.

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_subdirectory(unit)

# Sprint 1+ test directories:
#   add_subdirectory(integration)        # Sprint 2+
#   add_subdirectory(plugin-conformance) # Sprint 5
#   add_subdirectory(validation)         # Sprint 3+
#   add_subdirectory(determinism)        # Sprint 5
