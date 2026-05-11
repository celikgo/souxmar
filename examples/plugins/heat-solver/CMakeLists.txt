souxmar_add_plugin(heat_solver
  SOURCES   heat_solver.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.heat.linear"
)

if(APPLE)
  set_target_properties(heat_solver PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(heat_solver PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(heat_solver PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "heat_solver")
endif()

set(SOUXMAR_HEAT_SOLVER_DIR "$<TARGET_FILE_DIR:heat_solver>"
    CACHE INTERNAL "Built heat-solver plugin directory")
