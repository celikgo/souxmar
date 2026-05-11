// SPDX-License-Identifier: Apache-2.0
//
// hello-writer — minimal writer reference plugin.
//
// Reads a Mesh handle through the C ABI accessors and writes a 2-line
// text summary to a file path supplied via the `path` input. The point
// is to prove the writer vtable + souxmar_value_t input bag + Mesh read
// accessors work end-to-end. Real VTU output comes with the VTK adapter
// in Sprint 3+.

#include <cstdio>
#include <cstring>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"
#include "souxmar-c/writer.h"

namespace {

souxmar_status_t hello_write(const souxmar_mesh_t*  mesh,
                             const souxmar_field_t* /*field*/,
                             const souxmar_value_t* inputs,
                             void*                  /*user_data*/) {
  if (mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (inputs == nullptr || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer inputs must be a map");
  }

  const souxmar_value_t* path_value = souxmar_value_map_get(inputs, "path");
  if (path_value == nullptr || souxmar_value_kind(path_value) != SOUXMAR_VK_STRING) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer.text-summary requires `path: <string>`");
  }
  const char* path = souxmar_value_as_string(path_value);
  if (path == nullptr || path[0] == '\0') {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "`path` must not be empty");
  }

  std::FILE* fp = std::fopen(path, "w");
  if (fp == nullptr) {
    return souxmar_status_error(SOUXMAR_E_IO, "could not open `path` for writing");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  std::fprintf(fp, "num_nodes=%zu\nnum_cells=%zu\n", num_nodes, num_cells);
  std::fclose(fp);

  return souxmar_status_ok();
}

constexpr souxmar_writer_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &hello_write,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (host == nullptr || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s =
      souxmar_registry_add_writer(registry,
                                  "writer.text-summary",
                                  &kVtable,
                                  /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
