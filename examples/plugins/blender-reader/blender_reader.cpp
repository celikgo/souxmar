// SPDX-License-Identifier: Apache-2.0
//
// blender-reader — Sprint 8 push 3 opt-in reader plugin.
//
// Reads Blender `.blend` files by invoking Blender as a subprocess
// (`blender -b --python-expr ...`) to export the scene to a Wavefront
// OBJ inside a per-call work directory, then parsing that OBJ back
// into a souxmar Tri3 mesh.
//
// The subprocess pattern matches ADR-0009 / Sprint 8 push 2's
// OpenFOAM adapter:
//   - find_program(blender) at configure time;
//   - find_executable_on_path("blender") at plugin-load time
//     (capability only registers if the binary is actually present);
//   - subprocess invocation only, with a mandatory wall-clock timeout;
//   - stdout/stderr stream-capped; failures surface as
//     SOUXMAR_E_PLUGIN_REJECTED with the captured stderr tail.
//
// Blender's licence (GPL-2.0+) is not our problem because we never
// link against `libblender.so`; we run the binary as a child process,
// which is the same well-trodden process-isolation pattern ADR-0009
// authorises.
//
// Capability: `reader.blend`. Declared `single-threaded` — Blender's
// embedded Python interpreter and bpy state are not reentrant, even
// across subprocess boundaries (per-invocation, work dirs are unique;
// across invocations, we serialise to avoid clobbering temp paths).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

#include "souxmar/plugin/subprocess.h"

namespace fs = std::filesystem;

namespace {

constexpr std::chrono::milliseconds kDefaultTimeout{5 * 60 * 1000};  // 5 min.

// Blender's CLI exposes a `--python-expr` flag that runs Python after
// the .blend is loaded. The exporter call below is intentionally
// minimal: bpy.ops.wm.obj_export collapses to a single OBJ with
// triangulated faces, sufficient for souxmar's Tri3 ingestion.
//
// Blender 4.x renamed `bpy.ops.export_scene.obj` → `bpy.ops.wm.obj_export`;
// the new name is canonical from 4.0 onwards (matching our
// version_range = ">=3.4,<5.0" floor in the manifest, scoped to the
// 4.x family of operators in practice).
std::string make_export_script(const fs::path& out_obj) {
  std::ostringstream py;
  py << "import bpy\n"
     << "bpy.ops.object.select_all(action='SELECT')\n"
     << "bpy.ops.wm.obj_export(filepath=r'" << out_obj.string() << "', "
     <<   "export_selected_objects=True, "
     <<   "export_triangulated_mesh=True, "
     <<   "export_materials=False, "
     <<   "forward_axis='Y', up_axis='Z')\n";
  return py.str();
}

// Tail of a captured stderr stream, capped so error messages stay
// tractable when surfacing through the C ABI's thread-local buffer.
std::string stderr_tail(const std::string& s, std::size_t max_chars = 512) {
  if (s.size() <= max_chars) return s;
  return std::string("...") + s.substr(s.size() - max_chars);
}

// Mints a per-call work directory under temp_directory_path(). Random
// suffix avoids collision when two pipelines run the same .blend in
// parallel (the harness itself is single-threaded per plugin; this is
// belt-and-braces against ambient parallelism).
fs::path make_work_dir() {
  std::random_device                            rd;
  std::mt19937_64                               rng(rd());
  std::uniform_int_distribution<std::uint64_t>  dist;
  std::ostringstream                            name;
  name << "souxmar-blender-" << std::hex << dist(rng);
  const fs::path p = fs::temp_directory_path() / name.str();
  std::error_code ec;
  fs::create_directories(p, ec);
  return p;
}

// --- OBJ subset parser ---------------------------------------------
// Self-contained to avoid a host-side link between two plugins (the
// always-on obj-reader compiles to its own .so and is loaded into a
// peer registry; this plugin can't call into another plugin's symbols
// across the C ABI boundary).
//
// We deliberately keep the parser slim: positions + faces (vertex
// component only; vt/vn ignored). Blender's exporter writes triangles
// when `export_triangulated_mesh=True`, so the fan-triangulation
// fallback is rarely exercised — we keep it for robustness.

std::int64_t parse_face_field(std::string_view tok) noexcept {
  std::size_t slash = tok.find('/');
  std::string_view head = (slash == std::string_view::npos) ? tok : tok.substr(0, slash);
  if (head.empty()) return 0;
  char buf[64];
  std::size_t n = head.size() < sizeof(buf) - 1 ? head.size() : sizeof(buf) - 1;
  std::memcpy(buf, head.data(), n);
  buf[n] = '\0';
  char* end = nullptr;
  const long long v = std::strtoll(buf, &end, 10);
  if (end == buf) return 0;
  return static_cast<std::int64_t>(v);
}

std::int64_t resolve_index(std::int64_t obj_idx, std::size_t num_verts) noexcept {
  if (obj_idx > 0) {
    const std::int64_t z = obj_idx - 1;
    return z < static_cast<std::int64_t>(num_verts) ? z : -1;
  }
  if (obj_idx < 0) {
    const std::int64_t z = static_cast<std::int64_t>(num_verts) + obj_idx;
    return z >= 0 ? z : -1;
  }
  return -1;
}

souxmar_status_t parse_obj(std::istream&                in,
                           std::vector<double>&         flat_coords,
                           std::vector<std::uint64_t>&  tri_nodes) {
  std::string line;
  std::vector<std::int64_t> face_indices;
  face_indices.reserve(8);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
    if (s >= line.size() || line[s] == '#') continue;
    std::istringstream iss(line.substr(s));
    std::string head;
    if (!(iss >> head)) continue;
    if (head == "v") {
      double x = 0, y = 0, z = 0;
      if (!(iss >> x >> y >> z)) {
        return souxmar_status_error(SOUXMAR_E_IO, "OBJ `v` line malformed");
      }
      flat_coords.push_back(x);
      flat_coords.push_back(y);
      flat_coords.push_back(z);
      continue;
    }
    if (head == "f") {
      face_indices.clear();
      std::string tok;
      while (iss >> tok) {
        const auto v_obj = parse_face_field(tok);
        if (v_obj == 0) {
          return souxmar_status_error(SOUXMAR_E_IO,
              "OBJ `f` line has malformed vertex reference");
        }
        const auto z = resolve_index(v_obj, flat_coords.size() / 3);
        if (z < 0) {
          return souxmar_status_error(SOUXMAR_E_IO,
              "OBJ face references undefined vertex");
        }
        face_indices.push_back(z);
      }
      if (face_indices.size() < 3) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "OBJ face has fewer than 3 vertices");
      }
      for (std::size_t i = 1; i + 1 < face_indices.size(); ++i) {
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[0]));
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[i]));
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[i + 1]));
      }
    }
    // All other prefixes (vn / vt / vp / o / g / s / mtllib / usemtl /
    // l) are ignored — the OBJ produced by Blender's exporter only
    // depends on `v` + `f` for our purposes.
  }
  if (flat_coords.empty() || tri_nodes.empty()) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "Blender export produced no triangles — empty .blend?");
  }
  return souxmar_status_ok();
}

souxmar_status_t blender_read(const char*                       path,
                              const souxmar_value_t*            inputs,
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

  // Resolve `blender` afresh each call — between plugin load and
  // first invocation a user may have sourced a different environment.
  const auto blender_bin = souxmar::plugin::find_executable_on_path("blender");
  if (!blender_bin) {
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED,
        "blender binary not on $PATH at call time");
  }

  // Optional timeout override from the value bag.
  std::chrono::milliseconds timeout = kDefaultTimeout;
  if (inputs) {
    const souxmar_value_t* t = souxmar_value_map_get(inputs, "timeout_seconds");
    if (t && souxmar_value_kind(t) == SOUXMAR_VK_NUMBER) {
      const double secs = souxmar_value_as_number(t);
      if (secs > 0.0) {
        timeout = std::chrono::milliseconds(
            static_cast<std::int64_t>(secs * 1000.0));
      }
    }
  }

  const fs::path work_dir = make_work_dir();
  if (work_dir.empty()) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "could not create blender work directory under temp_directory_path()");
  }
  const fs::path out_obj = work_dir / "scene.obj";

  souxmar::plugin::SubprocessOptions opts;
  opts.argv = {
      blender_bin->string(),
      "-b",                      // headless / no-UI
      path,                      // input .blend
      "--python-expr", make_export_script(out_obj),
  };
  opts.work_dir          = work_dir;
  opts.timeout           = timeout;
  opts.max_capture_bytes = 128 * 1024;  // Blender is chatty on startup

  const auto result = souxmar::plugin::run_subprocess(opts);

  // Map result states to status codes mirroring openfoam-solver.
  if (!result.ok) {
    static thread_local std::string msg;
    msg = "blender spawn failed: " + result.error_message;
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
  if (result.timed_out) {
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_TIMEOUT,
        "blender export exceeded timeout");
  }
  if (result.fatal_signal != 0) {
    static thread_local std::string msg;
    msg = "blender exited on signal " + std::to_string(result.fatal_signal) +
          " — stderr tail: " + stderr_tail(result.stderr_bytes);
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
  if (result.exit_code != 0) {
    static thread_local std::string msg;
    msg = "blender exit " + std::to_string(result.exit_code) +
          " — stderr tail: " + stderr_tail(result.stderr_bytes);
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
  if (!fs::exists(out_obj)) {
    static thread_local std::string msg;
    msg = "blender exited clean but produced no OBJ — stderr tail: " +
          stderr_tail(result.stderr_bytes);
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }

  std::ifstream in(out_obj);
  if (!in.is_open()) {
    std::error_code ec;
    fs::remove_all(work_dir, ec);
    return souxmar_status_error(SOUXMAR_E_IO,
        "could not reopen blender-exported OBJ for parsing");
  }
  std::vector<double>        flat_coords;
  std::vector<std::uint64_t> tri_nodes;
  const auto parse_status = parse_obj(in, flat_coords, tri_nodes);
  std::error_code ec;
  fs::remove_all(work_dir, ec);
  if (parse_status.code != SOUXMAR_OK) return parse_status;

  const std::size_t num_nodes = flat_coords.size() / 3;
  const std::size_t num_tris  = tri_nodes.size()   / 3;

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
  }
  souxmar_mesh_reserve_nodes(mesh, num_nodes);
  souxmar_mesh_reserve_cells(mesh, num_tris);
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double p[3] = {
        flat_coords[i * 3 + 0],
        flat_coords[i * 3 + 1],
        flat_coords[i * 3 + 2]};
    souxmar_mesh_add_node(mesh, p);
  }
  for (std::size_t i = 0; i < num_tris; ++i) {
    const std::uint64_t nodes[3] = {
        tri_nodes[i * 3 + 0],
        tri_nodes[i * 3 + 1],
        tri_nodes[i * 3 + 2]};
    souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TRI3, nodes, 3, -1, nullptr);
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_reader_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &blender_read,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  // Load-time binary probe. If blender is not on $PATH at this moment,
  // we refuse to register the capability — the registry will simply
  // not list `reader.blend`, and pipelines referencing it surface a
  // clean "capability not found" instead of a runtime spawn failure.
  if (!souxmar::plugin::find_executable_on_path("blender").has_value()) {
    return -2;
  }
  const souxmar_status_t s = souxmar_registry_add_reader(
      registry, "reader.blend", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
