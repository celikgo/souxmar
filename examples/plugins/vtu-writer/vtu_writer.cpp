// SPDX-License-Identifier: Apache-2.0
//
// vtu-writer — emit a ParaView-readable .vtu (VTK XML UnstructuredGrid)
// without linking against VTK.
//
// Why this exists: a real VTK adapter pulls in ~300 MB of dependencies that
// Sprint 3 does not need. ParaView's .vtu XML format is small enough to
// hand-emit for the basic ASCII case; that gets us a runnable end-to-end
// example today and a regression target the future VTK-backed writer must
// match. The full VTK adapter (binary + appended data + parallel pieces)
// lands in Sprint 6 alongside the OpenCASCADE adapter.
//
// Format reference: VTK File Formats, "UnstructuredGrid" section.
//   https://docs.vtk.org/en/latest/design_documents/VTKFileFormats.html

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"
#include "souxmar-c/writer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Map souxmar element types to VTK cell type ids. The VTK numbering is
// stable and documented in vtkCellType.h.
//   https://vtk.org/doc/nightly/html/vtkCellType_8h_source.html
// 0 means "no VTK equivalent — refuse to write."
int souxmar_to_vtk_cell_type(uint16_t et) {
  switch (et) {
    case SOUXMAR_ET_VERTEX:    return 1;   // VTK_VERTEX
    case SOUXMAR_ET_EDGE2:     return 3;   // VTK_LINE
    case SOUXMAR_ET_EDGE3:     return 21;  // VTK_QUADRATIC_EDGE
    case SOUXMAR_ET_TRI3:      return 5;   // VTK_TRIANGLE
    case SOUXMAR_ET_TRI6:      return 22;  // VTK_QUADRATIC_TRIANGLE
    case SOUXMAR_ET_QUAD4:     return 9;   // VTK_QUAD
    case SOUXMAR_ET_QUAD8:     return 23;  // VTK_QUADRATIC_QUAD
    case SOUXMAR_ET_QUAD9:     return 28;  // VTK_BIQUADRATIC_QUAD
    case SOUXMAR_ET_TET4:      return 10;  // VTK_TETRA
    case SOUXMAR_ET_TET10:     return 24;  // VTK_QUADRATIC_TETRA
    case SOUXMAR_ET_HEX8:      return 12;  // VTK_HEXAHEDRON
    case SOUXMAR_ET_HEX20:     return 25;  // VTK_QUADRATIC_HEXAHEDRON
    case SOUXMAR_ET_HEX27:     return 29;  // VTK_TRIQUADRATIC_HEXAHEDRON
    case SOUXMAR_ET_PRISM6:    return 13;  // VTK_WEDGE
    case SOUXMAR_ET_PRISM15:   return 26;  // VTK_QUADRATIC_WEDGE
    case SOUXMAR_ET_PYRAMID5:  return 14;  // VTK_PYRAMID
    case SOUXMAR_ET_PYRAMID13: return 27;  // VTK_QUADRATIC_PYRAMID
    default:                   return 0;
  }
}

// Emit one <DataArray> per time step of `field` into an already-open
// <PointData> / <CellData> element.
//
// Precision: max_digits10 (17 for double) so a value survives the round trip
// through text. The default ostream precision is 6 significant digits, which
// is fine for coordinates a human reads and quietly destructive for a solved
// field — a 1e-9 displacement printed as "1.23457e-09" has lost three digits
// the solver worked to produce, and a convergence study run against the
// written file would measure the writer instead of the mesh.
void emit_field_arrays(std::ostringstream& out, const souxmar_field_t* field) {
  const char* name = souxmar_field_name(field);
  const std::size_t count = souxmar_field_count(field);
  const std::size_t steps = souxmar_field_num_time_steps(field);
  const std::uint8_t components = souxmar_field_components(field);
  const double* data = souxmar_field_data_const(field);

  const auto saved_precision = out.precision();
  out << std::setprecision(std::numeric_limits<double>::max_digits10);

  for (std::size_t t = 0; t < steps; ++t) {
    // VTK has no notion of time inside a single .vtu, so a multi-step field
    // becomes one suffixed array per step. Step 0 keeps the bare name: the
    // overwhelmingly common single-step case then reads in ParaView as the
    // field the pipeline actually named, not as "displacement_t0".
    out << "        <DataArray type=\"Float64\" Name=\"" << name;
    if (t > 0)
      out << "_t" << t;
    out << "\" NumberOfComponents=\"" << static_cast<int>(components) << "\" format=\"ascii\">\n"
        << "          ";
    const std::size_t base = t * count * components;
    for (std::size_t i = 0; i < count * components; ++i) {
      out << data[base + i];
      out << ((i + 1) % 6 == 0 ? "\n          " : " ");
    }
    if (count * components == 0)
      out << "\n          ";
    out << "\n        </DataArray>\n";
  }

  out << std::setprecision(saved_precision);
}

souxmar_status_t vtu_write(const souxmar_mesh_t* mesh,
                           const souxmar_field_t* field,
                           const souxmar_value_t* inputs,
                           void* /*user_data*/) {
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
                                "writer.vtu requires `path: <string>`");
  }
  const char* path = souxmar_value_as_string(path_value);
  if (path == nullptr || path[0] == '\0') {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "`path` must not be empty");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);

  // Build the file in memory then commit in a single write — VTU readers
  // are strict about XML well-formedness, and a partial file from a crash
  // mid-emit confuses ParaView in surprising ways.
  std::ostringstream out;
  out << "<?xml version=\"1.0\"?>\n"
      << "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\">\n"
      << "  <UnstructuredGrid>\n"
      << "    <Piece NumberOfPoints=\"" << num_nodes
      << "\" NumberOfCells=\"" << num_cells << "\">\n";

  // ---- Points ------------------------------------------------------------
  out << "      <Points>\n"
      << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n"
      << "          ";
  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  for (std::size_t i = 0; i < flat_size; ++i) {
    out << coords[i];
    out << ((i + 1) % 9 == 0 ? "\n          " : " ");
  }
  if (flat_size == 0) out << "\n          ";
  out << "\n        </DataArray>\n"
      << "      </Points>\n";

  // ---- Cells -------------------------------------------------------------
  // VTU wants three parallel arrays: connectivity (flat node ids),
  // offsets (one per cell, end of that cell's slice in connectivity),
  // types (one per cell, VTK cell type int).
  out << "      <Cells>\n";

  out << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n"
      << "          ";
  std::vector<uint64_t> scratch;
  std::vector<std::size_t> offsets;
  offsets.reserve(num_cells);
  std::size_t running_offset = 0;
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
    if (n == 0) continue;
    scratch.resize(n);
    const auto status = souxmar_mesh_cell_nodes(mesh, c, scratch.data(), n);
    if (status.code != SOUXMAR_OK) {
      return status;
    }
    for (std::size_t i = 0; i < n; ++i) out << scratch[i] << " ";
    running_offset += n;
    offsets.push_back(running_offset);
  }
  out << "\n        </DataArray>\n";

  out << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n"
      << "          ";
  for (auto o : offsets) out << o << " ";
  out << "\n        </DataArray>\n";

  out << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n"
      << "          ";
  for (std::size_t c = 0; c < num_cells; ++c) {
    const uint16_t et = souxmar_mesh_cell_type(mesh, c);
    const int vtk_type = souxmar_to_vtk_cell_type(et);
    if (vtk_type == 0) {
      return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                  "writer.vtu: mesh contains a cell type with no VTK equivalent");
    }
    out << vtk_type << " ";
  }
  out << "\n        </DataArray>\n"
      << "      </Cells>\n";

  // ---- PointData / CellData ----------------------------------------------
  // The writer vtable's `field` is documented as "may be NULL (mesh-only
  // writers)", and until now this writer treated every field as if it were
  // NULL: the parameter was unnamed and nothing downstream of a solver ever
  // reached the file. ROADMAP Phase 2's definition of done — "a .vtu that
  // opens in ParaView and shows a recognisably correct stress field" — was
  // unreachable for that reason alone, independently of whether any solver
  // computed one.
  if (field != nullptr) {
    const std::uint8_t location = souxmar_field_location(field);
    const std::size_t count = souxmar_field_count(field);

    // A field whose count does not match the thing it claims to be attached
    // to is a pipeline wiring bug. Writing it anyway produces a file ParaView
    // opens and renders wrong, which is far worse than refusing.
    if (location == SOUXMAR_FL_NODAL) {
      if (count != num_nodes) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "writer.vtu: nodal field length does not match the mesh node count");
      }
      out << "      <PointData>\n";
      emit_field_arrays(out, field);
      out << "      </PointData>\n";
    } else if (location == SOUXMAR_FL_CELL) {
      if (count != num_cells) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "writer.vtu: cell field length does not match the mesh cell count");
      }
      out << "      <CellData>\n";
      emit_field_arrays(out, field);
      out << "      </CellData>\n";
    }
    // SOUXMAR_FL_FACE and SOUXMAR_FL_GAUSS_POINT are dropped deliberately.
    // Neither has a VTU equivalent: face data would need the faces emitted as
    // their own cells, and Gauss-point data needs a per-cell quadrature rule
    // the format cannot carry. Silently omitting them is the honest option —
    // failing would make writer.vtu unusable as a general sink, and
    // interpolating them to nodes here would invent numbers the solver never
    // produced. A future postproc stage does that projection explicitly.
  }

  out << "    </Piece>\n"
      << "  </UnstructuredGrid>\n"
      << "</VTKFile>\n";

  std::ofstream sink(path, std::ios::binary | std::ios::trunc);
  if (!sink.is_open()) {
    return souxmar_status_error(SOUXMAR_E_IO, "could not open `path` for writing");
  }
  const auto blob = out.str();
  sink.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  if (!sink.good()) {
    return souxmar_status_error(SOUXMAR_E_IO, "write to `path` failed");
  }
  return souxmar_status_ok();
}

constexpr souxmar_writer_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &vtu_write,
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
      souxmar_registry_add_writer(registry, "writer.vtu", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
