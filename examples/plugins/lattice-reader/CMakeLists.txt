souxmar_add_plugin(lattice_reader
  SOURCES   lattice_reader.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "reader.lattice"
)

if(APPLE)
  set_target_properties(lattice_reader PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(lattice_reader PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(lattice_reader PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "lattice_reader")
endif()

set(SOUXMAR_LATTICE_READER_DIR "$<TARGET_FILE_DIR:lattice_reader>"
    CACHE INTERNAL "Built lattice-reader plugin directory")
