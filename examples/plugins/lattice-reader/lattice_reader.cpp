// SPDX-License-Identifier: Apache-2.0
//
// lattice-reader — parametric strut-lattice generator.
//
// Registers `reader.lattice`. It is a reader rather than a mesher for a
// structural reason, not an aesthetic one: the v1 mesher ABI hands a
// mesher no value bag (`souxmar_mesher_mesh_fn(geometry, options,
// out_mesh, user_data)` — contract 0.4), so a fully parametric
// generator has nowhere to read `unit_cell` / `cell_size` /
// `relative_density` from. `souxmar_reader_read_fn` gets
// `(path, inputs, options)`, so parametric generation lives here.
// The stage emits a **mesh** (Edge2 beam elements) and leaves
// out_geometry NULL.
//
// What it computes:
//   A periodic strut lattice, trimmed to a box. The box is tiled with
//   nx x ny x nz cubic unit cells of edge `cell_size`, anchored at the
//   bbox minimum. Each unit cell contributes a fixed strut set, listed
//   in quarter-cell integer coordinates so every site a strut can touch
//   (corners at 0/4, face + body centres at 2, diamond sites at 1/3)
//   lands on an exact integer grid — nodes are therefore deduplicated
//   exactly, with no coordinate tolerance and no floating-point
//   comparison anywhere.
//
//   Unit cells (all five implemented for real, no aliases):
//     cubic   12 struts — the 12 cube edges. Axial only. 4 struts per
//             axis; interior edges are shared by 4 cells and collapse
//             in dedup, giving the classic simple-cubic lattice.
//     bcc      8 struts — the four body diagonals, each split at the
//             body centre into two half-diagonals so the centre is a
//             real 8-way junction (needed for a beam model). Strut
//             length a*sqrt(3)/2. No axial struts: this is BCC, not
//             BCCZ; adding the Z struts would change the stiffness
//             anisotropy and there is no contract key to ask for it.
//     fcc     24 struts — face diagonals: each of the 6 face centres
//             joined to the 4 corners of its face. Strut length
//             a/sqrt(2). Face centres are shared with the neighbouring
//             cell and collapse in dedup.
//     octet   36 struts — the octet truss (Fuller; Deshpande, Fleck &
//             Ashby 2001): the fcc set (24 tetrahedra edges) plus the
//             12 edges of the regular octahedron formed by the 6 face
//             centres. All struts have length a/sqrt(2). No axial
//             members — that is what makes the octet truss
//             stretch-dominated.
//     diamond 16 struts — the diamond-cubic (tetrahedral) network: the
//             4 offset sites (1/4,1/4,1/4), (3/4,3/4,1/4),
//             (3/4,1/4,3/4), (1/4,3/4,3/4) each bonded to their 4
//             nearest FCC sites, bond vectors (+-1/4,+-1/4,+-1/4) with
//             an even number of minus signs. Strut length
//             a*sqrt(3)/4; every interior node has coordination 4.
//
//   Cell tag = strut family (contract 3.2): 0 X-axial, 1 Y-axial,
//   2 Z-axial, 3 body-diagonal, 4 face-diagonal.
//
//   Struts are de-duplicated by a sorted (lo, hi) node-index key held
//   in a std::set — an ordered container, never an unordered one, so
//   the output is byte-identical across platforms. Node ids are handed
//   out in first-touch order while walking cells (k outermost, then j,
//   then i) and, within a cell, the static strut table in order; both
//   loops run in index order, so ids are deterministic too.
//
//   Relative density <-> strut diameter. With `strut_diameter` left at
//   0 the diameter is derived from `relative_density` by equating the
//   swept cylinder volume of every strut to the requested solid
//   fraction of the tiled volume:
//
//       rho* = (pi*d^2/4) * L_total / V_lattice
//       =>  d = sqrt(4 * rho* * V_lattice / (pi * L_total))
//
//   where L_total is the summed length of the *generated* struts (so
//   boundary trimming is accounted for exactly) and
//   V_lattice = nx*ny*nz*cell_size^3. The relation ignores the volume
//   double-counted where struts meet at a node, so it OVER-predicts the
//   density of a given diameter — the error is a couple of percent
//   below rho* ~ 0.3 and grows fast above it, which means the derived
//   diameter is correspondingly under-sized in that regime. Above
//   rho* ~ 0.3, size the struts explicitly and treat the number as
//   nominal. Worked example: 50 mm cube, bcc, 5 mm cells,
//   relative_density 0.15 -> 1000 unit cells, 2331 nodes, 8000 struts,
//   L_total = 34.641 m, V_lattice = 1.25e-4 m^3, d = 0.830 mm (and
//   feeding that d back through the relation returns 0.1500).
//
// What this is NOT:
//   * Not a beam solver input in the full sense: the ABI's reader
//     surface can transport a mesh only — there is no channel for a
//     scalar section property, so the derived strut diameter cannot be
//     attached to the mesh. It is used here to validate that the
//     requested density is geometrically achievable, and it is
//     documented (above and in example.lattice) so a downstream beam
//     solver can be handed the same number explicitly.
//   * Not a conformal lattice: struts are trimmed at the box, not at a
//     part surface, so boundary struts dangle (a real generator either
//     trims to a B-rep or culls partial cells). Dangling ends are left
//     in deliberately — culling them would silently change the
//     relative density.
//   * No fillets / node blending, no strut tapering, no graded or
//     conformal cell size, no self-intersection check.
//   * No printability check. A 60 um strut is generated happily; LPBF
//     cannot print below roughly 150-200 um. Run
//     `solver.am.printability` for that.
//
// Spec file (`path`, required). UTF-8 text, `#` starts a comment
// (to end of line), one `key = value` per line, blank lines ignored.
// Values are numbers, bare strings, or `[a, b, c]` lists. Unknown keys
// are ignored (forward compatibility); a malformed line — no `=`, an
// unterminated list, a non-numeric value for a numeric key, a list of
// the wrong length — is SOUXMAR_E_IO naming the offending line number.
// Every recognised key may also be given in the stage's `input:` map,
// where it OVERRIDES the file.
//
// Inputs (spec file keys and `souxmar_value_t` map keys are identical):
//   bbox             : list of 6, m, default [0,0,0, 0.05,0.05,0.05]
//                      (xmin,ymin,zmin, xmax,ymax,zmax; swapped if
//                      given inverted)
//   unit_cell        : cubic | bcc | fcc | octet | diamond, default bcc
//                      (anything else is SOUXMAR_E_INVALID_ARGUMENT)
//   cell_size        : m, default 0.005, clamped [1e-4, 1.0]
//   strut_diameter   : m, default 0.0 => derive from relative_density
//   relative_density : -, default 0.15, clamped [0.01, 0.6]
//   nx, ny, nz       : int, default (and for any non-positive value)
//                      round(span / cell_size) per axis, each clamped
//                      to [1, 60]
//   max_cells        : int, default 400000 — cap on emitted Edge2 cells
//
// `souxmar_reader_options_t` is ignored: the dispatcher passes readers
// `options == NULL` (contract 0.5), and node dedup here is exact by
// construction, so `merge_coincident_nodes` / `coincidence_tolerance`
// have nothing to change.
//
// Capability: `reader.lattice`. Declared `reentrant` — pure functional
// over its inputs, no shared state.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

// pi to double precision — <numbers> would do, but the plugin sticks to
// the same headers the other example plugins use.
constexpr double kPi = 3.14159265358979323846;

// Strut-family cell tags. FROZEN by contract 3.2 — downstream tooling
// reads these numbers off the mesh.
constexpr std::int32_t kFamilyXAxial       = 0;
constexpr std::int32_t kFamilyYAxial       = 1;
constexpr std::int32_t kFamilyZAxial       = 2;
constexpr std::int32_t kFamilyBodyDiagonal = 3;
constexpr std::int32_t kFamilyFaceDiagonal = 4;

// Input ranges (contract 3.2).
constexpr double kMinCellSize        = 1e-4;   // m — 0.1 mm
constexpr double kMaxCellSize        = 1.0;    // m
constexpr double kMinRelDensity      = 0.01;   // -
constexpr double kMaxRelDensity      = 0.6;    // - beyond this the
                                               //   overlap-free density
                                               //   model is meaningless
constexpr int    kMinCellsPerAxis    = 1;
constexpr int    kMaxCellsPerAxis    = 60;     // 60^3 = 216000 unit cells
constexpr std::int64_t kMaxMaxCells  = 5000000;  // ceiling on `max_cells`
                                                 // itself: 5e6 Edge2 cells
                                                 // is ~ 0.5 GB of host mesh

// Strut-diameter sanity gates.
//   Lower: 1e-6 m = 1 um. Below that the "lattice" is a numerical
//   artefact rather than a structure. (Note this is a numerical gate,
//   not a printability gate — LPBF cannot do much below 150-200 um.)
//   Upper: 1.0 * cell_size — a strut as thick as the cell it tiles is a
//   solid block, not a lattice. The gate is deliberately loose so that
//   nothing inside the documented relative_density range [0.01, 0.6] is
//   ever refused: the worst case is simple-cubic at 0.6, which derives
//   d = 0.505 * cell_size. Between roughly 0.5 * cell_size and the gate
//   the struts interpenetrate over a good fraction of their length and
//   the overlap-free density relation over-predicts badly — the
//   geometry is still a valid wireframe, the density number is not.
constexpr double kMinStrutDiameter         = 1e-6;
constexpr double kMaxStrutDiameterFraction = 1.0;

// Packing stride for the (I,J,K) quarter-cell node key. Quarter-cell
// indices run 0 .. 4*kMaxCellsPerAxis == 240, so 1024 leaves a wide
// margin and keeps the packed key well inside int64.
constexpr std::int64_t kPackStride = 1024;

enum class UnitCell : std::uint8_t { Cubic, Bcc, Fcc, Octet, Diamond };

// A strut of the unit cell, endpoints in quarter-cell integer
// coordinates (each component in [0, 4]; 4 == the next cell's corner).
struct Strut {
  std::array<std::uint8_t, 3> a;
  std::array<std::uint8_t, 3> b;
  std::int32_t                family;
};

// --- cubic: the 12 cube edges -------------------------------------------
constexpr Strut kCubicStruts[12] = {
    {{0, 0, 0}, {4, 0, 0}, kFamilyXAxial},
    {{0, 4, 0}, {4, 4, 0}, kFamilyXAxial},
    {{0, 0, 4}, {4, 0, 4}, kFamilyXAxial},
    {{0, 4, 4}, {4, 4, 4}, kFamilyXAxial},
    {{0, 0, 0}, {0, 4, 0}, kFamilyYAxial},
    {{4, 0, 0}, {4, 4, 0}, kFamilyYAxial},
    {{0, 0, 4}, {0, 4, 4}, kFamilyYAxial},
    {{4, 0, 4}, {4, 4, 4}, kFamilyYAxial},
    {{0, 0, 0}, {0, 0, 4}, kFamilyZAxial},
    {{4, 0, 0}, {4, 0, 4}, kFamilyZAxial},
    {{0, 4, 0}, {0, 4, 4}, kFamilyZAxial},
    {{4, 4, 0}, {4, 4, 4}, kFamilyZAxial},
};

// --- bcc: 8 half body diagonals, corner -> body centre (2,2,2) ---------
constexpr Strut kBccStruts[8] = {
    {{0, 0, 0}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{4, 0, 0}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{4, 4, 0}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{0, 4, 0}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{0, 0, 4}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{4, 0, 4}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{4, 4, 4}, {2, 2, 2}, kFamilyBodyDiagonal},
    {{0, 4, 4}, {2, 2, 2}, kFamilyBodyDiagonal},
};

// --- fcc: 24 corner -> face-centre struts (face diagonals) -------------
// Face centres: -z (2,2,0), +z (2,2,4), -y (2,0,2), +y (2,4,2),
//               -x (0,2,2), +x (4,2,2).
constexpr Strut kFccStruts[24] = {
    {{0, 0, 0}, {2, 2, 0}, kFamilyFaceDiagonal},  // -z face
    {{4, 0, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{4, 4, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{0, 4, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {2, 2, 4}, kFamilyFaceDiagonal},  // +z face
    {{4, 0, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{0, 0, 0}, {2, 0, 2}, kFamilyFaceDiagonal},  // -y face
    {{4, 0, 0}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{4, 0, 4}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{0, 4, 0}, {2, 4, 2}, kFamilyFaceDiagonal},  // +y face
    {{4, 4, 0}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{0, 0, 0}, {0, 2, 2}, kFamilyFaceDiagonal},  // -x face
    {{0, 4, 0}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{4, 0, 0}, {4, 2, 2}, kFamilyFaceDiagonal},  // +x face
    {{4, 4, 0}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{4, 0, 4}, {4, 2, 2}, kFamilyFaceDiagonal},
};

// --- octet: the fcc set + the 12 octahedron edges ----------------------
// The octahedron is formed by the 6 face centres; its edges join every
// pair of face centres that are NOT opposite (15 pairs - 3 opposite
// pairs = 12 edges), each of length a/sqrt(2) — the same length as the
// tetrahedra edges, which is what makes the octet truss regular.
constexpr Strut kOctetStruts[36] = {
    // 24 tetrahedra edges — identical to kFccStruts.
    {{0, 0, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{4, 0, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{4, 4, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{0, 4, 0}, {2, 2, 0}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{4, 0, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {2, 2, 4}, kFamilyFaceDiagonal},
    {{0, 0, 0}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{4, 0, 0}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{4, 0, 4}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{0, 4, 0}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{4, 4, 0}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{0, 0, 0}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{0, 4, 0}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{0, 4, 4}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{0, 0, 4}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{4, 0, 0}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{4, 4, 0}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{4, 4, 4}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{4, 0, 4}, {4, 2, 2}, kFamilyFaceDiagonal},
    // 12 octahedron edges: (-z,+z) / (-y,+y) / (-x,+x) excluded.
    {{2, 2, 0}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{2, 2, 0}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{2, 2, 0}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{2, 2, 0}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{2, 2, 4}, {2, 0, 2}, kFamilyFaceDiagonal},
    {{2, 2, 4}, {2, 4, 2}, kFamilyFaceDiagonal},
    {{2, 2, 4}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{2, 2, 4}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{2, 0, 2}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{2, 0, 2}, {4, 2, 2}, kFamilyFaceDiagonal},
    {{2, 4, 2}, {0, 2, 2}, kFamilyFaceDiagonal},
    {{2, 4, 2}, {4, 2, 2}, kFamilyFaceDiagonal},
};

// --- diamond: 16 tetrahedral bonds ------------------------------------
// The four offset sites B1..B4 sit at (1,1,1), (3,3,1), (3,1,3),
// (1,3,3) in quarter-cell units; each bonds along the four
// (+-1,+-1,+-1)/4 directions with an even number of minus signs, which
// lands exactly on the FCC sites (cube corners and face centres) of
// this cell or of the +x/+y/+z neighbour. Every bond therefore stays
// inside the closed cell cube and every interior node ends up with
// coordination 4.
constexpr Strut kDiamondStruts[16] = {
    {{1, 1, 1}, {0, 0, 0}, kFamilyBodyDiagonal},  // B1
    {{1, 1, 1}, {2, 2, 0}, kFamilyBodyDiagonal},
    {{1, 1, 1}, {2, 0, 2}, kFamilyBodyDiagonal},
    {{1, 1, 1}, {0, 2, 2}, kFamilyBodyDiagonal},
    {{3, 3, 1}, {2, 2, 0}, kFamilyBodyDiagonal},  // B2
    {{3, 3, 1}, {4, 4, 0}, kFamilyBodyDiagonal},
    {{3, 3, 1}, {4, 2, 2}, kFamilyBodyDiagonal},
    {{3, 3, 1}, {2, 4, 2}, kFamilyBodyDiagonal},
    {{3, 1, 3}, {2, 0, 2}, kFamilyBodyDiagonal},  // B3
    {{3, 1, 3}, {4, 2, 2}, kFamilyBodyDiagonal},
    {{3, 1, 3}, {4, 0, 4}, kFamilyBodyDiagonal},
    {{3, 1, 3}, {2, 2, 4}, kFamilyBodyDiagonal},
    {{1, 3, 3}, {0, 2, 2}, kFamilyBodyDiagonal},  // B4
    {{1, 3, 3}, {2, 4, 2}, kFamilyBodyDiagonal},
    {{1, 3, 3}, {2, 2, 4}, kFamilyBodyDiagonal},
    {{1, 3, 3}, {0, 4, 4}, kFamilyBodyDiagonal},
};

std::span<const Strut> struts_for(UnitCell cell) {
  switch (cell) {
    case UnitCell::Cubic:   return {kCubicStruts, 12};
    case UnitCell::Bcc:     return {kBccStruts, 8};
    case UnitCell::Fcc:     return {kFccStruts, 24};
    case UnitCell::Octet:   return {kOctetStruts, 36};
    case UnitCell::Diamond: return {kDiamondStruts, 16};
  }
  return {};
}

bool unit_cell_from_string(const std::string& name, UnitCell* out) {
  if (name == "cubic")   { *out = UnitCell::Cubic;   return true; }
  if (name == "bcc")     { *out = UnitCell::Bcc;     return true; }
  if (name == "fcc")     { *out = UnitCell::Fcc;     return true; }
  if (name == "octet")   { *out = UnitCell::Octet;   return true; }
  if (name == "diamond") { *out = UnitCell::Diamond; return true; }
  return false;
}

// ---- value-bag helpers (contract 2.5 — read_number / read_int copied
// ---- verbatim from examples/plugins/modal-stub/modal_stub.cpp) -------

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

std::string read_string(const souxmar_value_t* inputs, const char* key,
                        const std::string& dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_STRING) return dv;
  const char* s = souxmar_value_as_string(v);
  return s ? std::string(s) : dv;
}

// Reads a fixed-length numeric list. Leaves `out` untouched and returns
// false unless the key is present, is a list of exactly `n` entries, and
// every entry is a number.
bool read_number_list(const souxmar_value_t* inputs, const char* key,
                      double* out, std::size_t n) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return false;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return false;
  if (souxmar_value_list_size(v) != n) return false;
  for (std::size_t i = 0; i < n; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = souxmar_value_as_number(souxmar_value_list_at(v, i));
  }
  return true;
}

// ---- spec ------------------------------------------------------------

struct LatticeSpec {
  double      bbox[6]          = {0.0, 0.0, 0.0, 0.05, 0.05, 0.05};  // m
  std::string unit_cell        = "bcc";
  double      cell_size        = 0.005;   // m
  double      strut_diameter   = 0.0;     // m; <= 0 => derive
  double      relative_density = 0.15;    // -
  int         nx               = 0;       // 0 => derive from bbox
  int         ny               = 0;
  int         nz               = 0;
  std::int64_t max_cells       = 400000;  // emitted Edge2 cells
};

// Error-message scratch. The host only borrows status strings, so a
// thread-local with static storage duration is the established pattern
// here (see examples/plugins/stl-reader/stl_reader.cpp).
std::string& error_scratch() {
  static thread_local std::string msg;
  return msg;
}

souxmar_status_t io_error_at_line(std::size_t line_no, const char* what) {
  std::string& msg = error_scratch();
  msg = "reader.lattice: spec file line " + std::to_string(line_no) + ": " + what;
  return souxmar_status_error(SOUXMAR_E_IO, msg.c_str());
}

std::string trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  const auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
  };
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// strtod with full-consumption + finiteness checks. <charconv>'s
// from_chars for double is still patchy across the supported toolchains,
// so strtod on a NUL-terminated std::string is the portable path.
bool parse_double(const std::string& text, double* out) {
  const std::string t = trim(text);
  if (t.empty()) return false;
  char*        end = nullptr;
  const double v   = std::strtod(t.c_str(), &end);
  if (end == t.c_str()) return false;
  while (*end == ' ' || *end == '\t') ++end;
  if (*end != '\0') return false;
  if (!std::isfinite(v)) return false;
  *out = v;
  return true;
}

// `[a, b, c]` -> doubles. Returns false on a missing bracket, a
// non-numeric entry, or a count mismatch.
bool parse_double_list(const std::string& text, double* out, std::size_t n) {
  const std::string t = trim(text);
  if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
  const std::string body = t.substr(1, t.size() - 2);
  std::size_t       count = 0;
  std::size_t       pos   = 0;
  while (true) {
    const std::size_t comma = body.find(',', pos);
    const std::string item =
        trim(comma == std::string::npos ? body.substr(pos) : body.substr(pos, comma - pos));
    if (item.empty() && count == 0 && comma == std::string::npos) return false;
    if (count >= n) return false;
    if (!parse_double(item, &out[count])) return false;
    ++count;
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return count == n;
}

souxmar_status_t parse_spec_file(const char* path, LatticeSpec* spec) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::string& msg = error_scratch();
    msg = std::string("reader.lattice: cannot open lattice spec '") + path + "'";
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, msg.c_str());
  }

  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    const std::string stripped = trim(line);
    if (stripped.empty()) continue;

    const std::size_t eq = stripped.find('=');
    if (eq == std::string::npos) {
      return io_error_at_line(line_no, "expected 'key = value'");
    }
    const std::string key   = trim(stripped.substr(0, eq));
    const std::string value = trim(stripped.substr(eq + 1));
    if (key.empty()) {
      return io_error_at_line(line_no, "empty key");
    }
    if (value.empty()) {
      return io_error_at_line(line_no, "empty value");
    }

    if (key == "bbox") {
      double b[6] = {0, 0, 0, 0, 0, 0};
      if (!parse_double_list(value, b, 6)) {
        return io_error_at_line(line_no,
            "bbox expects a list of 6 numbers: [xmin, ymin, zmin, xmax, ymax, zmax]");
      }
      for (std::size_t i = 0; i < 6; ++i) spec->bbox[i] = b[i];
      continue;
    }
    if (key == "unit_cell") {
      spec->unit_cell = value;
      continue;
    }
    if (key == "cell_size" || key == "strut_diameter" ||
        key == "relative_density") {
      double v = 0.0;
      if (!parse_double(value, &v)) {
        return io_error_at_line(line_no, "expected a number");
      }
      if (key == "cell_size")             spec->cell_size = v;
      else if (key == "strut_diameter")   spec->strut_diameter = v;
      else                                spec->relative_density = v;
      continue;
    }
    if (key == "nx" || key == "ny" || key == "nz" || key == "max_cells") {
      double v = 0.0;
      if (!parse_double(value, &v)) {
        return io_error_at_line(line_no, "expected an integer");
      }
      // Saturate rather than wrap: an out-of-range count is clamped to
      // its documented range further down, and a value beyond int64 is
      // a units mistake, not a lattice.
      const double clamped = std::clamp(v, -1e15, 1e15);
      if (key == "nx")             spec->nx = static_cast<int>(std::clamp(clamped, -1e9, 1e9));
      else if (key == "ny")        spec->ny = static_cast<int>(std::clamp(clamped, -1e9, 1e9));
      else if (key == "nz")        spec->nz = static_cast<int>(std::clamp(clamped, -1e9, 1e9));
      else                         spec->max_cells = static_cast<std::int64_t>(clamped);
      continue;
    }
    // Unknown key — ignored on purpose (forward compatibility).
  }
  return souxmar_status_ok();
}

// ---- generation ------------------------------------------------------

struct LatticeMesh {
  std::vector<double>        coords;        // 3 * num_nodes
  std::vector<std::uint64_t> strut_nodes;   // 2 * num_struts
  std::vector<std::int32_t>  strut_tags;    // num_struts
  double                     total_length = 0.0;  // m
};

std::int64_t pack_site(std::int64_t i, std::int64_t j, std::int64_t k) {
  return (i * kPackStride + j) * kPackStride + k;
}

// Diameter (m) realising `relative_density` for a lattice of summed
// strut length `total_length` (m) inside `volume` (m^3):
//     rho* = (pi d^2 / 4) * L / V  =>  d = sqrt(4 rho* V / (pi L)).
// Returns 0.0 when the inputs cannot produce a diameter (empty lattice
// or empty volume) so the caller's range gate rejects it.
double strut_diameter_for_density(double relative_density, double total_length,
                                  double volume) {
  if (!(total_length > 0.0) || !(volume > 0.0) || !(relative_density > 0.0)) {
    return 0.0;
  }
  return std::sqrt(4.0 * relative_density * volume / (kPi * total_length));
}

// Inverse of the above — used to report/verify the achieved density.
double density_for_strut_diameter(double diameter, double total_length,
                                  double volume) {
  if (!(volume > 0.0)) return 0.0;
  return (kPi * diameter * diameter / 4.0) * total_length / volume;
}

// Walks the cell grid in index order and emits deduplicated nodes +
// struts. Node ids are assigned on first touch; strut keys are sorted
// (lo, hi) pairs held in an ordered set, so the whole traversal is
// deterministic and platform-independent.
void generate_lattice(const double origin[3], double cell_size, int nx, int ny,
                      int nz, UnitCell cell_type, LatticeMesh* out) {
  const std::span<const Strut> struts = struts_for(cell_type);
  const double                 quarter = cell_size * 0.25;

  std::map<std::int64_t, std::uint64_t>        node_of_site;
  std::set<std::pair<std::uint64_t, std::uint64_t>> seen_struts;

  const auto node_at_site = [&](std::int64_t I, std::int64_t J,
                                std::int64_t K) -> std::uint64_t {
    const std::int64_t key = pack_site(I, J, K);
    const auto         it  = node_of_site.find(key);
    if (it != node_of_site.end()) return it->second;
    const std::uint64_t id =
        static_cast<std::uint64_t>(out->coords.size() / 3);
    out->coords.push_back(origin[0] + quarter * static_cast<double>(I));
    out->coords.push_back(origin[1] + quarter * static_cast<double>(J));
    out->coords.push_back(origin[2] + quarter * static_cast<double>(K));
    node_of_site.emplace(key, id);
    return id;
  };

  for (int k = 0; k < nz; ++k) {
    for (int j = 0; j < ny; ++j) {
      for (int i = 0; i < nx; ++i) {
        const std::int64_t base_i = static_cast<std::int64_t>(i) * 4;
        const std::int64_t base_j = static_cast<std::int64_t>(j) * 4;
        const std::int64_t base_k = static_cast<std::int64_t>(k) * 4;
        for (const Strut& s : struts) {
          const std::uint64_t na = node_at_site(base_i + s.a[0],
                                                base_j + s.a[1],
                                                base_k + s.a[2]);
          const std::uint64_t nb = node_at_site(base_i + s.b[0],
                                                base_j + s.b[1],
                                                base_k + s.b[2]);
          if (na == nb) continue;  // degenerate — cannot happen with the
                                   // tables above, but never emit one.
          const std::pair<std::uint64_t, std::uint64_t> key =
              (na < nb) ? std::pair<std::uint64_t, std::uint64_t>{na, nb}
                        : std::pair<std::uint64_t, std::uint64_t>{nb, na};
          if (!seen_struts.insert(key).second) continue;  // already emitted
          out->strut_nodes.push_back(key.first);
          out->strut_nodes.push_back(key.second);
          out->strut_tags.push_back(s.family);
          const double dx = out->coords[key.first * 3 + 0] -
                            out->coords[key.second * 3 + 0];
          const double dy = out->coords[key.first * 3 + 1] -
                            out->coords[key.second * 3 + 1];
          const double dz = out->coords[key.first * 3 + 2] -
                            out->coords[key.second * 3 + 2];
          // Accumulated in strut-emission (index) order — determinism.
          out->total_length += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
      }
    }
  }
}

// ---- reader entry point ----------------------------------------------

souxmar_status_t lattice_read(const char*                     path,
                             const souxmar_value_t*          inputs,
                             const souxmar_reader_options_t* /*options*/,
                             souxmar_mesh_t**                out_mesh,
                             souxmar_geometry_t**            out_geometry,
                             void*                           /*user_data*/) {
  if (!path) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "reader.lattice: path is NULL — `path:` names the lattice spec file "
        "and is required");
  }
  if (!out_mesh || !out_geometry) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "out_mesh / out_geometry are NULL");
  }
  *out_mesh     = nullptr;
  *out_geometry = nullptr;

  LatticeSpec spec;
  const souxmar_status_t file_status = parse_spec_file(path, &spec);
  if (file_status.code != SOUXMAR_OK) return file_status;

  // Value-bag entries override the spec file.
  double bbox_override[6] = {0, 0, 0, 0, 0, 0};
  if (read_number_list(inputs, "bbox", bbox_override, 6)) {
    for (std::size_t i = 0; i < 6; ++i) spec.bbox[i] = bbox_override[i];
  }
  spec.unit_cell        = read_string(inputs, "unit_cell", spec.unit_cell);
  spec.cell_size        = read_number(inputs, "cell_size", spec.cell_size);
  spec.strut_diameter   = read_number(inputs, "strut_diameter", spec.strut_diameter);
  spec.relative_density = read_number(inputs, "relative_density", spec.relative_density);
  spec.nx               = read_int(inputs, "nx", spec.nx);
  spec.ny               = read_int(inputs, "ny", spec.ny);
  spec.nz               = read_int(inputs, "nz", spec.nz);
  spec.max_cells        = static_cast<std::int64_t>(
      read_number(inputs, "max_cells", static_cast<double>(spec.max_cells)));

  UnitCell cell_type = UnitCell::Bcc;
  if (!unit_cell_from_string(spec.unit_cell, &cell_type)) {
    std::string& msg = error_scratch();
    msg = "reader.lattice: unknown unit_cell '" + spec.unit_cell +
          "' — expected cubic, bcc, fcc, octet or diamond";
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, msg.c_str());
  }

  // Normalise the box (accept it given inverted) and clamp the scalars.
  double origin[3] = {0.0, 0.0, 0.0};
  double span[3]   = {0.0, 0.0, 0.0};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double lo = std::min(spec.bbox[axis], spec.bbox[axis + 3]);
    const double hi = std::max(spec.bbox[axis], spec.bbox[axis + 3]);
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
      return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
          "reader.lattice: bbox contains a non-finite coordinate");
    }
    origin[axis] = lo;
    span[axis]   = hi - lo;
  }
  const double cell_size = std::clamp(spec.cell_size, kMinCellSize, kMaxCellSize);
  const double rel_density =
      std::clamp(spec.relative_density, kMinRelDensity, kMaxRelDensity);
  const std::int64_t max_cells =
      std::clamp<std::int64_t>(spec.max_cells, 1, kMaxMaxCells);

  // Cell counts: explicit when given (> 0), otherwise the nearest whole
  // number of cells that fits the span. The lattice is anchored at the
  // box minimum and spans n*cell_size, which can differ from the box by
  // up to half a cell per axis — give nx/ny/nz explicitly when that
  // matters. A degenerate axis yields the clamped minimum of 1 cell.
  const auto derive = [cell_size](int explicit_n, double axis_span) {
    const int n = (explicit_n > 0)
                      ? explicit_n
                      : static_cast<int>(std::llround(axis_span / cell_size));
    return std::clamp(n, kMinCellsPerAxis, kMaxCellsPerAxis);
  };
  const int nx = derive(spec.nx, span[0]);
  const int ny = derive(spec.ny, span[1]);
  const int nz = derive(spec.nz, span[2]);

  // Cap the emitted Edge2 count before generating anything. The
  // pre-dedup product is an upper bound on the deduplicated total.
  const double strut_estimate = static_cast<double>(nx) * static_cast<double>(ny) *
                                static_cast<double>(nz) *
                                static_cast<double>(struts_for(cell_type).size());
  if (strut_estimate > static_cast<double>(max_cells)) {
    std::string& msg = error_scratch();
    msg = "reader.lattice: " + std::to_string(nx) + "x" + std::to_string(ny) +
          "x" + std::to_string(nz) + " " + spec.unit_cell + " cells would emit up to " +
          std::to_string(static_cast<std::int64_t>(strut_estimate)) +
          " struts, above max_cells = " + std::to_string(max_cells) +
          " — raise cell_size, shrink the bbox, or raise max_cells";
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, msg.c_str());
  }

  LatticeMesh lat;
  generate_lattice(origin, cell_size, nx, ny, nz, cell_type, &lat);
  if (lat.strut_tags.empty()) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
        "reader.lattice: generated an empty lattice (should be unreachable — "
        "every unit cell contributes at least 8 struts)");
  }

  // Strut diameter: explicit, else derived from the relative density.
  // The value cannot ride along on the mesh (the reader ABI carries no
  // scalar metadata), so it is used here as a feasibility gate and is
  // documented in the header + example.lattice for whoever sizes the
  // beam sections downstream.
  const double lattice_volume = static_cast<double>(nx) * static_cast<double>(ny) *
                                static_cast<double>(nz) * cell_size * cell_size *
                                cell_size;
  const double diameter =
      (spec.strut_diameter > 0.0)
          ? spec.strut_diameter
          : strut_diameter_for_density(rel_density, lat.total_length, lattice_volume);
  if (!(diameter >= kMinStrutDiameter)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "reader.lattice: strut diameter resolves below 1 um — raise "
        "relative_density, raise cell_size, or set strut_diameter explicitly");
  }
  if (!(diameter < kMaxStrutDiameterFraction * cell_size)) {
    std::string& msg = error_scratch();
    msg = "reader.lattice: strut diameter " + std::to_string(diameter) +
          " m is not below cell_size (" + std::to_string(cell_size) +
          " m) — a strut as thick as the cell it tiles is a solid block, not a "
          "lattice (the overlap-free relation reports relative density " +
          std::to_string(density_for_strut_diameter(diameter, lat.total_length,
                                                    lattice_volume)) +
          "); lower relative_density, lower strut_diameter, or raise cell_size";
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, msg.c_str());
  }

  const std::size_t num_nodes  = lat.coords.size() / 3;
  const std::size_t num_struts = lat.strut_tags.size();

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
  }
  souxmar_mesh_reserve_nodes(mesh, num_nodes);
  souxmar_mesh_reserve_cells(mesh, num_struts);

  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double p[3] = {lat.coords[i * 3 + 0],
                         lat.coords[i * 3 + 1],
                         lat.coords[i * 3 + 2]};
    souxmar_mesh_add_node(mesh, p);
  }
  for (std::size_t i = 0; i < num_struts; ++i) {
    const std::uint64_t nodes[2] = {lat.strut_nodes[i * 2 + 0],
                                    lat.strut_nodes[i * 2 + 1]};
    // Cell tag = strut family (contract 3.2).
    const souxmar_status_t cs = souxmar_mesh_add_cell(
        mesh, SOUXMAR_ET_EDGE2, nodes, 2, lat.strut_tags[i], nullptr);
    if (cs.code != SOUXMAR_OK) {
      souxmar_mesh_free(mesh);
      return cs;
    }
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_reader_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &lattice_read,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_reader(
      registry, "reader.lattice", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
