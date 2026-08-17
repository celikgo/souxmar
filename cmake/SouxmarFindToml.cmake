# SouxmarFindToml — toml++, pinned to header-only mode.
#
# Why this exists rather than linking `tomlplusplus::tomlplusplus` directly:
# how a distribution packages toml++ changes whether our code can catch its
# exceptions, and getting that wrong is silent.
#
# vcpkg (the project's supported path) ships toml++ header-only. Homebrew ships
# a shared library and its CMake config appends
# `-DTOML_HEADER_ONLY=0 -DTOML_SHARED_LIB=1` to every consumer. In that mode
# toml++'s symbols — including the typeinfo for `toml::parse_error` — are
# hidden inside the dylib, so a `catch (const toml::parse_error&)` in our code
# does not match the object thrown from inside the library. It falls through as
# an unrelated exception: `parse_manifest` stops returning a typed ParseError
# with a line and column, and an uncaught exception escapes `souxmar plugin
# list` on any malformed manifest.
#
# Header-only is toml++'s default and its most portable configuration: the
# implementation compiles into our own translation units, so throw and catch
# sit on the same side of every boundary. We therefore take the package's
# include directory and nothing else.
#
# The call sites keep a `catch (const std::exception&)` fallback so that a
# downstream build which overrides this back to shared-library mode still
# degrades to a clean typed error instead of terminating.

include_guard(GLOBAL)

find_package(tomlplusplus CONFIG REQUIRED)

if(NOT TARGET souxmar::tomlplusplus)
  add_library(souxmar_tomlplusplus INTERFACE)
  add_library(souxmar::tomlplusplus ALIAS souxmar_tomlplusplus)

  get_target_property(_souxmar_toml_includes
    tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
  if(_souxmar_toml_includes)
    # SYSTEM so toml++'s own deprecation warnings do not trip SOUXMAR_WERROR.
    target_include_directories(souxmar_tomlplusplus SYSTEM
      INTERFACE ${_souxmar_toml_includes})
  endif()

  target_compile_definitions(souxmar_tomlplusplus INTERFACE TOML_HEADER_ONLY=1)

  unset(_souxmar_toml_includes)
endif()
