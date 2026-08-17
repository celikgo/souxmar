// SPDX-License-Identifier: Apache-2.0
//
// am-manufacturability — design-for-additive-manufacturing (DfAM) checks.
//
// Registers three capabilities, all mesh-only analyses and therefore all
// `solver.*` rather than `postproc.*` (the postproc dispatch path
// hard-requires an upstream `field:` handle — see ADR-0044 and
// src/pipeline/registry_dispatcher.cpp:252):
//
//   solver.am.overhang      per-cell downskin / overhang / support need
//   solver.am.printability  composite DfAM printability score
//   solver.am.buildtime     per-layer build time and cross-sectional area
//
// The boundary-face extraction all three share lives in
// boundary_faces.hpp; read that file's header for the topology contract.
//
// ======================================================================
// What it computes
// ======================================================================
//
// solver.am.overhang
//   For every boundary facet with outward unit normal `n`, and the unit
//   build direction `b`:
//
//       the facet is DOWNWARD  iff  n . b < 0
//       its tilt from the build plate is  acos(|n . b|)  [rad]
//       support_needed = 1  iff  any downward facet of the cell has
//                                tilt < overhang_threshold_deg
//
//   The tilt is the angle between the facet and the build plate (the
//   plane perpendicular to `b`), which is the angle AM practice quotes:
//   a horizontal down-facing facet is 0 deg (fully unsupported, needs
//   support), a vertical wall is 90 deg (self-supporting), a 45 deg
//   facet is 45 deg and sits exactly on the default threshold. The
//   comparison is strict, so a facet exactly at the threshold is NOT
//   flagged. Verified numerically: n = (0,0,-1) -> 0.000 deg;
//   n = (1,0,0) -> 90.000 deg; n = (1,0,-1)/sqrt(2) -> 45.000 deg.
//
//   The 45 deg default is the powder-bed rule of thumb (Thomas, "The
//   development of design rules for selective laser melting", 2009;
//   reproduced in every machine vendor's DfAM guide). It is a rule of
//   thumb, not a material property: real self-supporting angles range
//   from roughly 30 to 50 deg with alloy, layer height and beam
//   parameters.
//
// solver.am.printability
//   A weighted-penalty heuristic. One hard gate plus four weighted
//   penalties, every penalty in [0, 1]:
//
//     hard gate — build-volume fit: if the mesh's axis-aligned bounding
//       box does not fit inside machine_build_volume component-wise, the
//       score is 0 and the limiting factor is 3 for every cell. No
//       re-orientation search is attempted here (that is what the
//       `set_build_orientation` agent tool is for).
//
//     p_overhang = (threshold - min_downskin_tilt) / threshold, clamped
//                  to [0, 1]; 0 when the cell has no sub-threshold
//                  downward facet.
//     p_thin     = (min_wall_thickness - proxy) / min_wall_thickness,
//                  clamped to [0, 1]; 0 when proxy >= min_wall_thickness
//                  or when no proxy exists.
//     p_aspect   = (slenderness - 8) / 8, clamped to [0, 1], where
//                  slenderness = (part height along b) / (smaller
//                  in-plane bounding extent). 8:1 is the slenderness
//                  rule of thumb that recurs in vendor DfAM guides for
//                  recoater-blade collision and part-toppling risk; it
//                  is not a validated limit. Global, so identical on
//                  every cell.
//     p_corr     = (min_wall_thickness + 2*corrosion_allowance - proxy)
//                  / (min_wall_thickness + 2*corrosion_allowance),
//                  clamped to [0, 1]; only when corrosion_allowance > 0.
//                  The factor 2 assumes both faces of the wall are
//                  wetted.
//
//     printability_score = clamp(1 - (0.40*p_overhang + 0.30*p_thin
//                                     + 0.15*p_aspect + 0.15*p_corr),
//                                0, 1)
//
//     limiting_factor_code names the largest *weighted* term, ties
//     broken in code order (1 overhang, 2 thin wall, 4 aspect,
//     5 corrosion), or 0 when every penalty is zero.
//
//   wall_thickness_proxy_m is 2*V_cell / A_boundary(cell) for a cell
//   that owns boundary facets: for a wall meshed with a single cell
//   through its thickness (two opposite boundary facets of area A and
//   volume A*t) that evaluates to exactly t. It is reported as the
//   bounding-box diagonal ("bulk") for a cell with no boundary facet,
//   and as 0 for a surface-mesh cell, where the mesh carries no
//   thickness information at all and the thin-wall penalties are
//   skipped.
//
// solver.am.buildtime
//   Layer resolution follows contract §2.2: a non-negative
//   souxmar_mesh_cell_tag IS the layer index (that is what
//   `mesher.am.layered` writes); otherwise the cell centroid's
//   projection on `b` is binned by `layer_height` from the mesh's
//   minimum projection.
//
//   Per-layer cross-section:
//     volume mesh  : area = (sum of cell volumes in the bin) / layer_height
//     surface mesh : area = sum over the bin's facets of |n . b| * facet_area
//                    (contract §3.11). For a closed shell that counts both
//                    the up-facing and the down-facing facet of every
//                    column, so it over-predicts the true cross-section by
//                    roughly a factor of 2. Stated here because the
//                    contract fixes the definition.
//
//   `machine_layers = max(1, round(layer_height / process_layer_height))`
//   converts one simulation layer into the machine's real powder/road
//   layers. Per simulation layer:
//
//     lpbf, sls  : t = machine_layers * (area / (hatch_spacing*scan_speed)
//                                        + recoat_time)
//                  — scanned track length is area/hatch_spacing.
//     fff        : t = machine_layers * area / (road_width*print_speed)
//                  — extruded road length is area/road_width; no
//                    recoater, so no per-layer recoat overhead.
//     ded_waam   : t = (area*layer_height) / (deposition_rate
//                                             / (3600*density))
//                  — mass-flow limited, geometry only sets the volume.
//
//   An empty layer inside the build height still costs
//   machine_layers*recoat_time on a powder-bed machine (the recoater
//   still passes) and nothing on fff/ded_waam.
//
//   A mixed shell+solid mesh takes the volume path, so its shell cells
//   contribute nothing to the cross-section.
//
// ======================================================================
// What this is NOT
// ======================================================================
//
//   * Not a support-structure generator. `support_needed` is a per-cell
//     flag, not a volume; nothing is generated, offset or costed.
//   * Not a geometric thickness measurement. The wall proxy is
//     mesh-resolution dependent for any cell that is not a single cell
//     through the wall: a cell with one boundary facet reports twice its
//     own height, whatever the real wall is. A true local thickness
//     needs a medial-axis or ray-cast computation, which needs geometry
//     the mesh ABI does not carry.
//   * Not a printability guarantee. The score is a documented weighted
//     heuristic with hand-chosen weights, calibrated against nothing. It
//     is a triage aid for ranking candidate designs and orientations,
//     and it will happily score an unbuildable part above 0.9 if the
//     unbuildable part of it is something no term above looks at
//     (trapped powder, unreachable internal channels, thermal
//     stress-driven recoater crashes, minimum feature size below the
//     beam width, ...).
//   * Not a layer slicer. `solver.am.buildtime` bins each cell's whole
//     volume into the single layer its centroid falls in, so a mesh whose
//     cells are much taller than `layer_height` reports a spiky
//     cross-section profile (all of a cell's volume in one bin, nothing
//     in the bins it also spans). Feed it a mesh whose cell height is the
//     layer height — which is what `mesher.am.layered` produces — or set
//     `layer_height` to the mesh's own cell height. The real contour
//     slicing lives in `writer.am.gcode` / `writer.am.cli`.
//   * Not a build-time quote. The time model is a pure geometric
//     scan-length model. It ignores skywriting and jump times, contour
//     versus infill parameter sets, multi-laser splitting, purge and
//     warm-up cycles, dose factors, machine start/stop, and every kind
//     of operator intervention. Real LPBF jobs commonly run 10-40%
//     longer than this model.
//   * Not orientation optimisation. `build_direction` is an input, not a
//     search space.
//   * Not applicable to beam / point meshes. A mesh whose every cell is a
//     Vertex or an Edge2 (what `reader.lattice` produces) has no facet to
//     measure, so all three capabilities reject it with
//     SOUXMAR_E_PLUGIN_REJECTED rather than returning a reassuring
//     "nothing needs support, score 1.0". Cells of an unsupported type
//     inside an otherwise decomposable mesh are skipped silently and read
//     back as zeros.
//   * Nothing here reads a material file; every property is an explicit
//     input with a default (contract §2.6).
//
// ======================================================================
// Inputs (souxmar_value_t map) — units are strict SI unless noted
// ======================================================================
//
// solver.am.overhang
//   build_direction        : list [x,y,z], default [0,0,1] (normalised;
//                            zero-length is SOUXMAR_E_INVALID_ARGUMENT)
//   overhang_threshold_deg : number, default 45     (deg, clamped [0,90])
//   layer_height           : number, default 0.001  (m, clamped [1e-6,10])
//
// solver.am.printability
//   build_direction        : as above
//   overhang_threshold_deg : number, default 45     (deg, clamped [0,90])
//   min_wall_thickness     : number, default 5.0e-4 (m, clamped [1e-6,1])
//   machine_build_volume   : list [x,y,z], default [0.25, 0.25, 0.3]
//                            (m, each clamped [1e-3, 100])
//   corrosion_allowance    : number, default 0.0    (m, clamped [0, 0.1])
//   layer_height           : number, default 0.001  (m, clamped [1e-6,10])
//
// solver.am.buildtime
//   process                : string, default "lpbf"; one of
//                            lpbf | fff | ded_waam | sls. Anything else
//                            is SOUXMAR_E_INVALID_ARGUMENT.
//   layer_height           : number, default 0.001    (m, clamped [1e-6,10])
//   process_layer_height   : number, default 3.0e-5   (m, clamped [1e-7,1])
//   scan_speed             : number, default 0.8      (m/s, clamped [1e-6,1e4])
//   hatch_spacing          : number, default 1.1e-4   (m, clamped [1e-7,1])
//   recoat_time            : number, default 8.0      (s per machine layer,
//                                                      clamped [0, 3600])
//   print_speed            : number, default 0.05     (m/s, clamped [1e-6,100])
//   road_width             : number, default 4.0e-4   (m, clamped [1e-7,1])
//   deposition_rate        : number, default 3.0      (kg/h, clamped [1e-6,1e4])
//   laser_power            : number, default 200      (W, clamped [0,1e6])
//   machine_power_overhead : number, default 2500     (W, clamped [0,1e7])
//   material_cost_per_kg   : number, default 90       (currency/kg, >= 0)
//   machine_rate_per_hour  : number, default 45       (currency/h, >= 0)
//   density                : number, default 7990     (kg/m^3, clamped [1,3e4])
//   build_direction        : as above
//
//   The last five keys are read, range-checked and then unused here: the
//   field contract carries three components (time, cumulative time,
//   area), and energy / mass / cost totals are re-derived by
//   `writer.am.report` from this field plus the same keys. They are read
//   anyway so that one YAML block drives both stages and so a nonsense
//   value is rejected at the earliest stage — the same pattern
//   modal-stub uses for (E, I, rho, A).
//
// ======================================================================
// Outputs — all three are per-cell VECTOR fields with one time step,
// following the postproc.mesh_quality precedent for packing several
// related metrics into one field (contract §2.4)
// ======================================================================
//
//   "overhang"     [0] min_downskin_tilt_deg  (90 when the cell has no
//                      downward boundary facet)
//                  [1] support_needed         (0 or 1)
//                  [2] downskin_area_m2       (total area of the cell's
//                      downward boundary facets, whether or not they are
//                      below the threshold)
//   "printability" [0] printability_score     (0..1, 1 = trivially printable)
//                  [1] limiting_factor_code   (0 none, 1 overhang,
//                      2 thin wall, 3 build-volume fit, 4 aspect ratio,
//                      5 corrosion allowance)
//                  [2] wall_thickness_proxy_m
//   "buildtime"    [0] layer_time_s           (this cell's layer)
//                  [1] cumulative_time_s      (through this cell's layer)
//                  [2] layer_area_m2

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

#include "boundary_faces.hpp"

namespace {

using souxmar::examples::dfam::BoundaryFace;
using souxmar::examples::dfam::MeshGeometry;
using souxmar::examples::dfam::Vec3;
using souxmar::examples::dfam::build_mesh_geometry;
using souxmar::examples::dfam::plane_basis;
using souxmar::examples::dfam::projected_range;
using souxmar::examples::dfam::vec_dot;
using souxmar::examples::dfam::vec_norm;
using souxmar::examples::dfam::vec_normalise;
using souxmar::examples::dfam::vec_sub;

constexpr double kRadToDeg = 180.0 / std::numbers::pi;

// A cell tag is an int32_t, so a hostile or accidental tag could ask for
// two billion layer bins. Cap the layer count at a value no real build
// reaches (1e6 layers at a 20 um powder layer is a 20 m tall part) and
// report SOUXMAR_E_INVALID_ARGUMENT above it rather than allocating.
constexpr std::size_t kMaxLayers = 1000000;

// solver.am.printability weights. They sum to 1.0, so the score spans the
// full [0, 1] range for a cell that fails every term. Hand-chosen, in
// descending order of how often each one actually stops a build in
// practice; calibrated against nothing (see "What this is NOT").
constexpr double kWeightOverhang  = 0.40;
constexpr double kWeightThinWall  = 0.30;
constexpr double kWeightAspect    = 0.15;
constexpr double kWeightCorrosion = 0.15;

// Slenderness (height / smaller in-plane extent) above which the aspect
// penalty starts. 8:1 is the DfAM rule of thumb for recoater-collision
// and toppling risk on powder-bed machines.
constexpr double kSlendernessLimit = 8.0;

// ---- Value-bag helpers -------------------------------------------------
//
// read_number is copied verbatim from
// examples/plugins/modal-stub/modal_stub.cpp; read_string and read_vec3
// follow the same shape (borrowed pointers, strict kind check, silent
// fall back to the default). modal-stub's read_int and the block's
// read_bool are deliberately absent: §3.9-3.11 declare no integer and no
// boolean inputs, and an unused static helper is a -Wunused-function
// warning.

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

const char* read_string(const souxmar_value_t* inputs, const char* key,
                        const char* dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_STRING) return dv;
  const char* s = souxmar_value_as_string(v);
  return s ? s : dv;
}

Vec3 read_vec3(const souxmar_value_t* inputs, const char* key, Vec3 dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return dv;
  if (souxmar_value_list_size(v) != 3) return dv;
  Vec3 out = dv;
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return dv;
    out[i] = souxmar_value_as_number(c);
  }
  return out;
}

// Contract §2.1: `build_direction` defaults to [0, 0, 1], is normalised
// internally, and a zero-length vector is rejected.
bool read_build_direction(const souxmar_value_t* inputs, Vec3* out) {
  const Vec3 raw = read_vec3(inputs, "build_direction", Vec3{{0.0, 0.0, 1.0}});
  if (!(vec_norm(raw) > 0.0)) return false;
  *out = vec_normalise(raw, Vec3{{0.0, 0.0, 1.0}});
  return true;
}

// A mesh whose every cell is a Vertex or an Edge2 carries no facet and no
// volume, so every metric here would come back as a reassuring zero. Say
// so instead. Cells of an unsupported type in an otherwise decomposable
// mesh are left as zeros (documented in the header).
souxmar_status_t reject_undecomposable(const MeshGeometry& geom) {
  if (geom.num_unsupported_cells == geom.num_cells) {
    return souxmar_status_error(
        SOUXMAR_E_PLUGIN_REJECTED,
        "no cell in this mesh has a face decomposition (Vertex/Edge-only "
        "mesh); the DfAM checks need surface or volume elements");
  }
  return souxmar_status_ok();
}

// ---- Shared per-cell downskin summary ---------------------------------

struct DownskinSummary {
  // 90 deg when the cell owns no downward boundary facet — see §3.9. A
  // near-vertical downward facet also reports ~90 deg, so the two cases
  // are indistinguishable in the output, which is harmless: neither
  // needs support.
  std::vector<double> min_tilt_deg;
  std::vector<double> down_area_m2;
  std::vector<double> boundary_area_m2;
};

DownskinSummary summarise_downskin(const MeshGeometry& geom, const Vec3& b) {
  DownskinSummary s;
  s.min_tilt_deg.assign(geom.num_cells, 90.0);
  s.down_area_m2.assign(geom.num_cells, 0.0);
  s.boundary_area_m2.assign(geom.num_cells, 0.0);

  // Faces arrive in (cell, local face) order; accumulate in that order so
  // the sums are bit-identical across platforms.
  for (const BoundaryFace& f : geom.boundary_faces) {
    if (f.cell >= geom.num_cells) continue;
    s.boundary_area_m2[f.cell] += f.area;
    const double nb = vec_dot(f.normal, b);
    if (nb < 0.0) {
      const double tilt =
          std::acos(std::clamp(std::abs(nb), 0.0, 1.0)) * kRadToDeg;
      if (tilt < s.min_tilt_deg[f.cell]) s.min_tilt_deg[f.cell] = tilt;
      s.down_area_m2[f.cell] += f.area;
    }
  }
  return s;
}

// ---- solver.am.overhang -----------------------------------------------

souxmar_status_t overhang_solve(const souxmar_mesh_t*           mesh,
                                const souxmar_value_t*          inputs,
                                const souxmar_solver_options_t* /*options*/,
                                souxmar_field_t**               out_field,
                                void*                           /*user_data*/) {
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  Vec3 b{{0.0, 0.0, 1.0}};
  if (!read_build_direction(inputs, &b)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }
  const double threshold_deg =
      std::clamp(read_number(inputs, "overhang_threshold_deg", 45.0), 0.0, 90.0);
  // Read and range-checked for input-contract parity with the other AM
  // capabilities (one YAML block drives them all); the overhang geometry
  // itself is layer-height independent.
  (void) std::clamp(read_number(inputs, "layer_height", 0.001), 1e-6, 10.0);

  MeshGeometry geom;
  const souxmar_status_t gs = build_mesh_geometry(mesh, &geom);
  if (gs.code != SOUXMAR_OK) return gs;
  const souxmar_status_t rs = reject_undecomposable(geom);
  if (rs.code != SOUXMAR_OK) return rs;

  const DownskinSummary ds = summarise_downskin(geom, b);

  souxmar_field_t* field =
      souxmar_field_new("overhang", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR,
                        geom.num_cells, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != geom.num_cells * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  for (std::size_t c = 0; c < geom.num_cells; ++c) {
    const double tilt = ds.min_tilt_deg[c];
    // Strict `<`: a facet exactly at the threshold is self-supporting.
    const bool support = (ds.down_area_m2[c] > 0.0) && (tilt < threshold_deg);
    data[c * 3 + 0] = tilt;
    data[c * 3 + 1] = support ? 1.0 : 0.0;
    data[c * 3 + 2] = ds.down_area_m2[c];
  }
  *out_field = field;
  return souxmar_status_ok();
}

// ---- solver.am.printability -------------------------------------------

souxmar_status_t printability_solve(const souxmar_mesh_t*           mesh,
                                    const souxmar_value_t*          inputs,
                                    const souxmar_solver_options_t* /*options*/,
                                    souxmar_field_t**               out_field,
                                    void*                           /*user_data*/) {
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  Vec3 b{{0.0, 0.0, 1.0}};
  if (!read_build_direction(inputs, &b)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }
  const double threshold_deg =
      std::clamp(read_number(inputs, "overhang_threshold_deg", 45.0), 0.0, 90.0);
  const double min_wall =
      std::clamp(read_number(inputs, "min_wall_thickness", 5.0e-4), 1e-6, 1.0);
  const double corrosion_allowance =
      std::clamp(read_number(inputs, "corrosion_allowance", 0.0), 0.0, 0.1);
  Vec3 build_volume = read_vec3(inputs, "machine_build_volume",
                               Vec3{{0.25, 0.25, 0.3}});
  for (std::size_t d = 0; d < 3; ++d) {
    build_volume[d] = std::clamp(build_volume[d], 1e-3, 100.0);
  }
  // As in overhang: accepted, range-checked, and not used by the score.
  (void) std::clamp(read_number(inputs, "layer_height", 0.001), 1e-6, 10.0);

  MeshGeometry geom;
  const souxmar_status_t gs = build_mesh_geometry(mesh, &geom);
  if (gs.code != SOUXMAR_OK) return gs;
  const souxmar_status_t rs = reject_undecomposable(geom);
  if (rs.code != SOUXMAR_OK) return rs;

  const DownskinSummary ds = summarise_downskin(geom, b);

  // ---- Global terms ----
  const Vec3 extent = vec_sub(geom.bbox_max, geom.bbox_min);
  const double bbox_diagonal = vec_norm(extent);

  // Hard gate: component-wise fit of the axis-aligned bounding box in the
  // machine envelope. No re-orientation is attempted.
  bool fits = true;
  for (std::size_t d = 0; d < 3; ++d) {
    if (extent[d] > build_volume[d]) fits = false;
  }

  // Slenderness along the build direction.
  double height = 0.0;
  double lo = 0.0;
  double hi = 0.0;
  if (projected_range(geom, b, &lo, &hi)) height = hi - lo;
  Vec3 u{{1.0, 0.0, 0.0}};
  Vec3 v{{0.0, 1.0, 0.0}};
  plane_basis(b, &u, &v);
  double footprint = 0.0;
  {
    double u_lo = 0.0, u_hi = 0.0, v_lo = 0.0, v_hi = 0.0;
    const bool have_u = projected_range(geom, u, &u_lo, &u_hi);
    const bool have_v = projected_range(geom, v, &v_lo, &v_hi);
    if (have_u && have_v) {
      footprint = std::min(u_hi - u_lo, v_hi - v_lo);
    }
  }
  double p_aspect = 0.0;
  if (footprint > 0.0) {
    const double slenderness = height / footprint;
    p_aspect = std::clamp((slenderness - kSlendernessLimit) / kSlendernessLimit,
                          0.0, 1.0);
  }

  souxmar_field_t* field =
      souxmar_field_new("printability", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR,
                        geom.num_cells, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != geom.num_cells * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  const double corrosion_required = min_wall + 2.0 * corrosion_allowance;

  for (std::size_t c = 0; c < geom.num_cells; ++c) {
    // Local wall-thickness proxy — see the header for what it is worth.
    double proxy = 0.0;
    if (ds.boundary_area_m2[c] > 0.0 && geom.cell_volume[c] > 0.0) {
      proxy = std::min(2.0 * geom.cell_volume[c] / ds.boundary_area_m2[c],
                       bbox_diagonal);
    } else if (ds.boundary_area_m2[c] <= 0.0 && geom.cell_volume[c] > 0.0) {
      proxy = bbox_diagonal;  // interior bulk cell: no free surface at all
    }
    // proxy stays 0 for a surface-mesh cell (no thickness information).

    double p_overhang = 0.0;
    if (ds.down_area_m2[c] > 0.0 && threshold_deg > 0.0 &&
        ds.min_tilt_deg[c] < threshold_deg) {
      p_overhang = std::clamp((threshold_deg - ds.min_tilt_deg[c]) / threshold_deg,
                             0.0, 1.0);
    }
    double p_thin = 0.0;
    if (proxy > 0.0 && proxy < min_wall) {
      p_thin = std::clamp((min_wall - proxy) / min_wall, 0.0, 1.0);
    }
    double p_corr = 0.0;
    if (corrosion_allowance > 0.0 && proxy > 0.0 && proxy < corrosion_required) {
      p_corr = std::clamp((corrosion_required - proxy) / corrosion_required,
                          0.0, 1.0);
    }

    const double w_over = kWeightOverhang  * p_overhang;
    const double w_thin = kWeightThinWall  * p_thin;
    const double w_asp  = kWeightAspect    * p_aspect;
    const double w_corr = kWeightCorrosion * p_corr;

    double score = std::clamp(1.0 - (w_over + w_thin + w_asp + w_corr), 0.0, 1.0);
    double code  = 0.0;
    if (!fits) {
      score = 0.0;
      code  = 3.0;  // build-volume fit is a hard gate, not a weighted term
    } else {
      // Largest weighted term wins; ties break in code order.
      double best = 0.0;
      if (w_over > best) { best = w_over; code = 1.0; }
      if (w_thin > best) { best = w_thin; code = 2.0; }
      if (w_asp  > best) { best = w_asp;  code = 4.0; }
      if (w_corr > best) { best = w_corr; code = 5.0; }
    }

    data[c * 3 + 0] = score;
    data[c * 3 + 1] = code;
    data[c * 3 + 2] = proxy;
  }
  *out_field = field;
  return souxmar_status_ok();
}

// ---- solver.am.buildtime ----------------------------------------------

enum class Process : std::uint8_t {
  Lpbf = 0,
  Fff,
  DedWaam,
  Sls,
};

bool process_from_string(const char* name, Process* out) {
  const std::string_view s(name ? name : "");
  if (s == "lpbf")     { *out = Process::Lpbf;    return true; }
  if (s == "fff")      { *out = Process::Fff;     return true; }
  if (s == "ded_waam") { *out = Process::DedWaam; return true; }
  if (s == "sls")      { *out = Process::Sls;     return true; }
  return false;
}

souxmar_status_t buildtime_solve(const souxmar_mesh_t*           mesh,
                                 const souxmar_value_t*          inputs,
                                 const souxmar_solver_options_t* /*options*/,
                                 souxmar_field_t**               out_field,
                                 void*                           /*user_data*/) {
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  Vec3 b{{0.0, 0.0, 1.0}};
  if (!read_build_direction(inputs, &b)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }
  Process process = Process::Lpbf;
  if (!process_from_string(read_string(inputs, "process", "lpbf"), &process)) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "process must be one of lpbf | fff | ded_waam | sls");
  }

  const double layer_height =
      std::clamp(read_number(inputs, "layer_height", 0.001), 1e-6, 10.0);
  const double process_layer_height =
      std::clamp(read_number(inputs, "process_layer_height", 3.0e-5), 1e-7, 1.0);
  const double scan_speed =
      std::clamp(read_number(inputs, "scan_speed", 0.8), 1e-6, 1.0e4);
  const double hatch_spacing =
      std::clamp(read_number(inputs, "hatch_spacing", 1.1e-4), 1e-7, 1.0);
  const double recoat_time =
      std::clamp(read_number(inputs, "recoat_time", 8.0), 0.0, 3600.0);
  const double print_speed =
      std::clamp(read_number(inputs, "print_speed", 0.05), 1e-6, 100.0);
  const double road_width =
      std::clamp(read_number(inputs, "road_width", 4.0e-4), 1e-7, 1.0);
  const double deposition_rate =
      std::clamp(read_number(inputs, "deposition_rate", 3.0), 1e-6, 1.0e4);
  const double density =
      std::clamp(read_number(inputs, "density", 7990.0), 1.0, 3.0e4);
  // Read + range-checked for the report writer's benefit only; see the
  // header's note on the modal-stub precedent.
  (void) std::clamp(read_number(inputs, "laser_power", 200.0), 0.0, 1.0e6);
  (void) std::clamp(read_number(inputs, "machine_power_overhead", 2500.0), 0.0, 1.0e7);
  (void) std::clamp(read_number(inputs, "material_cost_per_kg", 90.0), 0.0, 1.0e7);
  (void) std::clamp(read_number(inputs, "machine_rate_per_hour", 45.0), 0.0, 1.0e7);

  MeshGeometry geom;
  const souxmar_status_t gs = build_mesh_geometry(mesh, &geom);
  if (gs.code != SOUXMAR_OK) return gs;
  const souxmar_status_t rs = reject_undecomposable(geom);
  if (rs.code != SOUXMAR_OK) return rs;

  // ---- Layer resolution (contract §2.2) ----
  double proj_lo = 0.0;
  double proj_hi = 0.0;
  if (!projected_range(geom, b, &proj_lo, &proj_hi)) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "mesh node projection failed");
  }
  std::vector<std::size_t> cell_layer(geom.num_cells, 0);
  std::size_t              max_layer = 0;
  for (std::size_t c = 0; c < geom.num_cells; ++c) {
    const int32_t tag = souxmar_mesh_cell_tag(mesh, static_cast<uint64_t>(c));
    std::size_t   layer = 0;
    if (tag >= 0) {
      // A non-negative cell tag IS the layer index — what
      // mesher.am.layered writes.
      if (static_cast<std::size_t>(tag) >= kMaxLayers) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "cell tag exceeds the 1e6 layer-index cap");
      }
      layer = static_cast<std::size_t>(tag);
    } else {
      const double p   = vec_dot(geom.cell_centroid[c], b) - proj_lo;
      const double bin = std::floor(p / layer_height);
      if (!(bin >= 0.0)) {
        layer = 0;  // NaN or below the box minimum by rounding
      } else if (bin >= static_cast<double>(kMaxLayers)) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "layer_height is too small for this mesh (over 1e6 layers)");
      } else {
        layer = static_cast<std::size_t>(bin);
      }
    }
    cell_layer[c] = layer;
    if (layer > max_layer) max_layer = layer;
  }
  const std::size_t num_layers = max_layer + 1;

  // ---- Per-layer cross-sectional area ----
  const bool volume_mesh = geom.num_volume_cells > 0;
  std::vector<double> layer_area(num_layers, 0.0);
  if (volume_mesh) {
    for (std::size_t c = 0; c < geom.num_cells; ++c) {
      layer_area[cell_layer[c]] += geom.cell_volume[c] / layer_height;
    }
  } else {
    // Surface-only mesh: |n.b|-weighted projected facet area (§3.11).
    for (const BoundaryFace& f : geom.boundary_faces) {
      if (f.cell >= geom.num_cells) continue;
      layer_area[cell_layer[f.cell]] += std::abs(vec_dot(f.normal, b)) * f.area;
    }
  }

  // ---- Per-layer time ----
  // Machine (powder / road) layers per simulation layer, rounded half-up
  // explicitly rather than through std::round so the tie case is pinned by
  // this source and not by a library's rounding mode.
  const double machine_layers = std::max(
      1.0, std::floor(layer_height / process_layer_height + 0.5));
  // kg/h -> m^3/s for the deposition-rate-limited processes.
  const double volumetric_rate = deposition_rate / (3600.0 * density);

  std::vector<double> layer_time(num_layers, 0.0);
  std::vector<double> cumulative(num_layers, 0.0);
  double running = 0.0;
  for (std::size_t l = 0; l < num_layers; ++l) {
    const double area = layer_area[l];
    double t = 0.0;
    switch (process) {
      case Process::Lpbf:
      case Process::Sls: {
        // Scanned track length = area / hatch_spacing.
        const double scan = area / (hatch_spacing * scan_speed);
        t = machine_layers * (scan + recoat_time);
        break;
      }
      case Process::Fff: {
        // Extruded road length = area / road_width; no recoater.
        t = machine_layers * area / (road_width * print_speed);
        break;
      }
      case Process::DedWaam: {
        t = (area * layer_height) / volumetric_rate;
        break;
      }
    }
    layer_time[l] = t;
    running += t;
    cumulative[l] = running;
  }

  souxmar_field_t* field =
      souxmar_field_new("buildtime", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR,
                        geom.num_cells, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != geom.num_cells * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  for (std::size_t c = 0; c < geom.num_cells; ++c) {
    const std::size_t l = cell_layer[c];
    data[c * 3 + 0] = layer_time[l];
    data[c * 3 + 1] = cumulative[l];
    data[c * 3 + 2] = layer_area[l];
  }
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kOverhangVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &overhang_solve,
    nullptr,
};

constexpr souxmar_solver_vtable_t kPrintabilityVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &printability_solve,
    nullptr,
};

constexpr souxmar_solver_vtable_t kBuildtimeVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &buildtime_solve,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s_over = souxmar_registry_add_solver(
      registry, "solver.am.overhang", &kOverhangVtable, /*user_data=*/nullptr);
  if (s_over.code != SOUXMAR_OK) return 1;
  const souxmar_status_t s_print = souxmar_registry_add_solver(
      registry, "solver.am.printability", &kPrintabilityVtable, /*user_data=*/nullptr);
  if (s_print.code != SOUXMAR_OK) return 1;
  const souxmar_status_t s_time = souxmar_registry_add_solver(
      registry, "solver.am.buildtime", &kBuildtimeVtable, /*user_data=*/nullptr);
  if (s_time.code != SOUXMAR_OK) return 1;
  return 0;
}
