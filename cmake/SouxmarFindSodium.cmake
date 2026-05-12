# SouxmarFindSodium — locate libsodium across packaging worlds.
#
# Defines `souxmar::sodium` as an INTERFACE target that downstream
# CMakeLists can link unconditionally. Resolution order:
#
#   1. find_package(unofficial-sodium CONFIG) — the vcpkg port. This
#      is what `cmake --preset dev` (and CI) sees, and what
#      docs/CONTRIBUTING.md tells contributors to use.
#   2. pkg_check_modules(libsodium) — Homebrew, Debian/Ubuntu,
#      Arch, and most package managers expose libsodium this way.
#   3. FATAL_ERROR with an actionable install hint.
#
# Both consumers (src/crypto, src/updater) link `souxmar::sodium`
# instead of naming the upstream target directly, so the resolution
# choice is invisible to them — same C ABI either way.

include_guard(GLOBAL)

find_package(unofficial-sodium CONFIG QUIET)

add_library(souxmar_sodium INTERFACE)
add_library(souxmar::sodium ALIAS souxmar_sodium)

if(TARGET unofficial-sodium::sodium)
  target_link_libraries(souxmar_sodium INTERFACE unofficial-sodium::sodium)
  message(STATUS "souxmar: libsodium via vcpkg (unofficial-sodium::sodium)")
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(souxmar_sodium_pc QUIET IMPORTED_TARGET libsodium)
  if(souxmar_sodium_pc_FOUND)
    target_link_libraries(souxmar_sodium
      INTERFACE PkgConfig::souxmar_sodium_pc)
    message(STATUS
      "souxmar: libsodium via pkg-config (${souxmar_sodium_pc_VERSION})")
    return()
  endif()
endif()

message(FATAL_ERROR
  "souxmar: libsodium not found. Tried:\n"
  "  * find_package(unofficial-sodium CONFIG)  (the vcpkg port)\n"
  "  * pkg_check_modules(libsodium)            (Homebrew, distro packages)\n"
  "Install via:\n"
  "  Homebrew:       brew install libsodium\n"
  "  Debian/Ubuntu:  apt install libsodium-dev\n"
  "  Arch:           pacman -S libsodium\n"
  "  vcpkg:          configure with --preset dev (sets the toolchain)\n"
  "See docs/CONTRIBUTING.md for the supported dev-environment recipes.")
