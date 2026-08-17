souxmar_add_plugin(marine_loads
  SOURCES   marine_loads.cpp marine_integrity.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.marine.hydrostatic"
               "solver.marine.hull_collapse"
               "solver.marine.corrosion"
               "writer.marine.qualification_report"
)

if(APPLE)
  set_target_properties(marine_loads PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(marine_loads PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(marine_loads PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "marine_loads")
endif()

set(SOUXMAR_MARINE_LOADS_DIR "$<TARGET_FILE_DIR:marine_loads>"
    CACHE INTERNAL "Built marine plugin directory")
