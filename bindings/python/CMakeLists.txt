# pysouxmar — pybind11 module wrapping the souxmar C++ libraries.
#
# This file is the single CMakeLists for the Python bindings. It runs in
# two contexts:
#
#   1. In-tree dev build. The top-level souxmar CMakeLists adds this
#      subdirectory when SOUXMAR_BUILD_PYTHON=ON; souxmar::core, ::plugin,
#      ::pipeline are already defined as targets.
#
#   2. Standalone `pip install ./bindings/python`. scikit-build-core
#      starts CMake here with no parent project; we have to bring the
#      souxmar source tree in ourselves and build only the bits the
#      Python module needs (skipping CLI, examples, tests, benchmarks).
#
# Either way the souxmar libraries get linked statically into the
# extension so the resulting wheel is self-contained.

cmake_minimum_required(VERSION 3.25)

# Standalone-mode bootstrap: if no parent project has declared souxmar::core,
# pull in the root CMakeLists with everything but the bindings turned off.
if(NOT TARGET souxmar::core)
  set(_pysx_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  if(NOT EXISTS "${_pysx_repo_root}/CMakeLists.txt")
    message(FATAL_ERROR
      "pysouxmar: cannot locate souxmar source root at ${_pysx_repo_root}. "
      "If you copied bindings/python/ outside the souxmar repo, point "
      "SOUXMAR_SOURCE_DIR at the original tree.")
  endif()

  project(pysouxmar LANGUAGES C CXX)

  # Force everything off except what the binding needs. The user can still
  # override these on the cmake command line if they really want a richer
  # build behind a wheel.
  set(SOUXMAR_BUILD_PYTHON     ON  CACHE BOOL "" FORCE)
  set(SOUXMAR_BUILD_CLI        OFF CACHE BOOL "" FORCE)
  set(SOUXMAR_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
  set(SOUXMAR_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(SOUXMAR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

  # add_subdirectory recurses into the souxmar root, which itself adds the
  # bindings subdir back here — guard with a sentinel to avoid an infinite
  # recursion. The standalone bootstrap simulates a "souxmar host" build
  # whose only consumer is this Python module.
  set(SOUXMAR_PYSOUXMAR_BOOTSTRAPPING ON)
  add_subdirectory("${_pysx_repo_root}" "${CMAKE_CURRENT_BINARY_DIR}/_souxmar_root")
  unset(SOUXMAR_PYSOUXMAR_BOOTSTRAPPING)
  return()  # The recursive include built the binding via the second pass.
endif()

if(DEFINED SOUXMAR_PYSOUXMAR_BOOTSTRAPPING)
  # Second-pass entry — same file, but now souxmar::core is defined. Fall
  # through to the binding-build block below.
endif()

# ---- Binding build (both modes converge here) ---------------------------

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pysouxmar_ext MODULE
  src/pysouxmar.cpp
)

target_link_libraries(pysouxmar_ext
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::warnings
    souxmar::sanitizers
)

set_target_properties(pysouxmar_ext PROPERTIES
  OUTPUT_NAME       _pysouxmar
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET   hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
  FOLDER             "bindings"
)

# Install layout for wheels:
#   <site-packages>/pysouxmar/__init__.py
#   <site-packages>/pysouxmar/_pysouxmar.<ext>
install(TARGETS pysouxmar_ext
  LIBRARY DESTINATION pysouxmar
)
install(DIRECTORY pysouxmar/
  DESTINATION pysouxmar
  FILES_MATCHING PATTERN "*.py"
)

# Dev-build convenience: copy the .py shim next to the built extension so
# in-tree CMake builds can `PYTHONPATH=<build dir> python -c "import pysouxmar"`
# without an intermediate `pip install`.
add_custom_command(TARGET pysouxmar_ext POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/pysouxmar
          $<TARGET_FILE_DIR:pysouxmar_ext>/pysouxmar
  COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:pysouxmar_ext>
          $<TARGET_FILE_DIR:pysouxmar_ext>/pysouxmar/$<TARGET_FILE_NAME:pysouxmar_ext>
  COMMENT "pysouxmar: staging Python package next to the built extension"
  VERBATIM
)

# Expose the staged package directory so the Python integration tests
# can set PYTHONPATH to it without guessing.
set(PYSOUXMAR_PACKAGE_DIR "$<TARGET_FILE_DIR:pysouxmar_ext>"
    CACHE INTERNAL "Built pysouxmar package directory (parent of pysouxmar/)")
