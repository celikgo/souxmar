// SPDX-License-Identifier: Apache-2.0
//
// marine — pressure-hull loads and preliminary collapse sizing for
// additively-manufactured subsea parts.
//
// This translation unit registers two capabilities and carries the plugin's
// single exported symbol; `solver.marine.corrosion` and
// `writer.marine.qualification_report` live in marine_integrity.cpp and are
// published here through the vtable declarations in marine_data.hpp.
//
//   solver.marine.hydrostatic     nodal pressure per depth load case
//   solver.marine.hull_collapse   governing collapse pressure + margin
//
// ===========================================================================
// solver.marine.hydrostatic
// ===========================================================================
//
// What it computes:
//   Seawater hydrostatic pressure at every node, once per depth load case.
//   The mesh is treated as a rigid body lowered to each depth: the highest
//   node along `build_direction` sits at
//
//       depth_top(j) = design_depth * depth_factors[j]
//
//   and every other node sits deeper by its distance below that node, so
//
//       p(i, j) = rho * g * (depth_top(j) + (z_top - z_i))  [+ p_atm]
//       z_i     = dot(x_i, b_hat)          b_hat = normalised build_direction
//
//   `rho` is either the explicit `seawater_density` input or, when that is
//   <= 0, the one-atmosphere International Equation of State of Seawater
//   (Millero & Poisson 1981 / UNESCO 44, 1983) evaluated at `salinity_psu`
//   and `seawater_temperature` — see marine_data.hpp for the coefficients,
//   the validity band (0-42 PSU, -2..40 degC) and the 3.6e-3 kg/m^3 fit
//   error. rho(35 PSU, 10 degC) = 1026.95 kg/m^3, so the default 300 m
//   design depth gives 3.021 MPa gauge at the top of the hull.
//
//   The three default `depth_factors` are the three load cases a subsea
//   pressure-boundary design is checked at, and they land in the field as
//   three time steps:
//       step 0  factor 1.00   operating / maximum operating depth
//       step 1  factor 1.50   test / proof depth (hydrostatic test)
//       step 2  factor 2.25   collapse / crush depth
//   Any list of 1..16 factors is accepted; the step index is the list index,
//   so the meaning of a step is whatever the caller's list says it is.
//
//   Sign convention: the field is a positive pressure MAGNITUDE, compressive
//   on the wetted surface. It is a gauge pressure unless
//   `include_atmospheric` is true, in which case `atmospheric_pressure` is
//   added to give an absolute pressure. A structural stage consuming this
//   field must apply it as a follower load directed along the inward surface
//   normal; this solver does not know which faces are wetted.
//
// What this is NOT:
//   * Not a structural solution — there is no stress, strain or deflection
//     here, only the applied pressure.
//   * The equation of state is the one-atmosphere form. In-situ seawater is
//     compressed by the column above it, which raises density by roughly
//     0.5 % per 1000 m; at 300 m the resulting pressure error is under
//     0.2 %, at 3000 m it is ~1.5 % (unconservative).
//   * Every node is loaded, wetted or not. Internal / dry surfaces are
//     the consumer's problem.
//   * No dynamic pressure, no wave or slamming load, no thermal gradient,
//     no internal (compensated) pressure.
//
// Inputs (souxmar_value_t map):
//   design_depth          : number, m,      default 300     (clamped [0, 11000])
//   depth_factors         : list of number, default [1.0, 1.5, 2.25]
//                                           (first 16 entries, each clamped
//                                            [0, 20]; empty => default list)
//   seawater_density      : number, kg/m^3, default 0 => EOS-80 correlation
//                                           (explicit values clamped [900, 1300])
//   salinity_psu          : number, PSU,    default 35.0
//   seawater_temperature  : number, degC,   default 10.0
//   gravity               : number, m/s^2,  default 9.80665 (clamped [0, 30])
//   include_atmospheric   : bool,           default false
//   atmospheric_pressure  : number, Pa,     default 101325  (clamped [0, 1e7])
//   build_direction       : list of 3,      default [0, 0, 1] — the "up" axis
//
// Output: nodal scalar Field "hydrostatic_pressure" in Pa, one time step per
// depth factor.
//
// ===========================================================================
// solver.marine.hull_collapse
// ===========================================================================
//
// What it computes:
//   Closed-form preliminary collapse pressures for the three hull forms the
//   contract names, the governing (minimum) one, and the margin over the
//   factored design pressure. `diameter` is the shell OUTER diameter D and
//   `unsupported_length` is the frame spacing L (centre-to-centre of the
//   ring frames, or the length between end closures for an unstiffened
//   cylinder).
//
//   Mode 0 — interframe shell instability (lobar collapse between frames),
//   the Windenburg & Trilling (1934) closed-form approximation to von Mises'
//   solution, in the form given in Ross, "Pressure Vessels: External
//   Pressure Technology", 2nd ed. §3, and in Moss, "Pressure Vessel Design
//   Manual", 4th ed.:
//
//       p_wt = 2.42 * E * (t/D)^2.5
//              / [ (1 - nu^2)^0.75 * ( L/D - 0.45 * sqrt(t/D) ) ]
//
//   Valid for thin shells (t/D <~ 0.05) with L/D > 0.45*sqrt(t/D); the
//   bracket is checked before the division and the mode is dropped (not
//   clamped) when the geometry falls outside it.
//
//   Mode 1 — membrane yield. For a cylinder the thin-wall hoop stress is
//   p*D/(2t), so first yield of the shell membrane is at
//
//       p_y = 2 * sigma_y * t / D
//
//   as fixed by the capability contract. Using the mean diameter (D - t)
//   instead of the outer diameter would raise p_y by t/(D-t), i.e. 1.2 % at
//   the default geometry — far inside the model's own error.
//
//   DELIBERATE, DOCUMENTED DEVIATION: the contract quotes one membrane-yield
//   formula, p = 2*sigma_y*t/D, which is the CYLINDER hoop form. A sphere's
//   membrane stress is p*D/(4t), so this plugin uses p_y = 4*sigma_y*t/D for
//   `hull_type: sphere`. Applying the cylinder form to a sphere would halve
//   the reported yield pressure — conservative, but wrong, and wrong in a way
//   a naval architect would read as a bug.
//
//   Mode 2 — long-cylinder ("general") elastic instability, the classical
//   ring/Bresse-Levy result with the plane-strain correction:
//
//       p_long = 2 * E * t^3 / [ (1 - nu^2) * D^3 ]
//
//   This is the collapse of a cylinder whose ring frames contribute nothing,
//   so it is evaluated for `hull_type: cylinder` only.
//
//   Mode 3 — sphere elastic buckling, the classical Zoelly (1915) result
//
//       p_z = 2 * E * (t/R)^2 / sqrt(3 * (1 - nu^2))
//
//   Knockdowns. Elastic instability modes (0, 2, 3) are multiplied by
//   `imperfection_knockdown` * `am_anisotropy_knockdown`; the yield mode (1)
//   by `am_anisotropy_knockdown` alone, because out-of-roundness knocks down
//   buckling, not the yield strength. The governing collapse pressure is the
//   minimum over the applicable modes AFTER knockdown, ties resolved to the
//   lowest mode code.
//
//   Margin. p_design = seawater_density * gravity * design_depth (gauge,
//   atmospheric excluded), and
//
//       margin = p_collapse / (p_design * safety_factor)
//
//   A margin < 1 means the hull does not pass its own stated factor.
//
//   Hand-check, reproducible from the defaults (D = 1.0 m, t = 12 mm,
//   L = 500 mm, E = 190 GPa, nu = 0.28, sigma_y = 500 MPa, 316L,
//   ring-stiffened): p_wt = 17.11 MPa unfactored -> 11.55 MPa after
//   0.75 * 0.90; p_y = 12.00 MPa unfactored -> 10.80 MPa after 0.90. Yield
//   governs at 10.80 MPa; p_design at 300 m with rho = 1025 is 3.016 MPa, so
//   the margin against 1.5 * p_design is 2.39.
//
// What this is NOT:
//   * NOT a classification-society calculation. This is preliminary sizing
//     arithmetic. A pressure hull is approved against a class rule
//     (e.g. DNV-RU-SHIP Pt.5 Ch.7 for submersibles, ABS Underwater Vehicles,
//     or a naval standard) with the surveyor's own formulae, tolerances,
//     weld and out-of-roundness measurements. Nothing here substitutes for
//     that, and nothing here is a permit to dive.
//   * General instability of a RING-STIFFENED cylinder is not evaluated:
//     it needs the ring frame area, the frame second moment of inertia and
//     the bulkhead spacing (Bryant 1954 / Kendrick), and this capability's
//     input contract supplies none of them. It is frequently the governing
//     mode for a real framed hull and MUST be checked separately.
//   * Frame yielding, frame tripping, shell-frame interaction, end-closure
//     and penetration effects, residual stress from welding or from the AM
//     build, and creep are all ignored.
//   * The result is uniform over the mesh — every cell carries the same
//     triple. It is an analytical answer keyed to the input geometry, not a
//     mesh-resolved one, so refining the mesh will not change it.
//   * `imperfection_knockdown` defaults to 0.75, which is a reasonable
//     out-of-roundness allowance for a well-controlled cylinder. For a
//     SPHERE the classical Zoelly pressure overpredicts test data badly:
//     measured hemisphere collapse commonly falls at 0.2-0.5 of classical
//     (von Karman & Tsien 1939; Krenzke & Kiernan 1963). Lower the input
//     accordingly — the plugin will not do it for you.
//   * `am_anisotropy_knockdown` defaults to 0.90. Reported LPBF 316L
//     property anisotropy between in-plane and build-direction loading is
//     typically 5-15 %; 0.90 sits mid-band and must be replaced by the
//     project's own witness-coupon data (which is exactly the evidence
//     writer.marine.qualification_report asks for).
//
// Inputs (souxmar_value_t map):
//   hull_type              : string, cylinder | ring_stiffened_cylinder |
//                            sphere; default ring_stiffened_cylinder
//                            (anything else => SOUXMAR_E_INVALID_ARGUMENT)
//   diameter               : number, m,   default 1.0   (clamped [1e-3, 100])
//   thickness              : number, m,   default 0.012 (clamped [1e-5, 1];
//                            t/D > 0.25 => SOUXMAR_E_INVALID_ARGUMENT)
//   unsupported_length     : number, m,   default 0.5   (ignored for sphere)
//   youngs_modulus         : number, Pa,  default 1.9e11
//   poisson_ratio          : number, -,   default 0.28  (clamped [0, 0.49])
//   yield_strength         : number, Pa,  default 5.0e8
//   design_depth           : number, m,   default 300
//   seawater_density       : number, kg/m^3, default 1025
//   gravity                : number, m/s^2, default 9.80665
//   imperfection_knockdown : number, -,   default 0.75  (clamped (0, 1])
//   am_anisotropy_knockdown: number, -,   default 0.90  (clamped (0, 1])
//   safety_factor          : number, -,   default 1.5   (clamped [1, 10])
//
// Output: cell vector Field "collapse_margin", 1 time step, components
//   [0] collapse_pressure_Pa   governing knocked-down collapse pressure
//   [1] margin                 collapse / (design pressure * safety factor)
//   [2] governing_mode_code    0 interframe elastic instability
//                              1 membrane yield
//                              2 general (long-cylinder) instability
//                              3 sphere elastic buckling

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"
#include "souxmar-c/writer.h"

#include "marine_data.hpp"

namespace {

using souxmar_marine::read_bool;
using souxmar_marine::read_number;
using souxmar_marine::read_string;
using souxmar_marine::read_vec3;
using Vec3 = std::array<double, 3>;

// ---- shared clamp ranges (documented in the header comment above) --------
constexpr double kMaxDepthM        = 11000.0;  // m, Challenger Deep ~10 935 m
constexpr double kMaxDepthFactor   = 20.0;     // -, a 20x proof factor is absurd already
constexpr std::size_t kMaxFactors  = 16;       // contract §3.15 caps the list at 16
constexpr double kMinDensity       = 900.0;    // kg/m^3, fresher than any sea water
constexpr double kMaxDensity       = 1300.0;   // kg/m^3, saltier than the Dead Sea (1240)
constexpr double kMaxGravity       = 30.0;     // m/s^2
constexpr double kMaxAtmPressure   = 1.0e7;    // Pa, 100 bar of "atmosphere"

// Default load cases: operating / test / collapse (contract §3.15).
constexpr std::array<double, 3> kDefaultDepthFactors = {1.0, 1.5, 2.25};

// Resolve and normalise `build_direction` (contract §2.1). Returns false for
// a zero-length vector, which the caller reports as an invalid argument.
bool resolve_build_direction(const souxmar_value_t* inputs, Vec3* out) {
  Vec3 b{0.0, 0.0, 1.0};
  read_vec3(inputs, "build_direction", &b);
  const double n = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
  if (!(n > 0.0)) return false;
  *out = {b[0] / n, b[1] / n, b[2] / n};
  return true;
}

// Seawater density: explicit input wins, otherwise the EOS-80 correlation.
double resolve_seawater_density(const souxmar_value_t* inputs) {
  const double explicit_rho = read_number(inputs, "seawater_density", 0.0);
  if (explicit_rho > 0.0) {
    return std::clamp(explicit_rho, kMinDensity, kMaxDensity);
  }
  const double salinity = read_number(inputs, "salinity_psu",
                                      souxmar_marine::kRefSalinityPsu);
  const double temp_c   = read_number(inputs, "seawater_temperature",
                                      souxmar_marine::kRefTemperatureC);
  return souxmar_marine::seawater_density_kg_per_m3(salinity, temp_c);
}

// ---------------------------------------------------------------------------
// solver.marine.hydrostatic
// ---------------------------------------------------------------------------

std::vector<double> read_depth_factors(const souxmar_value_t* inputs) {
  std::vector<double> factors;
  const souxmar_value_t* list =
      (inputs && souxmar_value_kind(inputs) == SOUXMAR_VK_MAP)
          ? souxmar_value_map_get(inputs, "depth_factors")
          : nullptr;
  if (list && souxmar_value_kind(list) == SOUXMAR_VK_LIST) {
    // Entries past the 16th are ignored; non-numeric entries are skipped.
    const std::size_t n = std::min<std::size_t>(souxmar_value_list_size(list),
                                                kMaxFactors);
    factors.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      const souxmar_value_t* c = souxmar_value_list_at(list, i);
      if (c && souxmar_value_kind(c) == SOUXMAR_VK_NUMBER) {
        factors.push_back(
            std::clamp(souxmar_value_as_number(c), 0.0, kMaxDepthFactor));
      }
    }
  }
  if (factors.empty()) {
    factors.assign(kDefaultDepthFactors.begin(), kDefaultDepthFactors.end());
  }
  return factors;
}

souxmar_status_t hydrostatic_solve(const souxmar_mesh_t*           mesh,
                                  const souxmar_value_t*          inputs,
                                  const souxmar_solver_options_t* /*options*/,
                                  souxmar_field_t**               out_field,
                                  void*                           /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  Vec3 up{0.0, 0.0, 1.0};
  if (!resolve_build_direction(inputs, &up)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }

  const double design_depth = std::clamp(read_number(inputs, "design_depth", 300.0),
                                         0.0, kMaxDepthM);
  const double rho     = resolve_seawater_density(inputs);
  const double gravity = std::clamp(read_number(inputs, "gravity", 9.80665),
                                    0.0, kMaxGravity);
  const bool   add_atm = read_bool(inputs, "include_atmospheric", false);
  const double p_atm   = add_atm
      ? std::clamp(read_number(inputs, "atmospheric_pressure", 101325.0),
                   0.0, kMaxAtmPressure)
      : 0.0;

  std::vector<double> factors;
  std::vector<double> depth_below_top;  // metres below the highest node
  try {
    factors = read_depth_factors(inputs);
    depth_below_top.resize(num_nodes);
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                               "allocation failed while reading depth_factors");
  }

  // z_i = projection of node i on the up axis; z_top = the highest node.
  // Accumulate in index order so the result is bit-stable.
  double z_top = coords[0] * up[0] + coords[1] * up[1] + coords[2] * up[2];
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double z = coords[i * 3 + 0] * up[0] + coords[i * 3 + 1] * up[1] +
                     coords[i * 3 + 2] * up[2];
    depth_below_top[i] = z;  // provisional: absolute projection
    if (z > z_top) z_top = z;
  }
  for (std::size_t i = 0; i < num_nodes; ++i) {
    // Non-negative by construction; the max() only guards round-off at z_top.
    depth_below_top[i] = std::max(z_top - depth_below_top[i], 0.0);
  }

  const std::size_t num_steps = factors.size();
  souxmar_field_t* field = souxmar_field_new(
      "hydrostatic_pressure", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR,
      num_nodes, num_steps);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 1 * num_steps) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout: data[step * count * components + location * components + comp],
  // components == 1 here.
  for (std::size_t step = 0; step < num_steps; ++step) {
    const double depth_top = std::clamp(design_depth * factors[step], 0.0, kMaxDepthM);
    for (std::size_t i = 0; i < num_nodes; ++i) {
      const double depth = depth_top + depth_below_top[i];
      data[step * num_nodes + i] = rho * gravity * depth + p_atm;
    }
  }

  *out_field = field;
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------------
// solver.marine.hull_collapse
// ---------------------------------------------------------------------------

enum class HullType : int { Cylinder = 0, RingStiffenedCylinder = 1, Sphere = 2 };

struct CollapseMode {
  double      pressure_pa;  // after knockdown
  int         code;         // contract §3.16 governing_mode_code
  const char* name;         // documentation only
};

// Windenburg & Trilling (1934) interframe collapse. `applicable` is false
// when the geometry falls outside the formula's domain (the bracket
// L/D - 0.45*sqrt(t/D) must be positive), in which case the mode is dropped
// rather than clamped to a made-up number.
double windenburg_trilling_pa(double e_pa, double nu, double t, double d, double l,
                              bool* applicable) {
  const double t_over_d = t / d;
  const double bracket  = (l / d) - 0.45 * std::sqrt(t_over_d);
  if (!(bracket > 1.0e-9)) {
    *applicable = false;
    return 0.0;
  }
  *applicable = true;
  // (t/D)^2.5 and (1-nu^2)^0.75 are spelled out in multiplies and square
  // roots rather than std::pow: sqrt is a correctly-rounded IEEE-754
  // operation on every platform we ship, pow is not, and the determinism
  // gate wants this number bit-identical on Linux, macOS and Windows.
  //   x^2.5  = x*x*sqrt(x)
  //   y^0.75 = sqrt(y*sqrt(y))
  const double t_pow_2p5 = t_over_d * t_over_d * std::sqrt(t_over_d);
  const double y         = 1.0 - nu * nu;
  const double y_pow_0p75 = std::sqrt(y * std::sqrt(y));
  return 2.42 * e_pa * t_pow_2p5 / (y_pow_0p75 * bracket);
}

souxmar_status_t hull_collapse_solve(const souxmar_mesh_t*           mesh,
                                     const souxmar_value_t*          inputs,
                                     const souxmar_solver_options_t* /*options*/,
                                     souxmar_field_t**               out_field,
                                     void*                           /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no cells");
  }

  const char* hull_type_str = read_string(inputs, "hull_type", "ring_stiffened_cylinder");
  HullType hull_type = HullType::RingStiffenedCylinder;
  if (souxmar_marine::ascii_iequals(hull_type_str, "cylinder")) {
    hull_type = HullType::Cylinder;
  } else if (souxmar_marine::ascii_iequals(hull_type_str, "ring_stiffened_cylinder")) {
    hull_type = HullType::RingStiffenedCylinder;
  } else if (souxmar_marine::ascii_iequals(hull_type_str, "sphere")) {
    hull_type = HullType::Sphere;
  } else {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "hull_type must be cylinder, ring_stiffened_cylinder or sphere");
  }

  // Geometry. Clamps: a hull between 1 mm and 100 m across, a wall between
  // 10 um and 1 m, a frame spacing between 1 mm and 1 km.
  const double diameter  = std::clamp(read_number(inputs, "diameter", 1.0), 1.0e-3, 100.0);
  const double thickness = std::clamp(read_number(inputs, "thickness", 0.012), 1.0e-5, 1.0);
  const double length    = std::clamp(read_number(inputs, "unsupported_length", 0.5),
                                      1.0e-3, 1000.0);
  // Every formula below is a thin-shell result. Beyond t/D = 0.25 (D/t = 4)
  // they are not approximately wrong, they are meaningless, so refuse rather
  // than return a number someone might believe.
  if (thickness / diameter > 0.25) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "thickness/diameter exceeds 0.25; thin-shell collapse formulas do not apply");
  }

  const double e_pa  = std::clamp(read_number(inputs, "youngs_modulus", 1.9e11),
                                  1.0e6, 1.0e13);
  const double nu    = std::clamp(read_number(inputs, "poisson_ratio", 0.28), 0.0, 0.49);
  const double sig_y = std::clamp(read_number(inputs, "yield_strength", 5.0e8),
                                  1.0e5, 1.0e11);

  const double design_depth = std::clamp(read_number(inputs, "design_depth", 300.0),
                                         0.0, kMaxDepthM);
  const double rho = std::clamp(read_number(inputs, "seawater_density", 1025.0),
                                kMinDensity, kMaxDensity);
  const double gravity = std::clamp(read_number(inputs, "gravity", 9.80665),
                                    0.0, kMaxGravity);
  const double k_imp = std::clamp(read_number(inputs, "imperfection_knockdown", 0.75),
                                  1.0e-3, 1.0);
  const double k_am  = std::clamp(read_number(inputs, "am_anisotropy_knockdown", 0.90),
                                  1.0e-3, 1.0);
  const double sf    = std::clamp(read_number(inputs, "safety_factor", 1.5), 1.0, 10.0);

  const double radius = 0.5 * diameter;

  // ---- assemble the applicable modes, in ascending mode-code order -------
  std::array<CollapseMode, 4> modes{};
  std::size_t mode_count = 0;

  if (hull_type == HullType::Cylinder || hull_type == HullType::RingStiffenedCylinder) {
    bool wt_ok = false;
    const double p_wt = windenburg_trilling_pa(e_pa, nu, thickness, diameter, length,
                                               &wt_ok);
    if (wt_ok) {
      modes[mode_count++] = {p_wt * k_imp * k_am, 0, "interframe elastic instability"};
    }
    // Contract formula, cylinder hoop membrane: p = 2*sigma_y*t/D.
    modes[mode_count++] = {2.0 * sig_y * thickness / diameter * k_am, 1, "membrane yield"};
  }
  if (hull_type == HullType::Cylinder) {
    // Long (frameless) cylinder, plane-strain ring buckling:
    //   p = E t^3 / (4 (1-nu^2) R^3) = 2 E t^3 / ((1-nu^2) D^3).
    const double p_long = 2.0 * e_pa * thickness * thickness * thickness /
                          ((1.0 - nu * nu) * diameter * diameter * diameter);
    modes[mode_count++] = {p_long * k_imp * k_am, 2, "general (long-cylinder) instability"};
  }
  if (hull_type == HullType::Sphere) {
    // Sphere membrane stress is p*D/(4t) -> yield at p = 4*sigma_y*t/D. The
    // contract quotes the cylinder form; see the header comment.
    modes[mode_count++] = {4.0 * sig_y * thickness / diameter * k_am, 1, "membrane yield"};
    // Zoelly (1915) classical elastic buckling of a complete sphere.
    const double t_over_r = thickness / radius;
    const double p_z = 2.0 * e_pa * t_over_r * t_over_r /
                       std::sqrt(3.0 * (1.0 - nu * nu));
    modes[mode_count++] = {p_z * k_imp * k_am, 3, "sphere elastic buckling"};
  }

  if (mode_count == 0) {
    // Only reachable when the sole cylinder mode (Windenburg-Trilling) is out
    // of domain, which cannot happen because membrane yield is always added.
    // Kept as a guard so a future mode edit cannot silently divide by zero.
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "no collapse mode is applicable to this geometry");
  }

  // Governing = minimum after knockdown; strict `<` keeps the lowest mode
  // code on a tie, and iteration is in fixed index order.
  CollapseMode governing = modes[0];
  for (std::size_t i = 1; i < mode_count; ++i) {
    if (modes[i].pressure_pa < governing.pressure_pa) governing = modes[i];
  }

  const double p_design  = rho * gravity * design_depth;
  const double p_factored = p_design * sf;
  // margin is dimensionless; at zero design depth the margin is unbounded, so
  // report 0 as the documented "not evaluated" sentinel rather than inf/NaN.
  const double margin = (p_factored > 0.0) ? (governing.pressure_pa / p_factored) : 0.0;

  souxmar_field_t* field = souxmar_field_new(
      "collapse_margin", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells,
      /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_cells * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }
  for (std::size_t c = 0; c < num_cells; ++c) {
    data[c * 3 + 0] = governing.pressure_pa;
    data[c * 3 + 1] = margin;
    data[c * 3 + 2] = static_cast<double>(governing.code);
  }

  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kHydrostaticVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &hydrostatic_solve,
    nullptr,
};

constexpr souxmar_solver_vtable_t kHullCollapseVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &hull_collapse_solve,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s_hydro = souxmar_registry_add_solver(
      registry, "solver.marine.hydrostatic", &kHydrostaticVtable,
      /*user_data=*/nullptr);
  if (s_hydro.code != SOUXMAR_OK) return 1;

  const souxmar_status_t s_collapse = souxmar_registry_add_solver(
      registry, "solver.marine.hull_collapse", &kHullCollapseVtable,
      /*user_data=*/nullptr);
  if (s_collapse.code != SOUXMAR_OK) return 1;

  // Implemented in marine_integrity.cpp; registered here so the shared
  // object exports exactly one symbol.
  const souxmar_status_t s_corrosion = souxmar_registry_add_solver(
      registry, "solver.marine.corrosion", &souxmar_marine::kCorrosionVtable,
      /*user_data=*/nullptr);
  if (s_corrosion.code != SOUXMAR_OK) return 1;

  const souxmar_status_t s_report = souxmar_registry_add_writer(
      registry, "writer.marine.qualification_report",
      &souxmar_marine::kQualificationReportVtable, /*user_data=*/nullptr);
  if (s_report.code != SOUXMAR_OK) return 1;

  return 0;
}
