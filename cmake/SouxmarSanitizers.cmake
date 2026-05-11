# Sanitiser configuration. Targets opt in by linking `souxmar::sanitizers`.
# Driven by SOUXMAR_ENABLE_ASAN / TSAN / UBSAN / COVERAGE in SouxmarOptions.

include_guard(GLOBAL)

add_library(souxmar_sanitizers INTERFACE)
add_library(souxmar::sanitizers ALIAS souxmar_sanitizers)

set(_souxmar_san_flags "")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  if(SOUXMAR_ENABLE_ASAN)
    list(APPEND _souxmar_san_flags -fsanitize=address -fno-omit-frame-pointer)
  endif()
  if(SOUXMAR_ENABLE_TSAN)
    list(APPEND _souxmar_san_flags -fsanitize=thread -fno-omit-frame-pointer)
  endif()
  if(SOUXMAR_ENABLE_UBSAN)
    list(APPEND _souxmar_san_flags -fsanitize=undefined -fno-sanitize-recover=undefined)
  endif()
  if(_souxmar_san_flags)
    target_compile_options(souxmar_sanitizers INTERFACE ${_souxmar_san_flags})
    target_link_options(souxmar_sanitizers    INTERFACE ${_souxmar_san_flags})
  endif()

  if(SOUXMAR_ENABLE_COVERAGE)
    target_compile_options(souxmar_sanitizers INTERFACE --coverage -O0 -g)
    target_link_options(souxmar_sanitizers    INTERFACE --coverage)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  if(SOUXMAR_ENABLE_ASAN)
    target_compile_options(souxmar_sanitizers INTERFACE /fsanitize=address)
  endif()
  if(SOUXMAR_ENABLE_TSAN OR SOUXMAR_ENABLE_UBSAN)
    message(WARNING "TSAN/UBSAN are not supported with MSVC; ignoring.")
  endif()
endif()
