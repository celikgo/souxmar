souxmar_add_plugin(modal_stub
  SOURCES   modal_stub.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.modal.linear"
)

if(APPLE)
  set_target_properties(modal_stub PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(modal_stub PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(modal_stub PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "modal_stub")
endif()

set(SOUXMAR_MODAL_STUB_DIR "$<TARGET_FILE_DIR:modal_stub>"
    CACHE INTERNAL "Built modal-stub plugin directory")
