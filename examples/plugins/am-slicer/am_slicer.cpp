// SPDX-License-Identifier: Apache-2.0
//
// am-slicer — additive-manufacturing output writers. Registers three writer
// capabilities (contract §3.12-§3.14, ADR-0044):
//
//   writer.am.gcode    planar slice -> FFF/FDM G-code
//   writer.am.cli      planar slice -> Common Layer Interface (CLI) ASCII
//   writer.am.report   Markdown build report / traveller sheet
//
// The slicing itself — boundary extraction, plane intersection, contour
// chaining, hole classification, deterministic ordering — lives in
// slicer.hpp/slicer.cpp and is shared by the first two. Read that header
// first; it also documents every pathological case (triangle in the plane,
// single-vertex touch, in-plane edge, open contour) and how it is handled.
//
// What it computes:
//
//   writer.am.gcode. Layer planes sit at the *mid-height* of each layer
//   (h_k = z_min + (k + 0.5)*layer_height) because a mid-height section is the
//   least-biased single-plane approximation of the material a layer actually
//   contains; the emitted `G1 Z` for that layer is its *top*
//   (z = (k+1)*layer_height above the part's lowest point), which is where an
//   FFF nozzle sits while printing it. Extrusion follows the volumetric
//   equivalence rule every FFF slicer uses (Slic3r/PrusaSlicer "extrusion
//   width x layer height" road model): a print move of length L deposits a
//   rectangular road of cross-section road_width x layer_height, so
//
//       dE = flow_multiplier * road_width * layer_height * L
//            / (pi/4 * filament_diameter^2)
//
//   with E absolute (M82) and monotone except across retractions. Infill is
//   even-odd scanline fill: contours are cut by lines spaced `infill_spacing`
//   apart on a lattice anchored at the frame origin, at +infill_angle_deg on
//   even layers and -infill_angle_deg on odd ones; crossings are sorted and
//   paired, so holes are skipped without any extra logic. Consecutive
//   scanlines are printed in opposite directions (the usual zig-zag) to cut
//   travel. Units: souxmar inputs are SI (m, m/s, °C); G-code is mm and
//   mm/min, so lengths are x1000 and speeds x60000.
//
//   writer.am.cli. Same slicing, emitted as CLI ASCII per the Common Layer
//   Interface specification (version 2.0, hence `$$VERSION/200`): the header
//   block, then `$$LAYER/z` + one `$$POLYLINE/id,dir,n,x1,y1,...` per contour.
//   `dir` is 1 for an outer (counter-clockwise) contour, 0 for a hole
//   (clockwise) and 2 for an open polyline. Closed polylines repeat their
//   first point as their last, which is the convention every CLI reader we
//   know of accepts. `$$DATE/010100` is fixed by contract — no wall clock.
//
//   writer.am.report. Markdown. Mesh summary, process parameters, a build
//   time/energy/mass/cost total, a section rendered from whichever field the
//   pipeline handed over, and a Traceability digest: FNV-1a 64-bit
//   (offset basis 14695981039346656037, prime 1099511628211) over the bit
//   patterns of every node coordinate followed by every cell's element type
//   and connectivity, fed byte-by-byte in explicit little-endian order so the
//   digest does not depend on host endianness. Build totals:
//
//       mass       = enclosed_volume * density
//       lpbf, sls  : t_active = V / (hatch_spacing * process_layer_height * scan_speed)
//                    t_total  = t_active + recoat_time * ceil(height/process_layer_height)
//       fff        : t_active = V / (road_width * layer_height * print_speed)
//       ded_waam   : t_active = mass / (deposition_rate / 3600)
//       energy     = laser_power * t_active + machine_power_overhead * t_total
//       cost       = mass * material_cost_per_kg
//                    + (t_total / 3600) * machine_rate_per_hour
//
//   When the supplied field is `buildtime`, t_total is taken from that field
//   (max cumulative_time_s) instead and the report says so — that is the
//   "totals are re-derived by writer.am.report" clause of contract §3.11.
//
// What this is NOT:
//   * Not a production slicer. Single perimeter only — no wall count, no
//     tool-radius offset compensation (the extruded bead straddles the true
//     surface by half a road width), no support generation, no bridging or
//     overhang detection, no skirt/brim, no cooling-fan or acceleration
//     control, no arc fitting, no seam placement, no retraction tuning beyond
//     one length, no collision or build-volume checking.
//   * The G-code is a geometrically faithful but *mechanically naive*
//     toolpath. Do not feed it to a machine without reading it.
//   * The report's build-time model is the same closed-form estimate as
//     `solver.am.buildtime`: uniform hatching, no scan-strategy overhead, no
//     heat-up, no recoater failure, no post-processing. Cost is machine time
//     plus feedstock only — no labour, consumables, support removal, HIP,
//     inspection or scrap.
//   * The Traceability digest is a non-cryptographic content digest for
//     spotting "is this the same mesh?", NOT a signature and NOT tamper
//     evidence.
//
// Inputs (souxmar_value_t map). Unknown keys are ignored; every number is
// clamped to the documented range and no user value is ever divided by
// unchecked.
//
//   writer.am.gcode
//     path                : string, REQUIRED
//     layer_height        : m,  default 2.0e-4   (clamped [1e-5, 1e-2])
//     road_width          : m,  default 4.0e-4   (clamped [1e-5, 1e-2])
//     filament_diameter   : m,  default 1.75e-3  (clamped [1e-4, 1e-2])
//     nozzle_temperature  : °C, default 250      (clamped [0, 500])
//     bed_temperature     : °C, default 100      (clamped [0, 250])
//     print_speed         : m/s, default 0.05    (clamped [1e-4, 1.0])
//     travel_speed        : m/s, default 0.15    (clamped [1e-4, 2.0])
//     infill_spacing      : m,  default 0.002    (<= 0 disables infill;
//                                                 else clamped [1e-5, 0.1])
//     infill_angle_deg    : °,  default 45       (clamped [-360, 360])
//     retract_length      : m,  default 0.001    (clamped [0, 1e-2]; 0 = off)
//     flow_multiplier     : -,  default 1.0      (clamped [0.1, 5.0])
//     build_direction     : list of 3, default [0, 0, 1]
//     max_layers          : int, default 5000    (clamped [1, 200000])
//
//   writer.am.cli
//     path                : string, REQUIRED
//     layer_height        : m,  default 1.0e-3   (clamped [1e-5, 1e-1])
//     units               : mm per CLI unit, default 1.0 (clamped [1e-4, 1e3])
//     build_direction     : list of 3, default [0, 0, 1]
//     label               : string, default "souxmar"
//     binary              : bool, default false; true => SOUXMAR_E_NOT_IMPLEMENTED
//
//   writer.am.report
//     path                  : string, REQUIRED
//     title                 : string, default "souxmar AM build report"
//     process               : string, default "lpbf" (lpbf|fff|ded_waam|sls)
//     material              : string, default "316L"
//     design_notes          : string, default ""
//     layer_height          : m,      default 1.0e-3
//     process_layer_height  : m,      default 3.0e-5
//     scan_speed            : m/s,    default 0.8
//     hatch_spacing         : m,      default 1.1e-4
//     recoat_time           : s,      default 8.0
//     print_speed           : m/s,    default 0.05
//     road_width            : m,      default 4.0e-4
//     deposition_rate       : kg/h,   default 3.0
//     laser_power           : W,      default 200
//     machine_power_overhead: W,      default 2500
//     material_cost_per_kg  : -,      default 90
//     machine_rate_per_hour : -,      default 45
//     density               : kg/m³,  default 7990
//     build_direction       : list of 3, default [0, 0, 1]
//
// Output: files. All three writers ignore the `field` handle except
// writer.am.report, which dispatches on souxmar_field_name(). Nothing is
// written until the whole file has been built in memory, so a rejected input
// never leaves a truncated file behind.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"
#include "souxmar-c/writer.h"

#include "slicer.hpp"

namespace {

using souxmar_am_slicer::append_fixed;
using souxmar_am_slicer::BoundaryMesh;
using souxmar_am_slicer::Contour;
using souxmar_am_slicer::format_fixed;
using souxmar_am_slicer::Frame;
using souxmar_am_slicer::LayerSlice;
using souxmar_am_slicer::Manifold;
using souxmar_am_slicer::Point2;
using souxmar_am_slicer::SliceSet;
using souxmar_am_slicer::Tolerances;
using souxmar_am_slicer::Vec3;

constexpr double kPi = 3.14159265358979323846;

// ---- Value-bag helpers (contract §2.5) ---------------------------------
// read_number / read_int are copied verbatim from
// examples/plugins/modal-stub/modal_stub.cpp; the rest follow the same shape.

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

bool read_bool(const souxmar_value_t* inputs, const char* key, bool dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v) return dv;
  if (souxmar_value_kind(v) == SOUXMAR_VK_BOOL) return souxmar_value_as_bool(v) != 0;
  // YAML `1` / `0` is a common stand-in for a flag; accept it rather than
  // silently ignoring the user's intent.
  if (souxmar_value_kind(v) == SOUXMAR_VK_NUMBER) return souxmar_value_as_number(v) != 0.0;
  return dv;
}

const char* read_string(const souxmar_value_t* inputs, const char* key, const char* dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_STRING) return dv;
  const char* s = souxmar_value_as_string(v);
  return s ? s : dv;
}

// build_direction and friends: a list of exactly 3 numbers, else the default.
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

bool has_key(const souxmar_value_t* inputs, const char* key) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return false;
  return souxmar_value_map_get(inputs, key) != nullptr;
}

// Clamps and rejects NaN (both comparisons fail, so the lo branch wins).
double clamp_num(double v, double lo, double hi) {
  if (!(v > lo)) return lo;
  if (!(v < hi)) return hi;
  return v;
}

// ---- Common writer plumbing -------------------------------------------

souxmar_status_t require_path(const souxmar_value_t* inputs, const char** out_path) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer inputs must be a map");
  }
  const souxmar_value_t* pv = souxmar_value_map_get(inputs, "path");
  if (!pv || souxmar_value_kind(pv) != SOUXMAR_VK_STRING) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer.am.* requires `path: <string>`");
  }
  const char* path = souxmar_value_as_string(pv);
  if (!path || path[0] == '\0') {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "`path` must not be empty");
  }
  *out_path = path;
  return souxmar_status_ok();
}

souxmar_status_t frame_from_inputs(const souxmar_value_t* inputs, Frame* out) {
  const Vec3 dir = read_vec3(inputs, "build_direction", Vec3{0.0, 0.0, 1.0});
  if (!souxmar_am_slicer::build_frame(dir, out)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "`build_direction` has zero length");
  }
  return souxmar_status_ok();
}

// Binary mode on purpose: "\n" must stay a single LF byte on Windows too, or
// the determinism gate sees a different file per platform.
souxmar_status_t commit_file(const char* path, const std::string& blob) {
  std::ofstream sink(path, std::ios::binary | std::ios::trunc);
  if (!sink.is_open()) {
    return souxmar_status_error(SOUXMAR_E_IO, "could not open `path` for writing");
  }
  sink.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  if (!sink.good()) {
    return souxmar_status_error(SOUXMAR_E_IO, "write to `path` failed");
  }
  return souxmar_status_ok();
}

void append_uint(std::string* out, std::uint64_t v) {
  char      buf[24];
  const int n = std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v));
  if (n > 0 && static_cast<std::size_t>(n) < sizeof buf) out->append(buf, static_cast<std::size_t>(n));
}

void append_int(std::string* out, long long v) {
  char      buf[24];
  const int n = std::snprintf(buf, sizeof buf, "%lld", v);
  if (n > 0 && static_cast<std::size_t>(n) < sizeof buf) out->append(buf, static_cast<std::size_t>(n));
}

// Keeps user text from corrupting a machine-readable line: printable ASCII
// only, no separators, bounded length.
std::string sanitise_token(const char* s, const char* fallback, std::size_t max_len) {
  std::string out;
  if (s) {
    for (std::size_t i = 0; s[i] != '\0' && out.size() < max_len; ++i) {
      const unsigned char ch = static_cast<unsigned char>(s[i]);
      // ',' '/' '$' ';' are structural in CLI and G-code; '|' and '`' would
      // break a Markdown table cell.
      const bool bad = ch < 0x20 || ch > 0x7E || ch == ',' || ch == '/' ||
                       ch == ';' || ch == '$' || ch == '|' || ch == '`';
      out.push_back(bad ? '_' : static_cast<char>(ch));
    }
  }
  if (out.empty()) out = fallback;
  return out;
}

// Free-text for Markdown: keeps UTF-8 as-is (the report is a text document),
// drops control characters, and optionally folds newlines so the string can sit
// on one line (a heading, a table cell).
std::string sanitise_text(const char* s, std::size_t max_len, bool single_line = false) {
  std::string out;
  if (!s) return out;
  for (std::size_t i = 0; s[i] != '\0' && out.size() < max_len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (ch == '\n') {
      out.push_back(single_line ? ' ' : '\n');
    } else if (ch < 0x20 || ch == 0x7F) {
      out.push_back(' ');
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  return out;
}

const char* element_type_name(std::uint16_t et) {
  switch (et) {
    case SOUXMAR_ET_VERTEX:    return "Vertex";
    case SOUXMAR_ET_EDGE2:     return "Edge2";
    case SOUXMAR_ET_EDGE3:     return "Edge3";
    case SOUXMAR_ET_TRI3:      return "Tri3";
    case SOUXMAR_ET_TRI6:      return "Tri6";
    case SOUXMAR_ET_QUAD4:     return "Quad4";
    case SOUXMAR_ET_QUAD8:     return "Quad8";
    case SOUXMAR_ET_QUAD9:     return "Quad9";
    case SOUXMAR_ET_TET4:      return "Tet4";
    case SOUXMAR_ET_TET10:     return "Tet10";
    case SOUXMAR_ET_HEX8:      return "Hex8";
    case SOUXMAR_ET_HEX20:     return "Hex20";
    case SOUXMAR_ET_HEX27:     return "Hex27";
    case SOUXMAR_ET_PRISM6:    return "Prism6";
    case SOUXMAR_ET_PRISM15:   return "Prism15";
    case SOUXMAR_ET_PYRAMID5:  return "Pyramid5";
    case SOUXMAR_ET_PYRAMID13: return "Pyramid13";
    default:                   return "Unknown";
  }
}

// Layer plan shared by the two slicing writers: planes at layer mid-heights,
// counted from the part's lowest point along the build direction.
struct LayerPlan {
  double      h0        = 0.0;
  double      dh        = 0.0;
  std::size_t count     = 0;
  bool        truncated = false;
};

LayerPlan plan_layers(const BoundaryMesh& bnd, double layer_height, std::size_t max_layers) {
  LayerPlan plan;
  plan.dh              = layer_height;
  const double height  = bnd.hi[2] - bnd.lo[2];
  // The 1e-9 slack stops a 20.000000000000004 mm / 4 mm division from
  // inventing a sixth, empty layer.
  const double raw     = std::ceil(height / layer_height - 1e-9);
  double       n       = (raw < 1.0) ? 1.0 : raw;
  if (n > static_cast<double>(max_layers)) {
    n              = static_cast<double>(max_layers);
    plan.truncated = true;
  }
  plan.count = static_cast<std::size_t>(n);
  plan.h0    = bnd.lo[2] + 0.5 * layer_height;
  return plan;
}

// =======================================================================
// writer.am.gcode
// =======================================================================

// Retraction / priming feedrate. 1800 mm/min = 30 mm/s, the stock value in
// every mainstream FFF profile; not exposed because the contract's key list
// does not include it.
constexpr double kRetractFeedMmMin = 1800.0;
// Moves shorter than a nanometre are rounding noise, not toolpath.
constexpr double kZeroMoveMm = 1e-6;
// Per-layer scanline budget. A pathological infill_spacing against a large
// part would otherwise emit gigabytes; the layer then gets perimeters only
// plus a warning comment.
constexpr std::size_t kMaxScanlinesPerLayer = 100000;

struct GcodeInputs {
  double      layer_height       = 2.0e-4;
  double      road_width         = 4.0e-4;
  double      filament_diameter  = 1.75e-3;
  double      nozzle_temperature = 250.0;
  double      bed_temperature    = 100.0;
  double      print_speed        = 0.05;
  double      travel_speed       = 0.15;
  double      infill_spacing     = 0.002;
  double      infill_angle_deg   = 45.0;
  double      retract_length     = 0.001;
  double      flow_multiplier    = 1.0;
  Vec3        build_direction{0.0, 0.0, 1.0};
  std::size_t max_layers   = 5000;
  bool        infill_on    = true;
};

GcodeInputs read_gcode_inputs(const souxmar_value_t* in) {
  GcodeInputs g;
  g.layer_height       = clamp_num(read_number(in, "layer_height", 2.0e-4), 1e-5, 1e-2);
  g.road_width         = clamp_num(read_number(in, "road_width", 4.0e-4), 1e-5, 1e-2);
  g.filament_diameter  = clamp_num(read_number(in, "filament_diameter", 1.75e-3), 1e-4, 1e-2);
  g.nozzle_temperature = clamp_num(read_number(in, "nozzle_temperature", 250.0), 0.0, 500.0);
  g.bed_temperature    = clamp_num(read_number(in, "bed_temperature", 100.0), 0.0, 250.0);
  g.print_speed        = clamp_num(read_number(in, "print_speed", 0.05), 1e-4, 1.0);
  g.travel_speed       = clamp_num(read_number(in, "travel_speed", 0.15), 1e-4, 2.0);
  const double spacing = read_number(in, "infill_spacing", 0.002);
  g.infill_on          = spacing > 0.0;
  g.infill_spacing     = g.infill_on ? clamp_num(spacing, 1e-5, 0.1) : 0.0;
  g.infill_angle_deg   = clamp_num(read_number(in, "infill_angle_deg", 45.0), -360.0, 360.0);
  g.retract_length     = clamp_num(read_number(in, "retract_length", 0.001), 0.0, 1e-2);
  g.flow_multiplier    = clamp_num(read_number(in, "flow_multiplier", 1.0), 0.1, 5.0);
  g.build_direction    = read_vec3(in, "build_direction", Vec3{0.0, 0.0, 1.0});
  g.max_layers = static_cast<std::size_t>(
      clamp_num(static_cast<double>(read_int(in, "max_layers", 5000)), 1.0, 200000.0));
  return g;
}

struct Gcode {
  std::string out;
  double      e_mm              = 0.0;  // absolute extruder position (mm)
  double      section_mm2       = 0.0;  // road cross-section (mm^2)
  double      filament_area_mm2 = 1.0;
  double      print_feed        = 0.0;  // mm/min
  double      travel_feed       = 0.0;  // mm/min
  double      retract_mm        = 0.0;
  double      last_feed         = -1.0;
  double      x_mm              = 0.0;
  double      y_mm              = 0.0;
  bool        primed            = false;  // something has been extruded
  double      filament_mm       = 0.0;
  double      print_mm          = 0.0;
  double      travel_mm         = 0.0;
  std::size_t print_moves       = 0;
  std::size_t travel_moves      = 0;

  void feed(double f) {
    if (f == last_feed) return;
    out += " F";
    append_fixed(&out, f, 0);
    last_feed = f;
  }

  void retract() {
    if (!(retract_mm > 0.0) || !primed) return;
    out += "G1 E";
    append_fixed(&out, e_mm - retract_mm, 5);
    feed(kRetractFeedMmMin);
    out += " ; retract\n";
  }

  void prime() {
    if (!(retract_mm > 0.0) || !primed) return;
    out += "G1 E";
    append_fixed(&out, e_mm, 5);
    feed(kRetractFeedMmMin);
    out += " ; prime\n";
  }

  void travel_to(double x, double y) {
    const double d = std::hypot(x - x_mm, y - y_mm);
    if (d < kZeroMoveMm) return;
    retract();
    out += "G1 X";
    append_fixed(&out, x, 3);
    out += " Y";
    append_fixed(&out, y, 3);
    feed(travel_feed);
    out += "\n";
    prime();
    travel_mm += d;
    ++travel_moves;
    x_mm = x;
    y_mm = y;
  }

  void extrude_to(double x, double y) {
    const double d = std::hypot(x - x_mm, y - y_mm);
    if (d < kZeroMoveMm) return;
    const double de = d * section_mm2 / filament_area_mm2;
    e_mm += de;
    filament_mm += de;
    print_mm += d;
    ++print_moves;
    out += "G1 X";
    append_fixed(&out, x, 3);
    out += " Y";
    append_fixed(&out, y, 3);
    out += " E";
    append_fixed(&out, e_mm, 5);
    feed(print_feed);
    out += "\n";
    x_mm   = x;
    y_mm   = y;
    primed = true;
  }
};

void gc_param(std::string* out, const char* key, const std::string& value, const char* unit) {
  *out += ";   ";
  *out += key;
  *out += " = ";
  *out += value;
  if (unit && unit[0] != '\0') {
    *out += " ";
    *out += unit;
  }
  *out += "\n";
}

struct ScanLine {
  Point2 a;
  Point2 b;
};

// Even-odd scanline infill over the layer's closed contours. `angle_deg` is
// measured from the +u axis; lines sit on the global lattice s = k*spacing,
// s being the coordinate across the scan direction, so the pattern does not
// wander when the part moves.
std::vector<ScanLine> build_infill(const LayerSlice& ls,
                                   double            angle_deg,
                                   double            spacing,
                                   bool*             out_capped) {
  std::vector<ScanLine> lines;
  *out_capped = false;
  if (!(spacing > 0.0)) return lines;

  const double a  = angle_deg * kPi / 180.0;
  const double dx = std::cos(a);
  const double dy = std::sin(a);
  const double nx = -dy;
  const double ny = dx;

  double s_min = 0.0;
  double s_max = 0.0;
  bool   first = true;
  for (const Contour& c : ls.contours) {
    if (!c.closed) continue;  // an open contour bounds nothing
    for (const Point2& p : c.points) {
      const double s = p.u * nx + p.v * ny;
      if (first) {
        s_min = s;
        s_max = s;
        first = false;
      } else {
        s_min = std::min(s_min, s);
        s_max = std::max(s_max, s);
      }
    }
  }
  if (first) return lines;

  const double k0d = std::ceil(s_min / spacing);
  const double k1d = std::floor(s_max / spacing);
  if (!(k1d >= k0d)) return lines;
  if ((k1d - k0d) + 1.0 > static_cast<double>(kMaxScanlinesPerLayer)) {
    *out_capped = true;
    return lines;
  }
  const long long k0 = static_cast<long long>(k0d);
  const long long k1 = static_cast<long long>(k1d);

  std::vector<std::pair<double, std::uint32_t>> crossings;
  for (long long k = k0; k <= k1; ++k) {
    const double s = static_cast<double>(k) * spacing;
    crossings.clear();
    std::uint32_t edge_id = 0;
    for (const Contour& c : ls.contours) {
      if (!c.closed) continue;
      const std::size_t n = c.points.size();
      for (std::size_t i = 0; i < n; ++i) {
        const Point2& p0 = c.points[i];
        const Point2& p1 = c.points[(i + 1) % n];
        const double  s0 = p0.u * nx + p0.v * ny;
        const double  s1 = p1.u * nx + p1.v * ny;
        // Half-open rule: an edge counts when s is in [s0, s1), so a vertex
        // exactly on the scanline is counted by exactly one of its two edges
        // and the crossings still come in pairs.
        const bool hit = (s0 <= s && s < s1) || (s1 <= s && s < s0);
        if (hit) {
          const double t  = (s - s0) / (s1 - s0);
          const double pu = p0.u + t * (p1.u - p0.u);
          const double pv = p0.v + t * (p1.v - p0.v);
          crossings.emplace_back(pu * dx + pv * dy, edge_id);
        }
        ++edge_id;
      }
    }
    // (coordinate, edge id) is a total order — determinism gate.
    std::stable_sort(crossings.begin(), crossings.end(),
                     [](const std::pair<double, std::uint32_t>& l,
                        const std::pair<double, std::uint32_t>& r) {
                       if (l.first != r.first) return l.first < r.first;
                       return l.second < r.second;
                     });
    for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
      const double c0 = crossings[i].first;
      const double c1 = crossings[i + 1].first;
      if (c1 - c0 < 1e-9) continue;  // sliver: below a nanometre of travel
      ScanLine line;
      line.a = Point2{c0 * dx + s * nx, c0 * dy + s * ny};
      line.b = Point2{c1 * dx + s * nx, c1 * dy + s * ny};
      // Zig-zag: reverse every other line so the nozzle walks back and forth
      // instead of returning to the same side each time.
      if (((k - k0) % 2) != 0) std::swap(line.a, line.b);
      lines.push_back(line);
    }
  }
  return lines;
}

souxmar_status_t gcode_write_impl(const souxmar_mesh_t*  mesh,
                                 const souxmar_value_t* inputs) {
  const char*      path = nullptr;
  souxmar_status_t st   = require_path(inputs, &path);
  if (st.code != SOUXMAR_OK) return st;

  const GcodeInputs g = read_gcode_inputs(inputs);
  Frame             frame;
  if (!souxmar_am_slicer::build_frame(g.build_direction, &frame)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "`build_direction` has zero length");
  }

  BoundaryMesh bnd;
  st = souxmar_am_slicer::extract_boundary(mesh, frame, &bnd);
  if (st.code != SOUXMAR_OK) return st;
  if (bnd.triangles.empty()) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "writer.am.gcode: mesh has no triangulatable boundary (need Tri3/Quad4 "
        "shells or Tet4/Hex8/Prism6/Pyramid5 volume cells)");
  }
  if (!(bnd.hi[2] - bnd.lo[2] > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer.am.gcode: mesh has zero extent along the build direction");
  }

  const LayerPlan  plan = plan_layers(bnd, g.layer_height, g.max_layers);
  const Tolerances tol  = souxmar_am_slicer::tolerances_for(bnd);
  const SliceSet   ss =
      souxmar_am_slicer::slice_uniform(bnd, plan.h0, plan.dh, plan.count, tol);

  const double lh_mm       = g.layer_height * 1000.0;
  const double rw_mm       = g.road_width * 1000.0;
  const double fil_mm      = g.filament_diameter * 1000.0;
  const double fil_area    = kPi / 4.0 * fil_mm * fil_mm;  // mm^2
  const double spacing_mm  = g.infill_spacing * 1000.0;

  Gcode gc;
  gc.filament_area_mm2 = fil_area;
  gc.section_mm2       = rw_mm * lh_mm * g.flow_multiplier;
  gc.print_feed        = g.print_speed * 60000.0;   // m/s -> mm/min
  gc.travel_feed       = g.travel_speed * 60000.0;
  gc.retract_mm        = g.retract_length * 1000.0;
  gc.out.reserve(4096);

  // ---- header ---------------------------------------------------------
  std::string& o = gc.out;
  o += "; souxmar writer.am.gcode (am-slicer example plugin)\n";
  o += "; FFF/FDM toolpath from a planar slice of the pipeline mesh.\n";
  o += "; Units: mm and mm/min (G-code convention); souxmar inputs are SI.\n";
  o += "; Coordinates are relative to the part bounding box minimum, so the\n";
  o += "; part corner sits at X0 Y0 and the first layer top is at Z";
  append_fixed(&o, lh_mm, 3);
  o += ".\n";
  o += "; No bed centring, no offsets, no build-volume check.\n";
  o += ";\n; parameters\n";
  gc_param(&o, "layer_height", format_fixed(lh_mm, 4), "mm");
  gc_param(&o, "road_width", format_fixed(rw_mm, 4), "mm");
  gc_param(&o, "filament_diameter", format_fixed(fil_mm, 4), "mm");
  gc_param(&o, "filament_cross_section", format_fixed(fil_area, 4), "mm^2");
  gc_param(&o, "nozzle_temperature", format_fixed(g.nozzle_temperature, 1), "degC");
  gc_param(&o, "bed_temperature", format_fixed(g.bed_temperature, 1), "degC");
  gc_param(&o, "print_speed", format_fixed(g.print_speed, 4) + " m/s = " +
                                  format_fixed(gc.print_feed, 0),
           "mm/min");
  gc_param(&o, "travel_speed", format_fixed(g.travel_speed, 4) + " m/s = " +
                                   format_fixed(gc.travel_feed, 0),
           "mm/min");
  gc_param(&o, "infill_spacing",
           g.infill_on ? format_fixed(spacing_mm, 4) : std::string("disabled"),
           g.infill_on ? "mm" : "");
  gc_param(&o, "infill_angle_deg", format_fixed(g.infill_angle_deg, 3), "deg (alternating +/-)");
  gc_param(&o, "retract_length", format_fixed(gc.retract_mm, 4), "mm");
  gc_param(&o, "retract_speed", format_fixed(kRetractFeedMmMin, 0), "mm/min (fixed)");
  gc_param(&o, "flow_multiplier", format_fixed(g.flow_multiplier, 4), "-");
  gc_param(&o, "build_direction",
           "[" + format_fixed(g.build_direction[0], 6) + ", " +
               format_fixed(g.build_direction[1], 6) + ", " +
               format_fixed(g.build_direction[2], 6) + "]",
           "");
  gc_param(&o, "max_layers", format_fixed(static_cast<double>(g.max_layers), 0), "");
  o += ";\n; slice\n";
  gc_param(&o, "boundary_facets", format_fixed(static_cast<double>(bnd.triangles.size()), 0), "");
  gc_param(&o, "source", bnd.from_shell ? "shell mesh (Tri3/Quad4)" : "volume mesh boundary", "");
  gc_param(&o, "part_height", format_fixed((bnd.hi[2] - bnd.lo[2]) * 1000.0, 4), "mm");
  gc_param(&o, "layers", format_fixed(static_cast<double>(plan.count), 0), "");
  gc_param(&o, "closed_contours", format_fixed(static_cast<double>(ss.closed_contours), 0), "");
  gc_param(&o, "open_contours", format_fixed(static_cast<double>(ss.open_contours), 0), "");
  gc_param(&o, "coplanar_triangles_skipped",
           format_fixed(static_cast<double>(ss.coplanar_triangles), 0), "");
  gc_param(&o, "single_vertex_touches_dropped",
           format_fixed(static_cast<double>(ss.vertex_touches), 0), "");
  gc_param(&o, "duplicate_segments_dropped",
           format_fixed(static_cast<double>(ss.duplicate_dropped), 0), "");
  gc_param(&o, "degenerate_segments_dropped",
           format_fixed(static_cast<double>(ss.degenerate_dropped), 0), "");
  if (plan.truncated) {
    o += "; WARNING: the part needs more than max_layers layers; the toolpath\n"
         "; stops at the layer budget and the top of the part is missing.\n";
  }
  if (bnd.manifold == Manifold::Open) {
    o += "; WARNING: the input shell is not edge-manifold (some edge is used by\n"
         "; a number of facets other than two). Expect open contours below.\n";
  }
  if (ss.open_contours > 0) {
    o += "; WARNING: ";
    append_uint(&o, static_cast<std::uint64_t>(ss.open_contours));
    // ASCII only in the machine-readable files: some firmware chokes on a
    // stray multi-byte character even inside a comment.
    o += " open contour(s) across the slice - the sliced boundary is not\n"
         "; closed there. They are printed as open paths, not repaired.\n";
  }
  o += ";\n; limitations: single perimeter, no tool-radius offset, no support,\n"
       "; no bridging, no cooling control, no seam placement. Read before use.\n";
  o += ";\n";

  // ---- start-up -------------------------------------------------------
  o += "G21 ; millimetres\n";
  o += "M104 S";
  append_fixed(&o, g.nozzle_temperature, 0);
  o += " ; nozzle target\n";
  o += "M140 S";
  append_fixed(&o, g.bed_temperature, 0);
  o += " ; bed target\n";
  o += "G28 ; home all axes\n";
  o += "G90 ; absolute XYZ\n";
  o += "M82 ; absolute E\n";
  o += "G92 E0 ; reset the extruder origin\n";

  // ---- layers ---------------------------------------------------------
  for (std::size_t k = 0; k < ss.layers.size(); ++k) {
    const LayerSlice& ls = ss.layers[k];
    const double      z_mm =
        (static_cast<double>(k) + 1.0) * lh_mm;  // layer top, part base at Z0

    o += "; ---- layer ";
    append_uint(&o, static_cast<std::uint64_t>(k));
    o += "/";
    append_uint(&o, static_cast<std::uint64_t>(ss.layers.size()));
    o += " plane h=";
    append_fixed(&o, (ls.h - bnd.lo[2]) * 1000.0, 4);
    o += "mm contours=";
    append_uint(&o, static_cast<std::uint64_t>(ls.contours.size()));
    o += "\n";
    if (ls.contours.empty()) {
      o += "; (empty layer: the plane misses the part)\n";
      continue;
    }
    if (ls.open_contours > 0) {
      o += "; WARNING: layer ";
      append_uint(&o, static_cast<std::uint64_t>(k));
      o += " has ";
      append_uint(&o, static_cast<std::uint64_t>(ls.open_contours));
      o += " open contour(s); printed as open path(s).\n";
    }

    o += "G1 Z";
    append_fixed(&o, z_mm, 3);
    gc.feed(gc.travel_feed);
    o += "\n";

    for (std::size_t ci = 0; ci < ls.contours.size(); ++ci) {
      const Contour& c = ls.contours[ci];
      o += "; perimeter ";
      append_uint(&o, static_cast<std::uint64_t>(ci + 1));
      o += "/";
      append_uint(&o, static_cast<std::uint64_t>(ls.contours.size()));
      o += " (";
      o += c.closed ? (c.hole ? "hole, closed" : "outer, closed") : "OPEN";
      o += ", ";
      append_uint(&o, static_cast<std::uint64_t>(c.points.size()));
      o += " pts, ";
      append_fixed(&o, c.perimeter * 1000.0, 4);
      o += " mm)\n";
      const auto to_x = [&](const Point2& p) { return (p.u - bnd.lo[0]) * 1000.0; };
      const auto to_y = [&](const Point2& p) { return (p.v - bnd.lo[1]) * 1000.0; };
      gc.travel_to(to_x(c.points[0]), to_y(c.points[0]));
      for (std::size_t i = 1; i < c.points.size(); ++i) {
        gc.extrude_to(to_x(c.points[i]), to_y(c.points[i]));
      }
      if (c.closed) gc.extrude_to(to_x(c.points[0]), to_y(c.points[0]));
    }

    if (g.infill_on) {
      // Contract §3.12: alternating +/- infill_angle_deg per layer.
      const double angle  = (k % 2 == 0) ? g.infill_angle_deg : -g.infill_angle_deg;
      bool         capped = false;
      const std::vector<ScanLine> lines =
          build_infill(ls, angle, g.infill_spacing, &capped);
      if (capped) {
        o += "; WARNING: infill_spacing is too fine for this cross-section "
             "(> ";
        append_uint(&o, static_cast<std::uint64_t>(kMaxScanlinesPerLayer));
        o += " scanlines); infill skipped on this layer.\n";
      } else if (!lines.empty()) {
        o += "; infill angle=";
        append_fixed(&o, angle, 3);
        o += "deg spacing=";
        append_fixed(&o, spacing_mm, 4);
        o += "mm lines=";
        append_uint(&o, static_cast<std::uint64_t>(lines.size()));
        o += "\n";
        for (const ScanLine& line : lines) {
          gc.travel_to((line.a.u - bnd.lo[0]) * 1000.0, (line.a.v - bnd.lo[1]) * 1000.0);
          gc.extrude_to((line.b.u - bnd.lo[0]) * 1000.0, (line.b.v - bnd.lo[1]) * 1000.0);
        }
      }
    }
  }

  // ---- trailer --------------------------------------------------------
  o += "; ---- end of print\n";
  gc.retract();
  o += "M104 S0 ; nozzle heater off\n";
  o += "M140 S0 ; bed heater off\n";
  o += "M84 ; disable steppers\n";
  o += "; totals: print moves=";
  append_uint(&o, static_cast<std::uint64_t>(gc.print_moves));
  o += " travel moves=";
  append_uint(&o, static_cast<std::uint64_t>(gc.travel_moves));
  o += "\n; totals: extruded path=";
  append_fixed(&o, gc.print_mm, 3);
  o += " mm, travel=";
  append_fixed(&o, gc.travel_mm, 3);
  o += " mm\n; totals: filament=";
  append_fixed(&o, gc.filament_mm, 4);
  o += " mm = ";
  append_fixed(&o, gc.filament_mm * fil_area, 4);
  o += " mm^3 deposited\n";

  return commit_file(path, gc.out);
}

// =======================================================================
// writer.am.cli
// =======================================================================

// A CLI file with more layers than this is almost certainly a mistaken
// layer_height; refusing beats writing a multi-gigabyte file.
constexpr std::size_t kMaxCliLayers = 100000;

souxmar_status_t cli_write_impl(const souxmar_mesh_t*  mesh,
                               const souxmar_value_t* inputs) {
  const char*      path = nullptr;
  souxmar_status_t st   = require_path(inputs, &path);
  if (st.code != SOUXMAR_OK) return st;

  if (read_bool(inputs, "binary", false)) {
    return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED,
                                "writer.am.cli: `binary: true` is not implemented; "
                                "only CLI ASCII is emitted");
  }

  const double layer_height = clamp_num(read_number(inputs, "layer_height", 1.0e-3), 1e-5, 1e-1);
  // `units` is the length of one CLI unit in millimetres (CLI $$UNITS).
  const double units_mm = clamp_num(read_number(inputs, "units", 1.0), 1e-4, 1e3);
  const std::string label = sanitise_token(read_string(inputs, "label", "souxmar"), "souxmar", 64);

  Frame frame;
  st = frame_from_inputs(inputs, &frame);
  if (st.code != SOUXMAR_OK) return st;

  BoundaryMesh bnd;
  st = souxmar_am_slicer::extract_boundary(mesh, frame, &bnd);
  if (st.code != SOUXMAR_OK) return st;
  if (bnd.triangles.empty()) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "writer.am.cli: mesh has no triangulatable boundary (need Tri3/Quad4 "
        "shells or Tet4/Hex8/Prism6/Pyramid5 volume cells)");
  }
  if (!(bnd.hi[2] - bnd.lo[2] > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer.am.cli: mesh has zero extent along the build direction");
  }

  const LayerPlan plan = plan_layers(bnd, layer_height, kMaxCliLayers);
  if (plan.truncated) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer.am.cli: `layer_height` is too small for this part "
                                "(more than 100000 layers)");
  }
  const Tolerances tol = souxmar_am_slicer::tolerances_for(bnd);
  const SliceSet   ss =
      souxmar_am_slicer::slice_uniform(bnd, plan.h0, plan.dh, plan.count, tol);

  // metres -> CLI units. `units_mm` is clamped above, so this never divides by
  // zero.
  const double scale = 1000.0 / units_mm;
  const auto   cu    = [&](double metres) { return metres * scale; };

  std::string o;
  o.reserve(4096);
  o += "$$HEADERSTART\n";
  o += "$$ASCII\n";
  o += "$$UNITS/";
  append_fixed(&o, units_mm, 6);
  o += "\n";
  o += "$$VERSION/200\n";
  o += "$$LABEL/1," + label + "\n";
  // Fixed by contract: a wall-clock date would break byte-reproducibility.
  o += "$$DATE/010100\n";
  o += "$$DIMENSION/";
  append_fixed(&o, 0.0, 6);
  o += ",";
  append_fixed(&o, 0.0, 6);
  o += ",";
  append_fixed(&o, 0.0, 6);
  o += ",";
  append_fixed(&o, cu(bnd.hi[0] - bnd.lo[0]), 6);
  o += ",";
  append_fixed(&o, cu(bnd.hi[1] - bnd.lo[1]), 6);
  o += ",";
  append_fixed(&o, cu(bnd.hi[2] - bnd.lo[2]), 6);
  o += "\n";
  o += "$$LAYERS/";
  append_uint(&o, static_cast<std::uint64_t>(ss.layers.size()));
  o += "\n";
  o += "$$HEADEREND\n";
  o += "$$GEOMETRYSTART\n";

  std::size_t polylines = 0;
  for (std::size_t k = 0; k < ss.layers.size(); ++k) {
    const LayerSlice& ls = ss.layers[k];
    // CLI layer height is the *top* of the layer, matching writer.am.gcode.
    o += "$$LAYER/";
    append_fixed(&o, cu((static_cast<double>(k) + 1.0) * layer_height), 6);
    o += "\n";
    for (const Contour& c : ls.contours) {
      // dir: 1 = counter-clockwise outer contour, 0 = clockwise hole,
      // 2 = open polyline (the CLI spec's "open line" direction code).
      const int  dir     = !c.closed ? 2 : (c.hole ? 0 : 1);
      const bool repeats = c.closed;  // closed polylines repeat point 1 last
      const std::size_t n = c.points.size() + (repeats ? 1u : 0u);
      o += "$$POLYLINE/1,";
      append_int(&o, dir);
      o += ",";
      append_uint(&o, static_cast<std::uint64_t>(n));
      for (std::size_t i = 0; i < n; ++i) {
        const Point2& p = c.points[i % c.points.size()];
        o += ",";
        append_fixed(&o, cu(p.u - bnd.lo[0]), 6);
        o += ",";
        append_fixed(&o, cu(p.v - bnd.lo[1]), 6);
      }
      o += "\n";
      ++polylines;
    }
  }
  o += "$$GEOMETRYEND\n";

  // Diagnostics as CLI comments ("//"), placed after $$GEOMETRYEND so the
  // mandated key order at the top of the file is untouched.
  o += "// souxmar writer.am.cli (am-slicer example plugin)\n";
  o += "// coordinates are relative to the part bounding box minimum\n";
  o += "// layer_height = " + format_fixed(layer_height * 1000.0, 6) + " mm\n";
  o += "// polylines = " + format_fixed(static_cast<double>(polylines), 0) +
       ", closed = " + format_fixed(static_cast<double>(ss.closed_contours), 0) +
       ", open = " + format_fixed(static_cast<double>(ss.open_contours), 0) + "\n";
  o += "// boundary facets = " + format_fixed(static_cast<double>(bnd.triangles.size()), 0) +
       ", coplanar skipped = " + format_fixed(static_cast<double>(ss.coplanar_triangles), 0) +
       ", vertex touches = " + format_fixed(static_cast<double>(ss.vertex_touches), 0) +
       ", duplicates = " + format_fixed(static_cast<double>(ss.duplicate_dropped), 0) +
       ", degenerate = " + format_fixed(static_cast<double>(ss.degenerate_dropped), 0) + "\n";
  if (bnd.manifold == Manifold::Open) {
    o += "// WARNING: input shell is not edge-manifold\n";
  }
  if (ss.open_contours > 0) {
    o += "// WARNING: open contours were emitted with dir=2; the sliced "
         "boundary is not closed there\n";
  }

  return commit_file(path, o);
}

// =======================================================================
// writer.am.report
// =======================================================================

// FNV-1a 64-bit. Bytes are fed in explicit little-endian order so the digest
// is independent of host endianness. Non-cryptographic by design.
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime  = 1099511628211ULL;

void fnv1a_byte(std::uint64_t* h, std::uint8_t b) {
  *h ^= static_cast<std::uint64_t>(b);
  *h *= kFnvPrime;
}

void fnv1a_u64(std::uint64_t* h, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) fnv1a_byte(h, static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}

void fnv1a_double(std::uint64_t* h, double d) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof bits);
  fnv1a_u64(h, bits);
}

std::string hex64(std::uint64_t v) {
  char      buf[24];
  const int n = std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  if (n <= 0 || static_cast<std::size_t>(n) >= sizeof buf) return std::string("0");
  return std::string(buf, static_cast<std::size_t>(n));
}

struct FieldView {
  const double* data     = nullptr;
  std::size_t   count    = 0;
  std::size_t   comps    = 0;
  std::size_t   steps    = 0;
  std::uint8_t  location = SOUXMAR_FL_NODAL;
  const char*   name     = "";

  double at(std::size_t loc, std::size_t comp, std::size_t step) const {
    return data[step * count * comps + loc * comps + comp];
  }
};

// House rule: souxmar_field_data_size() is verified against
// count * components * num_time_steps before anything is read.
bool make_field_view(const souxmar_field_t* f, FieldView* out) {
  if (!f) return false;
  const double* data = souxmar_field_data_const(f);
  if (!data) return false;
  FieldView v;
  v.data     = data;
  v.count    = souxmar_field_count(f);
  v.comps    = souxmar_field_components(f);
  v.steps    = souxmar_field_num_time_steps(f);
  v.location = souxmar_field_location(f);
  v.name     = souxmar_field_name(f) ? souxmar_field_name(f) : "";
  if (v.count == 0 || v.comps == 0 || v.steps == 0) return false;
  if (souxmar_field_data_size(f) != v.count * v.comps * v.steps) return false;
  *out = v;
  return true;
}

constexpr std::size_t kAllSteps = static_cast<std::size_t>(-1);

struct Stats {
  double min   = 0.0;
  double max   = 0.0;
  double mean  = 0.0;
  bool   valid = false;
};

// Accumulated in index order (step, location) — never reordered, per the
// determinism rule on accumulation order.
Stats comp_stats(const FieldView& f, std::size_t comp, std::size_t step) {
  Stats s;
  if (comp >= f.comps) return s;
  double      sum = 0.0;
  std::size_t n   = 0;
  const std::size_t s0 = (step == kAllSteps) ? 0 : step;
  const std::size_t s1 = (step == kAllSteps) ? f.steps : std::min(step + 1, f.steps);
  for (std::size_t st = s0; st < s1; ++st) {
    for (std::size_t i = 0; i < f.count; ++i) {
      const double v = f.at(i, comp, st);
      if (!std::isfinite(v)) continue;  // NaN metrics exist (mesh-quality) — skip
      if (n == 0) {
        s.min = v;
        s.max = v;
      } else {
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
      }
      sum += v;
      ++n;
    }
  }
  if (n == 0) return s;
  s.mean  = sum / static_cast<double>(n);
  s.valid = true;
  return s;
}

std::size_t count_ge(const FieldView& f, std::size_t comp, std::size_t step, double threshold) {
  std::size_t n = 0;
  if (comp >= f.comps) return n;
  const std::size_t st = std::min(step, f.steps - 1);
  for (std::size_t i = 0; i < f.count; ++i) {
    const double v = f.at(i, comp, st);
    if (std::isfinite(v) && v >= threshold) ++n;
  }
  return n;
}

void row4(std::string* o, const char* label, const std::string& a, const std::string& b,
          const std::string& c) {
  *o += "| ";
  *o += label;
  *o += " | ";
  *o += a;
  *o += " | ";
  *o += b;
  *o += " | ";
  *o += c;
  *o += " |\n";
}

void kv_row(std::string* o, const char* key, const std::string& value, const char* unit,
            bool defaulted) {
  *o += "| `";
  *o += key;
  *o += "` | ";
  *o += value;
  *o += " | ";
  *o += (unit ? unit : "");
  *o += " | ";
  *o += defaulted ? "default" : "supplied";
  *o += " |\n";
}

void stats_table(std::string* o, const FieldView& f, const char* const* comp_names,
                 const char* const* comp_units, int decimals) {
  *o += "\n| component | min | mean | max | unit |\n|---|---|---|---|---|\n";
  for (std::size_t c = 0; c < f.comps; ++c) {
    const Stats s = comp_stats(f, c, kAllSteps);
    *o += "| ";
    *o += comp_names ? comp_names[c] : "component";
    *o += " | ";
    *o += s.valid ? format_fixed(s.min, decimals) : std::string("n/a");
    *o += " | ";
    *o += s.valid ? format_fixed(s.mean, decimals) : std::string("n/a");
    *o += " | ";
    *o += s.valid ? format_fixed(s.max, decimals) : std::string("n/a");
    *o += " | ";
    *o += comp_units ? comp_units[c] : "-";
    *o += " |\n";
  }
}

// Per-step min/mean/max for a scalar history. Long histories are truncated in
// the middle (first 12 + last 12 steps) so a 5000-layer field stays readable;
// the truncation is announced.
void step_table(std::string* o, const FieldView& f, const char* value_label, int decimals) {
  *o += "\n| step | min | mean | max |\n|---|---|---|---|\n";
  const std::size_t head = 12;
  const std::size_t tail = 12;
  const bool        trunc = f.steps > head + tail;
  for (std::size_t st = 0; st < f.steps; ++st) {
    if (trunc && st == head) {
      *o += "| ... | ... | ... | ... |\n";
      st = f.steps - tail - 1;
      continue;
    }
    const Stats s = comp_stats(f, 0, st);
    *o += "| ";
    append_uint(o, static_cast<std::uint64_t>(st));
    *o += " | ";
    *o += s.valid ? format_fixed(s.min, decimals) : std::string("n/a");
    *o += " | ";
    *o += s.valid ? format_fixed(s.mean, decimals) : std::string("n/a");
    *o += " | ";
    *o += s.valid ? format_fixed(s.max, decimals) : std::string("n/a");
    *o += " |\n";
  }
  *o += "\nValues are ";
  *o += value_label;
  if (trunc) *o += "; middle steps omitted for length";
  *o += ".\n";
}

struct ReportInputs {
  std::string process   = "lpbf";
  std::string material  = "316L";
  std::string title     = "souxmar AM build report";
  std::string notes;
  double      layer_height           = 1.0e-3;
  double      process_layer_height   = 3.0e-5;
  double      scan_speed             = 0.8;
  double      hatch_spacing          = 1.1e-4;
  double      recoat_time            = 8.0;
  double      print_speed            = 0.05;
  double      road_width             = 4.0e-4;
  double      deposition_rate        = 3.0;  // kg/h
  double      laser_power            = 200.0;
  double      machine_power_overhead = 2500.0;
  double      material_cost_per_kg   = 90.0;
  double      machine_rate_per_hour  = 45.0;
  double      density                = 7990.0;
  Vec3        build_direction{0.0, 0.0, 1.0};
};

ReportInputs read_report_inputs(const souxmar_value_t* in) {
  ReportInputs r;
  r.title = sanitise_text(read_string(in, "title", "souxmar AM build report"), 200,
                          /*single_line=*/true);
  if (r.title.empty()) r.title = "souxmar AM build report";
  r.process  = sanitise_token(read_string(in, "process", "lpbf"), "lpbf", 32);
  r.material = sanitise_token(read_string(in, "material", "316L"), "316L", 64);
  r.notes    = sanitise_text(read_string(in, "design_notes", ""), 4000);
  r.layer_height         = clamp_num(read_number(in, "layer_height", 1.0e-3), 1e-6, 1.0);
  r.process_layer_height = clamp_num(read_number(in, "process_layer_height", 3.0e-5), 1e-7, 1.0);
  r.scan_speed           = clamp_num(read_number(in, "scan_speed", 0.8), 1e-4, 100.0);
  r.hatch_spacing        = clamp_num(read_number(in, "hatch_spacing", 1.1e-4), 1e-7, 1.0);
  r.recoat_time          = clamp_num(read_number(in, "recoat_time", 8.0), 0.0, 3600.0);
  r.print_speed          = clamp_num(read_number(in, "print_speed", 0.05), 1e-5, 10.0);
  r.road_width           = clamp_num(read_number(in, "road_width", 4.0e-4), 1e-6, 0.1);
  r.deposition_rate      = clamp_num(read_number(in, "deposition_rate", 3.0), 1e-4, 1000.0);
  r.laser_power          = clamp_num(read_number(in, "laser_power", 200.0), 0.0, 1e6);
  r.machine_power_overhead =
      clamp_num(read_number(in, "machine_power_overhead", 2500.0), 0.0, 1e7);
  r.material_cost_per_kg = clamp_num(read_number(in, "material_cost_per_kg", 90.0), 0.0, 1e7);
  r.machine_rate_per_hour = clamp_num(read_number(in, "machine_rate_per_hour", 45.0), 0.0, 1e7);
  r.density              = clamp_num(read_number(in, "density", 7990.0), 1.0, 30000.0);
  r.build_direction      = read_vec3(in, "build_direction", Vec3{0.0, 0.0, 1.0});
  return r;
}

struct Totals {
  double      volume_m3      = 0.0;
  double      mass_kg        = 0.0;
  double      height_m       = 0.0;
  double      active_time_s  = 0.0;
  double      total_time_s   = 0.0;
  double      energy_j       = 0.0;
  double      material_cost  = 0.0;
  double      machine_cost   = 0.0;
  std::size_t machine_layers = 0;
  const char* time_source    = "closed-form process model";
};

// Machine layers are only ever used to multiply a recoat time; a billion is
// already absurd and keeps the ceil() out of size_t overflow territory.
constexpr double kMaxMachineLayers = 1e9;

std::size_t layer_count_for(double height_m, double step_m) {
  const double n = std::ceil(height_m / step_m);
  if (!(n > 0.0)) return 0;
  return static_cast<std::size_t>(std::min(n, kMaxMachineLayers));
}

// `field_total_time_s <= 0` means "no buildtime field was supplied"; otherwise
// it wins over the closed-form estimate (contract §3.11: the writer re-derives
// the totals from the solver's own cumulative time).
Totals compute_totals(const ReportInputs& r, double volume_m3, double height_m,
                      double field_total_time_s) {
  Totals t;
  t.volume_m3 = volume_m3;
  t.height_m  = height_m;
  t.mass_kg   = volume_m3 * r.density;

  if (r.process == "fff") {
    // Deposition cross-section = road_width x layer_height.
    t.machine_layers = layer_count_for(height_m, r.layer_height);
    t.active_time_s  = volume_m3 / (r.road_width * r.layer_height * r.print_speed);
    t.total_time_s   = t.active_time_s;
  } else if (r.process == "ded_waam") {
    // Mass-flow limited: deposition_rate is kg/h.
    t.machine_layers = layer_count_for(height_m, r.layer_height);
    t.active_time_s  = t.mass_kg / (r.deposition_rate / 3600.0);
    t.total_time_s   = t.active_time_s;
  } else {
    // lpbf / sls (and anything unrecognised): hatched powder-bed scanning
    // plus one recoat per machine layer.
    t.machine_layers = layer_count_for(height_m, r.process_layer_height);
    t.active_time_s =
        volume_m3 / (r.hatch_spacing * r.process_layer_height * r.scan_speed);
    t.total_time_s =
        t.active_time_s + r.recoat_time * static_cast<double>(t.machine_layers);
  }
  if (!(t.total_time_s > 0.0)) t.total_time_s = t.active_time_s;
  if (field_total_time_s > 0.0) {
    t.total_time_s  = field_total_time_s;
    t.active_time_s = std::min(t.active_time_s, t.total_time_s);
    t.time_source   = "`buildtime` field (max cumulative_time_s)";
  }
  t.energy_j      = r.laser_power * t.active_time_s + r.machine_power_overhead * t.total_time_s;
  t.material_cost = t.mass_kg * r.material_cost_per_kg;
  t.machine_cost  = (t.total_time_s / 3600.0) * r.machine_rate_per_hour;
  return t;
}

void report_field_section(std::string* o, const souxmar_field_t* field);

souxmar_status_t report_write_impl(const souxmar_mesh_t*  mesh,
                                  const souxmar_field_t* field,
                                  const souxmar_value_t* inputs) {
  const char*      path = nullptr;
  souxmar_status_t st   = require_path(inputs, &path);
  if (st.code != SOUXMAR_OK) return st;

  const ReportInputs r = read_report_inputs(inputs);
  Frame              frame;
  if (!souxmar_am_slicer::build_frame(r.build_direction, &frame)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "`build_direction` has zero length");
  }
  BoundaryMesh bnd;
  st = souxmar_am_slicer::extract_boundary(mesh, frame, &bnd);
  if (st.code != SOUXMAR_OK) return st;

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);

  // Element histogram in a std::map: ordered by element-type id, so the table
  // is identical on every platform.
  std::map<std::uint16_t, std::size_t> histogram;
  std::uint64_t                        digest = kFnvOffset;
  std::size_t                          flat   = 0;
  const double*                        coords = souxmar_mesh_nodes_flat(mesh, &flat);
  if (!coords || flat != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }
  for (std::size_t i = 0; i < flat; ++i) fnv1a_double(&digest, coords[i]);
  std::array<std::uint64_t, 27> cn{};
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::uint16_t et = souxmar_mesh_cell_type(mesh, c);
    ++histogram[et];
    fnv1a_u64(&digest, static_cast<std::uint64_t>(et));
    const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
    fnv1a_u64(&digest, static_cast<std::uint64_t>(n));
    if (n == 0 || n > cn.size()) continue;
    const souxmar_status_t cs = souxmar_mesh_cell_nodes(mesh, c, cn.data(), cn.size());
    if (cs.code != SOUXMAR_OK) return cs;
    for (std::size_t k = 0; k < n; ++k) fnv1a_u64(&digest, cn[k]);
  }

  const double signed_volume = souxmar_am_slicer::enclosed_volume(bnd);
  const double volume_m3     = std::abs(signed_volume);
  const double height_m      = bnd.hi[2] - bnd.lo[2];

  // A `buildtime` field carries the solver's own cumulative time; prefer it
  // over the closed-form estimate when it is there.
  double    field_time_s = 0.0;
  FieldView probe;
  if (field && make_field_view(field, &probe) && probe.comps == 3 &&
      std::string(probe.name) == "buildtime") {
    const Stats cum = comp_stats(probe, 1, probe.steps - 1);
    if (cum.valid && cum.max > 0.0) field_time_s = cum.max;
  }
  const Totals totals =
      compute_totals(r, volume_m3, std::max(height_m, 0.0), field_time_s);

  std::string o;
  o.reserve(8192);
  o += "# " + r.title + "\n\n";
  o += "Generated by souxmar `writer.am.report` (am-slicer example plugin). "
       "Contains no wall-clock timestamp, no absolute path and no environment "
       "value, so re-running the same pipeline reproduces this file byte for "
       "byte.\n";

  // ---- 1. part and process -------------------------------------------
  o += "\n## 1. Part and process\n\n| item | value |\n|---|---|\n";
  o += "| process | `" + r.process + "` |\n";
  o += "| material | " + r.material + " |\n";
  o += "| build direction | [" + format_fixed(r.build_direction[0], 6) + ", " +
       format_fixed(r.build_direction[1], 6) + ", " +
       format_fixed(r.build_direction[2], 6) + "] |\n";
  o += "| build height along that axis | " + format_fixed(height_m * 1000.0, 4) + " mm |\n";
  if (r.process != "lpbf" && r.process != "fff" && r.process != "ded_waam" &&
      r.process != "sls") {
    o += "\n> `process` is not one of `lpbf`, `fff`, `ded_waam`, `sls`; the "
         "powder-bed (lpbf) build-time model was used.\n";
  }
  if (!r.notes.empty()) o += "\n**Design notes.** " + r.notes + "\n";

  // ---- 2. mesh summary ------------------------------------------------
  o += "\n## 2. Mesh summary\n\n| item | value |\n|---|---|\n";
  o += "| nodes | " + format_fixed(static_cast<double>(num_nodes), 0) + " |\n";
  o += "| cells | " + format_fixed(static_cast<double>(num_cells), 0) + " |\n";
  o += "| boundary facets (triangulated) | " +
       format_fixed(static_cast<double>(bnd.triangles.size()), 0) + " |\n";
  o += "| boundary source | ";
  o += bnd.triangles.empty()
           ? "none (mesh has no facet-bearing cells)"
           : (bnd.from_shell ? "shell cells (Tri3/Quad4)"
                             : "one-owner faces of the volume cells");
  o += " |\n";
  o += "| interior faces | " + format_fixed(static_cast<double>(bnd.interior_faces), 0) + " |\n";
  o += "| cells ignored (no facet contribution) | " +
       format_fixed(static_cast<double>(bnd.unsupported_cells), 0) + " |\n";
  o += "| closed boundary | ";
  o += bnd.manifold == Manifold::Closed
           ? "yes"
           : (bnd.manifold == Manifold::Open ? "**no** (edge audit failed)"
                                             : "not audited");
  o += " |\n";
  o += "| enclosed volume | " + format_fixed(volume_m3 * 1e9, 3) + " mm^3 = " +
       format_fixed(volume_m3 * 1e6, 4) + " cm^3 |\n";
  o += "| mass at `density` | " + format_fixed(totals.mass_kg, 6) + " kg |\n";
  o += "\n| element type | count |\n|---|---|\n";
  for (const auto& entry : histogram) {
    o += "| ";
    o += element_type_name(entry.first);
    o += " | " + format_fixed(static_cast<double>(entry.second), 0) + " |\n";
  }
  o += "\n| bounding box (build frame) | u | v | h (build axis) |\n|---|---|---|---|\n";
  row4(&o, "min (mm)", format_fixed(bnd.lo[0] * 1000.0, 4),
       format_fixed(bnd.lo[1] * 1000.0, 4), format_fixed(bnd.lo[2] * 1000.0, 4));
  row4(&o, "max (mm)", format_fixed(bnd.hi[0] * 1000.0, 4),
       format_fixed(bnd.hi[1] * 1000.0, 4), format_fixed(bnd.hi[2] * 1000.0, 4));
  row4(&o, "size (mm)", format_fixed((bnd.hi[0] - bnd.lo[0]) * 1000.0, 4),
       format_fixed((bnd.hi[1] - bnd.lo[1]) * 1000.0, 4),
       format_fixed((bnd.hi[2] - bnd.lo[2]) * 1000.0, 4));
  if (bnd.triangles.empty()) {
    o += "\n> This mesh contributes no boundary facets — it carries neither "
         "shell (Tri3/Quad4) nor volume (Tet4/Hex8/Prism6/Pyramid5) cells, so "
         "an Edge2 lattice or a point cloud lands here. There is no enclosed "
         "volume, and every mass, time, energy and cost total below is "
         "therefore zero.\n";
  } else if (bnd.manifold != Manifold::Closed) {
    o += "\n> The boundary is not a verified closed shell, so the enclosed "
         "volume — and every mass, cost and time derived from it — is "
         "unreliable.\n";
  }
  if (signed_volume < 0.0) {
    o += "\n> The boundary triangulation is inward-facing (negative signed "
         "volume); the magnitude was used.\n";
  }

  // ---- 3. process parameters -----------------------------------------
  o += "\n## 3. Process parameters\n\n| key | value | unit | source |\n|---|---|---|---|\n";
  kv_row(&o, "layer_height", format_fixed(r.layer_height * 1000.0, 4), "mm",
         !has_key(inputs, "layer_height"));
  kv_row(&o, "process_layer_height", format_fixed(r.process_layer_height * 1e6, 3), "um",
         !has_key(inputs, "process_layer_height"));
  kv_row(&o, "scan_speed", format_fixed(r.scan_speed, 4), "m/s",
         !has_key(inputs, "scan_speed"));
  kv_row(&o, "hatch_spacing", format_fixed(r.hatch_spacing * 1e6, 3), "um",
         !has_key(inputs, "hatch_spacing"));
  kv_row(&o, "recoat_time", format_fixed(r.recoat_time, 3), "s per machine layer",
         !has_key(inputs, "recoat_time"));
  kv_row(&o, "print_speed", format_fixed(r.print_speed, 4), "m/s",
         !has_key(inputs, "print_speed"));
  kv_row(&o, "road_width", format_fixed(r.road_width * 1000.0, 4), "mm",
         !has_key(inputs, "road_width"));
  kv_row(&o, "deposition_rate", format_fixed(r.deposition_rate, 4), "kg/h",
         !has_key(inputs, "deposition_rate"));
  kv_row(&o, "laser_power", format_fixed(r.laser_power, 2), "W",
         !has_key(inputs, "laser_power"));
  kv_row(&o, "machine_power_overhead", format_fixed(r.machine_power_overhead, 2), "W",
         !has_key(inputs, "machine_power_overhead"));
  kv_row(&o, "material_cost_per_kg", format_fixed(r.material_cost_per_kg, 2), "per kg",
         !has_key(inputs, "material_cost_per_kg"));
  kv_row(&o, "machine_rate_per_hour", format_fixed(r.machine_rate_per_hour, 2), "per hour",
         !has_key(inputs, "machine_rate_per_hour"));
  kv_row(&o, "density", format_fixed(r.density, 2), "kg/m^3", !has_key(inputs, "density"));

  // ---- 4. build totals ------------------------------------------------
  o += "\n## 4. Build totals and cost\n\n| item | value |\n|---|---|\n";
  o += "| machine layers | " + format_fixed(static_cast<double>(totals.machine_layers), 0) +
       " |\n";
  o += "| deposition (active) time | " + format_fixed(totals.active_time_s / 3600.0, 4) +
       " h |\n";
  o += "| total build time | " + format_fixed(totals.total_time_s / 3600.0, 4) + " h |\n";
  o += "| build-time source | " + std::string(totals.time_source) + " |\n";
  o += "| energy | " + format_fixed(totals.energy_j / 3.6e6, 4) + " kWh |\n";
  o += "| mass | " + format_fixed(totals.mass_kg, 6) + " kg |\n";
  o += "| feedstock cost | " + format_fixed(totals.material_cost, 2) + " |\n";
  o += "| machine cost | " + format_fixed(totals.machine_cost, 2) + " |\n";
  o += "| total cost | " + format_fixed(totals.material_cost + totals.machine_cost, 2) +
       " |\n";
  o += "\nCost is feedstock plus machine time only — no labour, gas, support "
       "removal, HIP, machining, inspection or scrap. Energy is "
       "`laser_power * active_time + machine_power_overhead * total_time`, "
       "where `laser_power` stands for the deposition source (laser, arc or "
       "nozzle heater); for FFF that over-predicts, because a nozzle heater "
       "duty-cycles.\n";

  // ---- 5. simulation evidence ----------------------------------------
  report_field_section(&o, field);

  // ---- 6. traceability ------------------------------------------------
  o += "\n## 6. Traceability\n\n| item | value |\n|---|---|\n";
  o += "| mesh digest (FNV-1a 64) | `" + hex64(digest) + "` |\n";
  o += "| digest input | every node coordinate, then per cell its element "
       "type, node count and connectivity |\n";
  o += "| nodes hashed | " + format_fixed(static_cast<double>(num_nodes), 0) + " |\n";
  o += "| cells hashed | " + format_fixed(static_cast<double>(num_cells), 0) + " |\n";
  o += "\nThe digest is a **non-cryptographic content digest** (FNV-1a, "
       "offset basis 14695981039346656037, prime 1099511628211), fed the IEEE-754 "
       "bit pattern of each coordinate in explicit little-endian byte order so "
       "it does not depend on host endianness. It answers \"is this the same "
       "mesh?\" and nothing else: it is not a signature, it carries no "
       "authenticity or tamper evidence, and it must not be used as one.\n";

  return commit_file(path, o);
}

void report_field_section(std::string* o, const souxmar_field_t* field) {
  *o += "\n## 5. Simulation evidence\n";
  if (!field) {
    *o += "\nNo field was supplied to this writer, so there is no simulation "
          "evidence to report — the sections above describe the mesh and the "
          "process inputs only. Wire a solver or postproc stage into the "
          "writer's `field:` input to populate this section.\n";
    return;
  }
  FieldView f;
  if (!make_field_view(field, &f)) {
    *o += "\nA field was supplied but its buffer is inconsistent with its "
          "declared count x components x steps, so nothing was read from it.\n";
    return;
  }

  const std::string name(f.name);
  *o += "\n| item | value |\n|---|---|\n";
  *o += "| field | `" + name + "` |\n";
  *o += "| location | ";
  *o += f.location == SOUXMAR_FL_NODAL
            ? "nodal"
            : (f.location == SOUXMAR_FL_CELL
                   ? "cell"
                   : (f.location == SOUXMAR_FL_FACE ? "face" : "gauss point"));
  *o += " |\n";
  *o += "| entries | " + format_fixed(static_cast<double>(f.count), 0) + " |\n";
  *o += "| components | " + format_fixed(static_cast<double>(f.comps), 0) + " |\n";
  *o += "| time steps | " + format_fixed(static_cast<double>(f.steps), 0) + " |\n";

  const std::size_t last = f.steps - 1;

  if (name == "temperature") {
    *o += "\n### Thermal history (`solver.am.thermal.lpbf`)\n";
    *o += "\nStep `j` is the state just after layer `j` was scanned. "
          "Temperatures are degC.\n";
    step_table(o, f, "nodal temperature in degC", 2);
    const Stats all = comp_stats(f, 0, kAllSteps);
    if (all.valid) {
      *o += "\nPeak nodal temperature over the whole build: " +
            format_fixed(all.max, 2) + " degC.\n";
    }
  } else if (name == "melt_pool") {
    *o += "\n### Melt pool and porosity risk (`postproc.am.melt_pool`)\n";
    const char* names[3] = {"melt_pool_depth", "normalised_enthalpy", "porosity_risk"};
    const char* units[3] = {"m", "-", "0..1"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 6);
      const Stats depth = comp_stats(f, 0, kAllSteps);
      if (depth.valid) {
        *o += "\nMean melt-pool depth " + format_fixed(depth.mean * 1e6, 2) +
              " um, maximum " + format_fixed(depth.max * 1e6, 2) + " um.\n";
      }
      const std::size_t risky = count_ge(f, 2, last, 0.5);
      *o += "\nCells with `porosity_risk >= 0.5`: " +
            format_fixed(static_cast<double>(risky), 0) + " of " +
            format_fixed(static_cast<double>(f.count), 0) + " (" +
            format_fixed(100.0 * static_cast<double>(risky) /
                             static_cast<double>(f.count),
                         2) +
            "%). The threshold is a reporting convention, not a physical "
            "boundary.\n";
    } else {
      stats_table(o, f, nullptr, nullptr, 6);
    }
  } else if (name == "distortion_displacement") {
    *o += "\n### Residual distortion (`solver.am.distortion.inherent_strain`)\n";
    const char* names[3] = {"ux", "uy", "uz"};
    const char* units[3] = {"m", "m", "m"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 9);
      double      max_mag  = 0.0;
      std::size_t max_node = 0;
      for (std::size_t i = 0; i < f.count; ++i) {
        const double ux = f.at(i, 0, last);
        const double uy = f.at(i, 1, last);
        const double uz = f.at(i, 2, last);
        if (!std::isfinite(ux) || !std::isfinite(uy) || !std::isfinite(uz)) continue;
        const double m = std::sqrt(ux * ux + uy * uy + uz * uz);
        if (m > max_mag) {
          max_mag  = m;
          max_node = i;
        }
      }
      *o += "\nAt the final step, the largest displacement magnitude is " +
            format_fixed(max_mag * 1000.0, 6) + " mm at node " +
            format_fixed(static_cast<double>(max_node), 0) + ".\n";
    } else {
      stats_table(o, f, nullptr, nullptr, 9);
    }
  } else if (name == "residual_stress") {
    *o += "\n### Residual stress (`postproc.am.residual_stress`)\n";
    const char* names[3] = {"sigma_vm", "sigma_vm_over_yield", "layer_index"};
    const char* units[3] = {"Pa", "-", "-"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 4);
      const Stats vm = comp_stats(f, 0, kAllSteps);
      if (vm.valid) {
        *o += "\nPeak von-Mises-equivalent residual stress " +
              format_fixed(vm.max / 1e6, 3) + " MPa (mean " +
              format_fixed(vm.mean / 1e6, 3) + " MPa).\n";
      }
      const std::size_t over = count_ge(f, 1, last, 1.0);
      *o += "\nCells at or above the yield strength: " +
            format_fixed(static_cast<double>(over), 0) + " of " +
            format_fixed(static_cast<double>(f.count), 0) + ".\n";
    } else {
      stats_table(o, f, nullptr, nullptr, 4);
    }
  } else if (name == "interface_temperature") {
    *o += "\n### FFF interlayer thermal history (`solver.am.polymer.fff`)\n";
    step_table(o, f, "interface temperature in degC", 2);
  } else if (name == "bond_quality") {
    *o += "\n### Interlayer bond quality (`postproc.am.bond_strength`)\n";
    const char* names[3] = {"degree_of_healing", "z_strength_fraction", "seconds_above_tg"};
    const char* units[3] = {"0..1", "0..1", "s"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 6);
      const Stats heal = comp_stats(f, 0, kAllSteps);
      if (heal.valid) {
        *o += "\nWeakest interlayer healing " + format_fixed(heal.min, 4) +
              " (mean " + format_fixed(heal.mean, 4) + ").\n";
      }
    } else {
      stats_table(o, f, nullptr, nullptr, 6);
    }
  } else if (name == "overhang") {
    *o += "\n### Overhang and support need (`solver.am.overhang`)\n";
    const char* names[3] = {"min_downskin_tilt", "support_needed", "downskin_area"};
    const char* units[3] = {"deg", "0/1", "m^2"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 6);
      const std::size_t need = count_ge(f, 1, last, 0.5);
      double            area = 0.0;
      for (std::size_t i = 0; i < f.count; ++i) {
        const double a = f.at(i, 2, last);
        if (std::isfinite(a)) area += a;
      }
      *o += "\nCells needing support: " + format_fixed(static_cast<double>(need), 0) +
            " of " + format_fixed(static_cast<double>(f.count), 0) +
            ". Total downskin area " + format_fixed(area * 1e6, 3) + " mm^2.\n";
    } else {
      stats_table(o, f, nullptr, nullptr, 6);
    }
  } else if (name == "printability") {
    *o += "\n### DfAM printability (`solver.am.printability`)\n";
    const char* names[3] = {"printability_score", "limiting_factor_code",
                            "wall_thickness_proxy"};
    const char* units[3] = {"0..1", "code", "m"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 6);
      const char* factor[6] = {"none", "overhang", "thin wall", "build-volume fit",
                               "aspect ratio / height", "corrosion allowance"};
      std::array<std::size_t, 6> tally{};
      std::size_t                other = 0;
      for (std::size_t i = 0; i < f.count; ++i) {
        const double code = f.at(i, 1, last);
        const int    idx  = std::isfinite(code) ? static_cast<int>(code) : -1;
        if (idx >= 0 && idx < 6) {
          ++tally[static_cast<std::size_t>(idx)];
        } else {
          ++other;
        }
      }
      *o += "\n| limiting factor | cells |\n|---|---|\n";
      for (std::size_t i = 0; i < 6; ++i) {
        *o += "| ";
        *o += factor[i];
        *o += " | " + format_fixed(static_cast<double>(tally[i]), 0) + " |\n";
      }
      if (other > 0) {
        *o += "| unrecognised code | " + format_fixed(static_cast<double>(other), 0) +
              " |\n";
      }
    } else {
      stats_table(o, f, nullptr, nullptr, 6);
    }
  } else if (name == "buildtime") {
    *o += "\n### Per-layer build time (`solver.am.buildtime`)\n";
    const char* names[3] = {"layer_time", "cumulative_time", "layer_area"};
    const char* units[3] = {"s", "s", "m^2"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 4);
      const Stats cum = comp_stats(f, 1, last);
      if (cum.valid) {
        *o += "\nBuild time from the field's own `cumulative_time_s`: " +
              format_fixed(cum.max / 3600.0, 4) + " h. Section 4 re-derives it "
              "from the closed-form process model; the two differ whenever the "
              "solver and this writer were given different process inputs.\n";
      }
    } else {
      stats_table(o, f, nullptr, nullptr, 4);
    }
  } else if (name == "corrosion") {
    *o += "\n### Seawater corrosion (`solver.marine.corrosion`)\n";
    const char* names[3] = {"thickness_loss", "pitting_risk", "galvanic_risk"};
    const char* units[3] = {"mm", "0..1", "0..1"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 6);
    } else {
      stats_table(o, f, nullptr, nullptr, 6);
    }
  } else if (name == "collapse_margin") {
    *o += "\n### Pressure-hull collapse margin (`solver.marine.hull_collapse`)\n";
    const char* names[3] = {"collapse_pressure", "margin", "governing_mode_code"};
    const char* units[3] = {"Pa", "-", "code"};
    if (f.comps == 3) {
      stats_table(o, f, names, units, 4);
      const Stats p = comp_stats(f, 0, last);
      const Stats m = comp_stats(f, 1, last);
      if (p.valid && m.valid) {
        *o += "\nCollapse pressure " + format_fixed(p.min / 1e6, 4) +
              " MPa, margin against the factored design pressure " +
              format_fixed(m.min, 4) + ". Analytical, not mesh-resolved.\n";
      }
    } else {
      stats_table(o, f, nullptr, nullptr, 4);
    }
  } else if (name == "hydrostatic_pressure") {
    *o += "\n### Hydrostatic load cases (`solver.marine.hydrostatic`)\n";
    step_table(o, f, "nodal pressure magnitude in Pa", 1);
  } else {
    *o += "\n### Generic field summary\n";
    *o += "\n`" + name +
          "` is not one of the fields this writer renders a tailored section "
          "for, so here are its per-component statistics over every time "
          "step.\n";
    stats_table(o, f, nullptr, nullptr, 6);
  }
}

// ---- vtables -----------------------------------------------------------
// Every entry point is wrapped: a std::bad_alloc from building a multi-megabyte
// toolpath in memory must become a status code, never an exception crossing the
// C ABI.

souxmar_status_t gcode_write(const souxmar_mesh_t*  mesh,
                            const souxmar_field_t* /*field*/,
                            const souxmar_value_t* inputs,
                            void*                  /*user_data*/) {
  try {
    return gcode_write_impl(mesh, inputs);
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "writer.am.gcode: out of memory building the toolpath");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "writer.am.gcode: unexpected failure");
  }
}

souxmar_status_t cli_write(const souxmar_mesh_t*  mesh,
                          const souxmar_field_t* /*field*/,
                          const souxmar_value_t* inputs,
                          void*                  /*user_data*/) {
  try {
    return cli_write_impl(mesh, inputs);
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "writer.am.cli: out of memory building the layer set");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "writer.am.cli: unexpected failure");
  }
}

souxmar_status_t report_write(const souxmar_mesh_t*  mesh,
                             const souxmar_field_t* field,
                             const souxmar_value_t* inputs,
                             void*                  /*user_data*/) {
  try {
    return report_write_impl(mesh, field, inputs);
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "writer.am.report: out of memory building the report");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "writer.am.report: unexpected failure");
  }
}

constexpr souxmar_writer_vtable_t kGcodeVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &gcode_write,
    nullptr,
};

constexpr souxmar_writer_vtable_t kCliVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &cli_write,
    nullptr,
};

constexpr souxmar_writer_vtable_t kReportVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &report_write,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t a =
      souxmar_registry_add_writer(registry, "writer.am.gcode", &kGcodeVtable,
                                  /*user_data=*/nullptr);
  if (a.code != SOUXMAR_OK) return 1;
  const souxmar_status_t b =
      souxmar_registry_add_writer(registry, "writer.am.cli", &kCliVtable,
                                  /*user_data=*/nullptr);
  if (b.code != SOUXMAR_OK) return 1;
  const souxmar_status_t c =
      souxmar_registry_add_writer(registry, "writer.am.report", &kReportVtable,
                                  /*user_data=*/nullptr);
  return c.code == SOUXMAR_OK ? 0 : 1;
}
