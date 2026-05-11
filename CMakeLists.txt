cmake_minimum_required(VERSION 3.25)

# Read version from the canonical VERSION file (single source of truth).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SOUXMAR_VERSION_RAW)
string(STRIP "${SOUXMAR_VERSION_RAW}" SOUXMAR_VERSION)

project(souxmar
  VERSION ${SOUXMAR_VERSION}
  DESCRIPTION "Open-source CAE platform: CAD, mesh, FEM/CFD, post-processing."
  HOMEPAGE_URL "https://souxmar.dev"
  LANGUAGES C CXX
)

# Plugin ABI version is independent of the project version. Pinned at 1 for the
# v1.x series. See docs/PLUGIN_SDK.md and docs/adr/0001-c-abi-for-plugins.md.
set(SOUXMAR_ABI_VERSION 1 CACHE STRING "souxmar plugin ABI major version" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Discoverable IDE support, position-independent everywhere (plugins).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Use folder grouping in IDE generators.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CTest)
include(SouxmarOptions)
include(SouxmarWarnings)
include(SouxmarSanitizers)

# Public include search path. Generated headers also land here under build/.
add_library(souxmar_public_headers INTERFACE)
target_include_directories(souxmar_public_headers
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
add_library(souxmar::public_headers ALIAS souxmar_public_headers)

# Generated build_config.h carries version + build-time flags.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/souxmar/build_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/include/souxmar/build_config.h"
  @ONLY
)

add_subdirectory(src)

if(BUILD_TESTING AND SOUXMAR_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(SOUXMAR_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SOUXMAR_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# Friendly summary so configure-time decisions are visible at a glance.
message(STATUS "")
message(STATUS "souxmar configuration")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  ABI version:       ${SOUXMAR_ABI_VERSION}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests:             ${SOUXMAR_BUILD_TESTS}")
message(STATUS "  Examples:          ${SOUXMAR_BUILD_EXAMPLES}")
message(STATUS "  Benchmarks:        ${SOUXMAR_BUILD_BENCHMARKS}")
message(STATUS "  CLI:               ${SOUXMAR_BUILD_CLI}")
message(STATUS "  Python:            ${SOUXMAR_BUILD_PYTHON}")
message(STATUS "  ASan / TSan / UBSan: ${SOUXMAR_ENABLE_ASAN} / ${SOUXMAR_ENABLE_TSAN} / ${SOUXMAR_ENABLE_UBSAN}")
message(STATUS "  Adapters: OCCT=${SOUXMAR_WITH_OPENCASCADE} Gmsh=${SOUXMAR_WITH_GMSH} VTK=${SOUXMAR_WITH_VTK}")
message(STATUS "")
