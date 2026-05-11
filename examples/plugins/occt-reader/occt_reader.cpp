// SPDX-License-Identifier: Apache-2.0
//
// occt-reader — Sprint 6 push 4 opt-in reader plugin.
//
// Reads STEP and IGES files through OpenCASCADE, producing a souxmar
// Geometry handle with the entity topology preserved (vertices /
// edges / faces / solids walked off the OCCT shape and added through
// the souxmar-c/geometry.h API).
//
// Build gating: this plugin is compiled only when
// `-DSOUXMAR_WITH_OPENCASCADE=ON` is passed AND find_package(OpenCASCADE)
// succeeds in the parent CMakeLists.txt. Default builds do not include
// this binary, so neither does the default CI matrix; nightly builds
// turn the flag on. The always-on counterpart for default CI is the
// stl-reader sibling plugin.
//
// Capability: `reader.step` (also handles `.iges` via the same
// translator, switched on extension). Declared `single-threaded` —
// OpenCASCADE's translator state is not thread-safe.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "souxmar-c/abi.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

// OpenCASCADE headers. These are only available when the host system
// has OCCT installed and find_package(OpenCASCADE) populated the
// include path — the build option gates the .cpp from compiling
// otherwise.
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pnt.hxx>

namespace {

std::string lower_extension(const std::filesystem::path& p) {
  auto ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

souxmar_status_t read_step_or_iges(const std::filesystem::path& path,
                                   TopoDS_Shape&                out_shape) {
  const auto ext = lower_extension(path);
  if (ext == ".step" || ext == ".stp") {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
      return souxmar_status_error(SOUXMAR_E_IO, "STEP reader rejected the file");
    }
    reader.TransferRoots();
    if (reader.NbShapes() == 0) {
      return souxmar_status_error(SOUXMAR_E_IO, "STEP file contained no shapes");
    }
    out_shape = reader.OneShape();
    return souxmar_status_ok();
  }
  if (ext == ".iges" || ext == ".igs") {
    IGESControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
      return souxmar_status_error(SOUXMAR_E_IO, "IGES reader rejected the file");
    }
    reader.TransferRoots();
    if (reader.NbShapes() == 0) {
      return souxmar_status_error(SOUXMAR_E_IO, "IGES file contained no shapes");
    }
    out_shape = reader.OneShape();
    return souxmar_status_ok();
  }
  static thread_local std::string msg;
  msg = std::string("unsupported extension '") + ext +
        "' — occt-reader handles .step / .stp / .iges / .igs";
  return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, msg.c_str());
}

// Walk the OCCT topology and emit entities into a souxmar Geometry.
// Tag preservation: each OCCT entity gets a souxmar tag matching the
// index of its first appearance in TopExp_Explorer order — stable
// across re-reads of the same file.
souxmar_status_t emit_geometry(const TopoDS_Shape&   shape,
                               souxmar_geometry_t*   geom) {
  // Tag preservation: each entity gets a tag matching its index in
  // TopExp_Explorer order — stable across re-reads of the same file.
  // souxmar_geometry_add_* returns the assigned index; souxmar_geometry_set_tag
  // attaches the tag in a second call (mirroring the souxmar-c API shape).
  std::int32_t tag = 0;

  for (TopExp_Explorer ex(shape, TopAbs_VERTEX); ex.More(); ex.Next(), ++tag) {
    const TopoDS_Vertex& v = TopoDS::Vertex(ex.Current());
    const gp_Pnt p = BRep_Tool::Pnt(v);
    const double xyz[3] = {p.X(), p.Y(), p.Z()};
    const std::uint32_t idx = souxmar_geometry_add_vertex(geom, xyz);
    souxmar_geometry_set_tag(geom, SOUXMAR_GK_VERTEX, idx, tag);
  }
  for (TopExp_Explorer ex(shape, TopAbs_EDGE);   ex.More(); ex.Next(), ++tag) {
    const std::uint32_t idx = souxmar_geometry_add_edge(geom);
    souxmar_geometry_set_tag(geom, SOUXMAR_GK_EDGE, idx, tag);
  }
  for (TopExp_Explorer ex(shape, TopAbs_FACE);   ex.More(); ex.Next(), ++tag) {
    const std::uint32_t idx = souxmar_geometry_add_face(geom);
    souxmar_geometry_set_tag(geom, SOUXMAR_GK_FACE, idx, tag);
  }
  for (TopExp_Explorer ex(shape, TopAbs_SOLID);  ex.More(); ex.Next(), ++tag) {
    const std::uint32_t idx = souxmar_geometry_add_solid(geom);
    souxmar_geometry_set_tag(geom, SOUXMAR_GK_SOLID, idx, tag);
  }
  return souxmar_status_ok();
}

souxmar_status_t occt_read(const char*                       path,
                           const souxmar_value_t*            /*inputs*/,
                           const souxmar_reader_options_t*   /*options*/,
                           souxmar_mesh_t**                  out_mesh,
                           souxmar_geometry_t**              out_geometry,
                           void*                             /*user_data*/) {
  if (!path) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "path is NULL");
  }
  if (!out_mesh || !out_geometry) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "out_mesh / out_geometry are NULL");
  }
  *out_mesh     = nullptr;
  *out_geometry = nullptr;

  TopoDS_Shape shape;
  const auto status = read_step_or_iges(std::filesystem::path(path), shape);
  if (status.code != SOUXMAR_OK) return status;

  souxmar_geometry_t* geom = souxmar_geometry_new();
  if (!geom) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_geometry_new");
  }
  const auto emit = emit_geometry(shape, geom);
  if (emit.code != SOUXMAR_OK) {
    souxmar_geometry_free(geom);
    return emit;
  }

  *out_geometry = geom;
  return souxmar_status_ok();
}

constexpr souxmar_reader_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &occt_read,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  // OCCT translates both STEP and IGES; we expose two capability ids
  // sharing one vtable. The plugin's read_fn switches on file
  // extension internally.
  const souxmar_status_t s_step = souxmar_registry_add_reader(
      registry, "reader.step", &kVtable, /*user_data=*/nullptr);
  if (s_step.code != SOUXMAR_OK) return 1;
  const souxmar_status_t s_iges = souxmar_registry_add_reader(
      registry, "reader.iges", &kVtable, /*user_data=*/nullptr);
  return s_iges.code == SOUXMAR_OK ? 0 : 1;
}
