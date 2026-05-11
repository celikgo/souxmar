# Compiler warning flags applied to every souxmar target.
# Targets opt in via `target_link_libraries(target PRIVATE souxmar::warnings)`.

include_guard(GLOBAL)

add_library(souxmar_warnings INTERFACE)
add_library(souxmar::warnings ALIAS souxmar_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(souxmar_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wmisleading-indentation
  )
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(souxmar_warnings INTERFACE
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
      -Wuseless-cast
    )
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(souxmar_warnings INTERFACE
    /W4
    /permissive-
    /w14242  # narrowing conversions
    /w14254  # signed/unsigned mismatch in operator<<
    /w14263  # function does not override
    /w14265  # virtual dtor missing
    /w14287  # unsigned/negative constant mismatch
    /w14296  # expression always true/false
    /w14311  # pointer truncation
    /w14545  # comma operator with no effect
    /w14546  # call with missing arguments
    /w14547  # operator before comma has no effect
    /w14549  # operator before comma has no effect
    /w14555  # expression has no effect
    /w14619  # pragma warning: invalid number
    /w14640  # construction of local static is not thread-safe
    /w14826  # conversion is sign-extended
    /w14905  # wide string literal cast
    /w14906  # string literal cast
    /w14928  # illegal copy-init
  )
  # MSVC defaults can be tightened.
  target_compile_definitions(souxmar_warnings INTERFACE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()

if(SOUXMAR_WERROR)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(souxmar_warnings INTERFACE -Werror)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(souxmar_warnings INTERFACE /WX)
  endif()
endif()
