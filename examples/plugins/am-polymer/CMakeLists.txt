souxmar_add_plugin(am_polymer
  SOURCES   am_polymer.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.am.polymer.fff" "postproc.am.bond_strength"
)

if(APPLE)
  set_target_properties(am_polymer PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_polymer PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_polymer PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_polymer")
endif()

set(SOUXMAR_AM_POLYMER_DIR "$<TARGET_FILE_DIR:am_polymer>"
    CACHE INTERNAL "Built am-polymer plugin directory")
