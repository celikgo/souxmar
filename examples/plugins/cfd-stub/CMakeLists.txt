souxmar_add_plugin(cfd_stub
  SOURCES   cfd_stub.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.cfd.simple"
)

if(APPLE)
  set_target_properties(cfd_stub PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(cfd_stub PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(cfd_stub PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "cfd_stub")
endif()

set(SOUXMAR_CFD_STUB_DIR "$<TARGET_FILE_DIR:cfd_stub>"
    CACHE INTERNAL "Built cfd-stub plugin directory")
