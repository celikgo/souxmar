// SPDX-License-Identifier: Apache-2.0
//
// am-thermal — laser powder-bed fusion (LPBF) thermal history and
// melt-pool diagnostics. Two capabilities in one plugin:
//
//   solver.am.thermal.lpbf   nodal "temperature" [°C], one step per layer
//   postproc.am.melt_pool    per-cell "melt_pool" vector, one step
//
// Defaults are 316L stainless steel on a 400 W LPBF machine.
//
// =====================================================================
// What it computes
// =====================================================================
//
// (1) Rosenthal moving point source — Rosenthal, "The theory of moving
//     sources of heat and its application to metal treatments",
//     Trans. ASME 68 (1946) 849-866. Quasi-steady temperature around a
//     point source of absorbed power q = absorptivity * laser_power
//     travelling at v = scan_speed over the surface of a semi-infinite
//     solid:
//
//       T(w, y, z) = T0 + q / (2*pi*k*R) * exp(-v*(R + w) / (2*alpha))
//         R     = sqrt(w^2 + y^2 + z^2)   [m]     distance to the source
//         w     = source-frame coordinate, > 0 *ahead* of the source
//         alpha = k / (rho * c)           [m^2/s] thermal diffusivity
//
//     The 2*pi (not 4*pi) is the surface-source form: a source sitting on
//     the free surface of a half-space spreads heat into a hemisphere.
//     The exponent decays fast ahead of the source (w > 0) and barely at
//     all behind it (R + w -> 0 on the trailing centreline) — that is the
//     long thermal tail of a weld/scan track.
//
// (2) Melt-pool depth D — the T = melt_temperature isotherm on the
//     centreline directly under the source (w = y = 0, z = D, so R = D):
//
//       q / (2*pi*k*D) * exp(-v*D / (2*alpha)) = T_melt - T0
//
//     The left-hand side is strictly decreasing in D, so the root is
//     bracketed by (0, R_c] with R_c = q / (2*pi*k*(T_melt - T0)) and
//     found by a fixed 100-step bisection.
//
// (3) Melt-pool width W — the widest point of the same isotherm on the
//     free surface (z = 0). Parametrising the isotherm by R:
//
//       w(R)  = -R - (2*alpha/v) * ln(R / R_c)
//       y(R)^2 = R^2 - w(R)^2
//
//     y vanishes at R = R_c (the trailing tip, w = -R_c) and where
//     |w| = R (the leading tip) and is unimodal in between;
//     W = 2 * max_R y(R), located with a fixed 1024-point scan plus 100
//     ternary-search refinements. Fixed iteration counts are deliberate:
//     the determinism gate forbids tolerance-based exits whose iteration
//     count could differ between platforms.
//
// (4) Peak surface temperature of a freshly scanned layer — evaluated at
//     the mid-hatch offset y0 = hatch_spacing / 2, i.e. the *coldest*
//     line between two adjacent tracks. That is the conservative place
//     to ask "did this layer melt". Setting dT/dw = 0 and substituting
//     u = w/R gives
//
//       (v*y0 / (2*alpha)) * sqrt((1 + u)/(1 - u)) + u = 0,  u in (-1, 0)
//
//     which is strictly increasing in u → 100 bisections; then
//     R = y0 / sqrt(1 - u^2), w = u*R, and T_peak = T0 + dT(w, y0, 0).
//     T_peak is capped at 2900 °C (see kVaporisationCapC).
//
// (5) Centreline cooling rate at solidification — the closed form that
//     falls out of (1) at the trailing tip of the melt pool
//     (w = -R = -R_c, dw/dt = -v for a material point):
//
//       |dT/dt| = 2*pi*k*v*(T_melt - T0)^2 / q          [K/s]
//
//     For the 316L defaults (q = 70 W, v = 0.8 m/s, k = 15 W/(m·K),
//     T_melt - T0 = 1320 K) this is 1.9e6 K/s, the right order for LPBF
//     (1e5..1e7 K/s). The ABI's fixed field-component layout has no slot
//     for it, so it is used inside the model as the physical floor on the
//     interlayer cooling time constant (a layer cannot shed its superheat
//     faster than conduction under the source allows) and documented
//     here as a derived diagnostic.
//
// (6) Interlayer decay. A deposited layer of thickness L at dT above the
//     baseplate, sitting on already-built material of the same
//     properties, with an insulated top surface, retains (Carslaw &
//     Jaeger, "Conduction of Heat in Solids", 2nd ed., §2.6 — slab plus
//     image solution)
//
//       phi(t) = erf( L / (2*sqrt(alpha*t)) )     -> L/sqrt(pi*alpha*t)
//
//     of that superheat after t seconds. §3.3 of the capability contract
//     asks for an exponential decay "with the lumped time constant over
//     interlayer_time", so the lumped time constant is *defined* by
//     matching the exact solution at one interlayer interval:
//
//       tau = interlayer_time / ln(1 / phi(interlayer_time))
//       tau = max(tau, (T_peak - T_baseplate) / |dT/dt|_(5))
//
//     and the per-step retained fraction actually used is
//     phi_step = exp(-interlayer_time / tau). For the 316L defaults
//     (L = 1 mm, alpha = 3.75e-6 m^2/s, 10 s dwell) phi = 0.092 and
//     tau = 4.2 s, i.e. the layer below the one being scanned sits at
//     ~230 °C. That is the right order for LPBF; a pure lumped-
//     capacitance tau = rho*c*L^2/k (0.27 s) would have claimed the
//     substrate is back at baseplate temperature within one dwell, which
//     is wrong by ~40x on the retained superheat.
//
// (7) Substrate accumulation. The substrate temperature seen by layer i
//     is carried forward layer by layer (in index order, so the result
//     is reproducible):
//
//       T_sub(first printed layer) = baseplate_temperature
//       T_peak(i)   = min(T_sub(i) + dT_rosenthal, 2900 °C)
//       T_sub(i+1)  = T_base + (T_peak(i) - T_base) * phi_step
//
//     a geometric recursion with ratio phi_step < 1, so it converges to
//     T_base + phi*dT/(1 - phi) (246 °C for the defaults) instead of
//     running away. It is additionally clamped at melt_temperature: a run
//     that saturates there has left the model's validity range. That
//     happens when the simulation layer is thick compared with the
//     diffusion length over one dwell (phi -> 1) — e.g. 1 mm layers with
//     interlayer_time below ~1 s, or interlayer_time = 0, which asks for
//     deposition with no cooling at all and duly pins the whole built
//     region at the vaporisation cap. There is no radiation or convection
//     loss in the model to arrest it.
//
// (8) Normalised enthalpy (keyhole onset) — Hann, Iammi & Folkes,
//     J. Phys. D 44 (2011) 445401; King et al., "Observation of
//     keyhole-mode laser melting in laser powder-bed fusion additive
//     manufacturing", J. Mater. Process. Technol. 214 (2014) 2915-2925:
//
//       dH / h_s = A*P / (h_s * sqrt(pi * alpha * v * sigma^3))
//       h_s      = rho * c * T_melt[K]      (enthalpy at melting)
//
//     sigma is the laser beam radius; see kBeamRadiusM for why it is a
//     documented constant rather than an input. For the 316L defaults
//     dH/h_s = 13.5 (conduction mode); at 400 W and 0.5 m/s it is 34
//     (keyhole), matching the published transition at ~30.
//
// (9) Porosity risk — two competing mechanisms, reported as the larger:
//
//     Lack of fusion, Tang, Pistorius & Beuth, "Prediction of
//     lack-of-fusion porosity for powder bed fusion", Additive
//     Manufacturing 14 (2017) 39-48. With h = hatch_spacing,
//     t = process_layer_height, W and D from (2)/(3), the criterion for
//     fully fused material is
//
//       I_lof = (h/W)^2 + (t/D)^2 <= 1
//
//     sub-score = clamp01((I_lof - 1) / (4 - 1)): exactly zero while the
//     published criterion says "fully fused", rising past it and
//     saturating at I_lof = 4, which is twice the criterion in linear
//     dimensions (the index is quadratic in the ratios). The severe
//     anchor is an interpolation choice, not a measured threshold. With
//     the 316L defaults I_lof = 1.93 → 0.31: the default 200 W / 0.8 m/s
//     / 110 µm / 30 µm recipe sits just past the Tang boundary *for this
//     model*, because Rosenthal under-predicts depth (see "What this is
//     NOT").
//
//     Keyholing: sub-score = clamp01((dH/h_s - 30) / (60 - 30)). 30 is
//     King et al.'s measured conduction→keyhole transition; 60 is an
//     interpolation anchor for the fully developed keyhole/spatter
//     regime, not a measured threshold.
//
//     porosity_risk = max(lack-of-fusion, keyhole). The two mechanisms
//     sit at opposite ends of the process window, so the binding one is
//     the maximum rather than a sum.
//
// =====================================================================
// What this is NOT
// =====================================================================
//   * Not a transient finite-element thermal solve. There is no mesh
//     conduction, no latent heat of fusion, no temperature-dependent
//     conductivity/specific heat, no radiation or convection loss, no
//     powder-vs-solid conductivity contrast.
//   * No powder-scale physics: no discrete particles, no powder packing,
//     no denudation, no spatter, no balling, no oxide films.
//   * No vapour and no recoil pressure: the keyhole is scored by a
//     dimensionless group (8), never modelled. Rosenthal's point source
//     is singular at R = 0, which is why peak temperature is capped.
//   * No per-scan-vector path. The model knows hatch_spacing and
//     scan_speed, not the actual stripe/island/rotation scan strategy,
//     so it cannot see contour-vs-infill differences, vector-end
//     overheating, or scan-rotation anisotropy. Melt-pool geometry is
//     therefore a per-layer quantity; cells in the same layer differ only
//     through their local substrate temperature.
//   * Uniform, isotropic material properties everywhere; no multi-
//     material, no support structures, no gas-flow direction.
//   * Rosenthal systematically *under*-predicts melt-pool depth for LPBF
//     conduction mode (typically by 1.5-2x) because it neglects latent
//     heat, Marangoni convection, surface depression, and the higher
//     effective absorptivity of a powder bed. It therefore *over*-
//     predicts lack-of-fusion risk. Calibrate `absorptivity` against one
//     measured single-track cross-section before trusting the numbers:
//     0.35 is a flat-plate 316L value at 1070 nm; powder beds with
//     multiple scattering or a keyhole absorb 0.5-0.7.
//   * The interlayer decay (6) has no superposition between layers, so
//     the bulk of a tall part reads baseplate temperature rather than the
//     tens of kelvin above it that a real build accumulates.
//   * Not a qualification tool. Every threshold in (9) is a documented
//     literature heuristic, not a guarantee of part quality.
//
// =====================================================================
// Inputs (souxmar_value_t map) — capability contract §3.3 / §3.4
// =====================================================================
//   laser_power           : number, W,          default 200
//   scan_speed            : number, m/s,        default 0.8
//   hatch_spacing         : number, m,          default 1.1e-4
//   layer_height          : number, m,          default 1.0e-3
//                           the *simulation* layer = one field step
//   process_layer_height  : number, m,          default 3.0e-5
//                           the machine's powder layer (fusion criterion)
//   absorptivity          : number, -,          default 0.35
//   preheat_temperature   : number, °C,         default 80
//   baseplate_temperature : number, °C,         default 80
//   thermal_conductivity  : number, W/(m·K),    default 15.0
//   density               : number, kg/m^3,     default 7990
//   specific_heat         : number, J/(kg·K),   default 500
//   melt_temperature      : number, °C,         default 1400
//   interlayer_time       : number, s,          default 10.0
//   build_direction       : list of 3 numbers,  default [0, 0, 1]
//   baseplate_layers      : int,                default 0
//   num_layers            : int,                default 0 (0 → from mesh)
//
// Units are strict SI except temperatures, which are °C in every input
// and output per the house rule; absolute temperatures are converted to
// kelvin internally where the physics needs them (only in h_s, (8)).
//
// interlayer_time is the dwell between the *simulation* layers of this
// model — the field's steps. If one simulation layer lumps
// N = layer_height / process_layer_height machine layers, pass
// N × (machine recoat + scan time).
//
// Layer resolution (capability contract §2.2, implemented identically in
// every AM capability):
//   1. souxmar_mesh_cell_tag(mesh, c) >= 0 → that value *is* the layer.
//   2. otherwise bin the cell centroid's projection on build_direction by
//      layer_height, measured from the mesh bounding-box minimum along
//      that axis.
//   num_layers = max resolved index + 1, clamped down to the `num_layers`
//   input when that is > 0.
//
// A node's layer is the *minimum* layer of the cells that touch it, so a
// node on the interface between layers i and i+1 belongs to layer i — it
// is the top surface of layer i, which is what the laser melts. Layer 0
// therefore owns two node planes (its floor and its top); every other
// layer owns one. Nodes touched by no cell fall back to coordinate
// binning.
//
// =====================================================================
// Outputs
// =====================================================================
// solver.am.thermal.lpbf:
//   souxmar_field_new("temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR,
//                     num_nodes, num_layers)   [°C]
//   Step j is the state just after layer j has been scanned:
//     * nodes of layers < baseplate_layers  → baseplate_temperature
//     * nodes of layer j                    → Rosenthal mid-hatch peak
//     * nodes of layers below j             → decayed per (6)/(7)
//     * nodes of layers above j             → preheat_temperature (powder)
//
// postproc.am.melt_pool (consumes the nodal temperature field above):
//   souxmar_field_new("melt_pool", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR,
//                     num_cells, 1)
//   component [0] melt_pool_depth_m   [m]
//   component [1] normalised_enthalpy [-]  (dH/h_s)
//   component [2] porosity_risk       [0..1]
//   Cells in baseplate layers get all three components zero — they are
//   not printed material.
//   The local pre-scan substrate temperature T0 for a cell in layer j is
//   the coldest nodal temperature, at step j, among the cell's nodes that
//   belong to a layer below j (for the first printed layer, and for
//   meshes that do not share nodes between layers, baseplate_temperature
//   is used). It is clamped to at most 0.6 × melt_temperature.
//
// Determinism: every loop runs in index order, every solve has a fixed
// iteration count, the melt-pool geometry cache is a std::map keyed by
// T0 rounded to 1 K (the model is nowhere near 1 K accurate), and erf is
// a fixed rational approximation rather than the host libm's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/postproc.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

using Vec3 = std::array<double, 3>;

constexpr double kPi = 3.14159265358979323846;

// Absolute-zero offset [K] — the house rule keeps temperatures in °C at
// the ABI boundary, so conversions to kelvin are explicit and local.
constexpr double kKelvinOffset = 273.15;

// Laser beam radius [m]. The §3.3 key set has no beam-radius input, but
// the normalised-enthalpy scaling (8) required by §3.4 cannot be
// evaluated without one, so it is a documented machine constant: 4.0e-5 m
// is the 1/e^2 radius of the ~80 µm spot on a 400 W LPBF machine of the
// EOS M290 / SLM 280 class, which is the machine the §3.3 defaults
// describe. Reported as a contract gap rather than smuggled in as an
// extra YAML key.
constexpr double kBeamRadiusM = 4.0e-5;

// Ceiling on any reported temperature [°C]. Iron boils at 2861 °C at
// 1 atm (CRC Handbook, 97th ed.); Fe-Cr-Ni alloys are close, so 2900 °C
// stands in as the temperature above which this model's assumptions (no
// vapour, no recoil pressure, no keyhole) are void. Rosenthal's point
// source is singular at R = 0, so an uncapped peak is meaningless.
constexpr double kVaporisationCapC = 2900.0;

// Tang et al. (2017) lack-of-fusion index: (h/W)^2 + (t/D)^2 <= 1 is
// fully fused. The risk sub-score is zero up to that published boundary
// and ramps linearly to 1.0 at kLofSevere = 4.0 — the index is quadratic
// in the ratios, so 4.0 is twice the criterion in linear dimensions. The
// severe anchor is an interpolation choice, not a measured threshold.
constexpr double kLofCriterion = 1.0;
constexpr double kLofSevere    = 4.0;

// Normalised-enthalpy thresholds [-]. 30 is King et al. (2014)'s
// measured conduction→keyhole transition; 60 is an interpolation anchor
// for the fully developed keyhole regime, not a measured threshold.
constexpr double kKeyholeOnset  = 30.0;
constexpr double kKeyholeSevere = 60.0;

// Iteration counts for the closed-form root solves. Fixed, because the
// determinism gate forbids tolerance-based exits: 100 bisections take a
// double bracket well below its last bit.
constexpr int kBisectionSteps = 100;
constexpr int kTernarySteps   = 100;
constexpr int kIsothermScan   = 1024;

// Guards on allocation: a nodal field is num_nodes * num_layers doubles.
constexpr int         kMaxLayers       = 4096;
constexpr std::size_t kMaxFieldEntries = 20000000;  // 160 MB of doubles

// Largest cell node count in the element taxonomy (Hex27).
constexpr std::size_t kMaxCellNodes = 27;

// ---------------------------------------------------------------------
// Value-bag readers. read_number / read_int are copied verbatim from
// examples/plugins/modal-stub/modal_stub.cpp; read_vec3 follows
// examples/plugins/cfd-stub/cfd_stub.cpp.
// ---------------------------------------------------------------------

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

// Reads a 3-number list. Returns false (leaving *out untouched) when the
// key is absent or is not a list of exactly three numbers.
bool read_vec3(const souxmar_value_t* inputs, const char* key, Vec3* out) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return false;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return false;
  if (souxmar_value_list_size(v) != 3) return false;
  Vec3 tmp{};
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return false;
    tmp[i] = souxmar_value_as_number(c);
  }
  *out = tmp;
  return true;
}

// ---------------------------------------------------------------------
// Small numeric helpers
// ---------------------------------------------------------------------

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

// Abramowitz & Stegun 7.1.26 rational approximation to erf(x), x >= 0,
// |error| <= 1.5e-7. Used instead of std::erf so the answer cannot
// depend on the host libm's erf, which is not required to be identically
// rounded across Linux/macOS/Windows.
double erf_approx(double x) {
  if (!(x > 0.0)) return 0.0;  // NaN-safe; erf(0) = 0
  constexpr double p  = 0.3275911;
  constexpr double a1 = 0.254829592;
  constexpr double a2 = -0.284496736;
  constexpr double a3 = 1.421413741;
  constexpr double a4 = -1.453152027;
  constexpr double a5 = 1.061405429;
  const double t    = 1.0 / (1.0 + p * x);
  const double poly = t * (a1 + t * (a2 + t * (a3 + t * (a4 + t * a5))));
  return std::clamp(1.0 - poly * std::exp(-x * x), 0.0, 1.0);
}

// ---------------------------------------------------------------------
// Process inputs (§3.3). Every value is clamped to a documented range and
// nothing is ever divided by an unchecked input.
// ---------------------------------------------------------------------

struct Process {
  double laser_power          = 200.0;    // W
  double scan_speed           = 0.8;      // m/s
  double hatch_spacing        = 1.1e-4;   // m
  double layer_height         = 1.0e-3;   // m   (simulation layer / step)
  double process_layer_height = 3.0e-5;   // m   (machine powder layer)
  double absorptivity         = 0.35;     // -
  double preheat_c            = 80.0;     // °C
  double baseplate_c          = 80.0;     // °C
  double conductivity         = 15.0;     // W/(m·K)
  double density              = 7990.0;   // kg/m^3
  double specific_heat        = 500.0;    // J/(kg·K)
  double melt_c               = 1400.0;   // °C
  double interlayer_time      = 10.0;     // s
  Vec3   build_dir{0.0, 0.0, 1.0};        // unit vector
  int    baseplate_layers     = 0;
  int    num_layers_input     = 0;

  // Derived.
  double absorbed_power = 70.0;      // W       = absorptivity * laser_power
  double alpha          = 3.754e-6;  // m^2/s   = k / (rho * c)
};

// Error messages are string literals: souxmar_status_error only borrows
// the pointer, so it must have static storage duration.
souxmar_status_t read_process(const souxmar_value_t* inputs, Process* p) {
  // Ranges below bracket every LPBF machine and metal/polymer feedstock
  // we know of, with a wide margin; they exist to keep the closed forms
  // finite, not to police the user.
  p->laser_power   = std::clamp(read_number(inputs, "laser_power", 200.0), 1.0, 1.0e5);
  p->scan_speed    = std::clamp(read_number(inputs, "scan_speed", 0.8), 1.0e-4, 100.0);
  p->hatch_spacing = std::clamp(read_number(inputs, "hatch_spacing", 1.1e-4), 1.0e-6, 1.0e-2);
  p->layer_height  = std::clamp(read_number(inputs, "layer_height", 1.0e-3), 1.0e-6, 1.0);
  p->process_layer_height =
      std::clamp(read_number(inputs, "process_layer_height", 3.0e-5), 1.0e-7, 1.0e-2);
  p->absorptivity  = std::clamp(read_number(inputs, "absorptivity", 0.35), 0.01, 1.0);
  p->preheat_c     = std::clamp(read_number(inputs, "preheat_temperature", 80.0), -250.0, 3000.0);
  p->baseplate_c   = std::clamp(read_number(inputs, "baseplate_temperature", 80.0), -250.0, 3000.0);
  p->conductivity  = std::clamp(read_number(inputs, "thermal_conductivity", 15.0), 0.01, 500.0);
  p->density       = std::clamp(read_number(inputs, "density", 7990.0), 1.0, 25000.0);
  p->specific_heat = std::clamp(read_number(inputs, "specific_heat", 500.0), 1.0, 1.0e4);
  p->melt_c        = std::clamp(read_number(inputs, "melt_temperature", 1400.0), 50.0, 4000.0);
  p->interlayer_time =
      std::clamp(read_number(inputs, "interlayer_time", 10.0), 0.0, 1.0e6);
  p->baseplate_layers = std::clamp(read_int(inputs, "baseplate_layers", 0), 0, kMaxLayers);
  p->num_layers_input = std::clamp(read_int(inputs, "num_layers", 0), 0, kMaxLayers);

  // §2.1 — build_direction defaults to +Z, is normalised internally, and
  // a zero-length vector is a hard error.
  Vec3 b{0.0, 0.0, 1.0};
  (void) read_vec3(inputs, "build_direction", &b);
  const double len = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
  if (!(len > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }
  p->build_dir = {b[0] / len, b[1] / len, b[2] / len};

  // The Rosenthal superheat is (T_melt - T0); a melt temperature at or
  // below the bed/plate temperature would divide the model by zero.
  const double warm = std::max(p->preheat_c, p->baseplate_c);
  if (!(p->melt_c > warm + 1.0)) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "melt_temperature must exceed preheat_temperature and "
        "baseplate_temperature by at least 1 K");
  }

  p->absorbed_power = p->absorptivity * p->laser_power;
  p->alpha          = p->conductivity / (p->density * p->specific_heat);
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------
// Rosenthal moving point source (1946)
// ---------------------------------------------------------------------

struct Source {
  double q     = 70.0;      // absorbed power [W]
  double v     = 0.8;       // travel speed   [m/s]
  double k     = 15.0;      // conductivity   [W/(m·K)]
  double alpha = 3.754e-6;  // diffusivity    [m^2/s]
};

Source source_of(const Process& p) {
  Source s;
  s.q     = p.absorbed_power;
  s.v     = p.scan_speed;
  s.k     = p.conductivity;
  s.alpha = p.alpha;
  return s;
}

// Superheat above the far-field temperature at source-frame (w, y, z) [K].
// w > 0 is ahead of the source.
double rosenthal_dT(const Source& s, double w, double y, double z) {
  const double r = std::sqrt(w * w + y * y + z * z);
  if (!(r > 0.0)) return kVaporisationCapC;  // singular at the source
  return (s.q / (2.0 * kPi * s.k * r)) *
         std::exp(-s.v * (r + w) / (2.0 * s.alpha));
}

// R_c = q / (2*pi*k*dT_melt) [m] — the radius at which the unattenuated
// hemispherical term alone equals the melting superheat. It is the
// trailing tip of the melt pool and an upper bound on every melt-pool
// dimension, so it brackets both root solves below.
double isotherm_scale(const Source& s, double dT_melt) {
  return s.q / (2.0 * kPi * s.k * std::max(dT_melt, 1.0e-9));
}

// Melt-pool depth [m]: the T = T_melt isotherm straight down from the
// source. dT(0, 0, d) is strictly decreasing in d, so bisect (0, R_c].
double melt_pool_depth(const Source& s, double dT_melt) {
  const double rc = isotherm_scale(s, dT_melt);
  double lo = rc * 1.0e-9;  // dT(lo) ~ 1e9 * dT_melt, far above the root
  double hi = rc;           // dT(hi) = dT_melt * exp(-v*rc/2a) <= dT_melt
  for (int i = 0; i < kBisectionSteps; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (rosenthal_dT(s, 0.0, 0.0, mid) > dT_melt) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return 0.5 * (lo + hi);
}

// Half-width [m] of the melt pool at the free surface. Parametrising the
// z = 0 isotherm by R: w(R) = -R - b*ln(R/R_c) with b = 2*alpha/v, and
// y(R)^2 = R^2 - w(R)^2. y^2 is negative outside the pool, zero at both
// tips and unimodal in between.
double melt_pool_half_width(const Source& s, double dT_melt) {
  const double rc = isotherm_scale(s, dT_melt);
  const double b  = 2.0 * s.alpha / s.v;

  const auto y2 = [rc, b](double r) {
    const double w = -r - b * std::log(r / rc);
    return r * r - w * w;
  };

  // Uniform scan of (0, R_c]. The maximum is broad (it sits near
  // 0.4*R_c for the 316L defaults), so 1024 samples locate the bracket.
  int    best_i  = 0;
  double best_y2 = -1.0;
  for (int i = 0; i < kIsothermScan; ++i) {
    const double r = rc * static_cast<double>(i + 1) / static_cast<double>(kIsothermScan);
    const double v = y2(r);
    if (v > best_y2) {
      best_y2 = v;
      best_i  = i;
    }
  }
  if (!(best_y2 > 0.0)) return 0.0;  // degenerate: no surface isotherm

  const double step = rc / static_cast<double>(kIsothermScan);
  double lo = std::max(step * 1.0e-6, static_cast<double>(best_i) * step);
  double hi = std::min(rc, static_cast<double>(best_i + 2) * step);
  for (int i = 0; i < kTernarySteps; ++i) {
    const double third = (hi - lo) / 3.0;
    const double m1    = lo + third;
    const double m2    = hi - third;
    if (y2(m1) < y2(m2)) {
      lo = m1;
    } else {
      hi = m2;
    }
  }
  return std::sqrt(std::max(y2(0.5 * (lo + hi)), 0.0));
}

// Peak superheat [K] on the free surface at lateral offset y0 from the
// track centreline. dT/dw = 0 becomes, with u = w/R,
//   c*sqrt((1+u)/(1-u)) + u = 0,  c = v*y0/(2*alpha),
// which is strictly increasing on (-1, 0) — bisect for u, then evaluate.
double rosenthal_surface_peak_dT(const Source& s, double y0) {
  if (!(y0 > 0.0)) return kVaporisationCapC;  // on the centreline: singular
  const double c  = s.v * y0 / (2.0 * s.alpha);
  double       lo = -1.0 + 1.0e-15;  // f(lo) = -1 < 0
  double       hi = 0.0;             // f(hi) = +c > 0
  for (int i = 0; i < kBisectionSteps; ++i) {
    const double u = 0.5 * (lo + hi);
    const double f = c * std::sqrt((1.0 + u) / std::max(1.0 - u, 1.0e-300)) + u;
    if (f < 0.0) {
      lo = u;
    } else {
      hi = u;
    }
  }
  const double u = 0.5 * (lo + hi);
  const double r = y0 / std::sqrt(std::max(1.0 - u * u, 1.0e-300));
  return rosenthal_dT(s, u * r, y0, 0.0);
}

// Centreline cooling rate at solidification [K/s], magnitude. Falls out
// of the Rosenthal solution at the trailing tip w = -R = -R_c with a
// material point moving at dw/dt = -v:
//   |dT/dt| = 2*pi*k*v*dT_melt^2 / q
double rosenthal_cooling_rate(const Source& s, double dT_melt) {
  return 2.0 * kPi * s.k * s.v * dT_melt * dT_melt / std::max(s.q, 1.0e-12);
}

// Normalised enthalpy dH/h_s [-] (Hann et al. 2011; King et al. 2014):
//   dH/h_s = A*P / (rho*c*T_melt[K] * sqrt(pi*alpha*v*sigma^3))
// h_s is the enthalpy at melting, so T_melt enters in kelvin — this is
// the one place the °C house convention has to be converted.
double normalised_enthalpy(const Process& p) {
  const double h_s = p.density * p.specific_heat * (p.melt_c + kKelvinOffset);
  const double sigma3 = kBeamRadiusM * kBeamRadiusM * kBeamRadiusM;
  const double denom =
      h_s * std::sqrt(kPi * p.alpha * p.scan_speed * sigma3);
  if (!(denom > 0.0)) return 0.0;
  return p.absorbed_power / denom;
}

// Fraction of a layer's superheat still present after `t` seconds:
// exact 1-D slab-on-semi-infinite-solid solution with an insulated free
// surface (Carslaw & Jaeger §2.6), clamped away from 0 and 1 so the
// equivalent lumped time constant stays finite.
double retained_fraction(const Process& p, double t) {
  if (!(t > 0.0)) return 1.0;  // no dwell → no cooling between layers
  const double phi =
      erf_approx(p.layer_height / (2.0 * std::sqrt(p.alpha * t)));
  return std::clamp(phi, 1.0e-12, 1.0 - 1.0e-12);
}

// ---------------------------------------------------------------------
// Layer bookkeeping (§2.2)
// ---------------------------------------------------------------------

struct Layers {
  std::vector<int> cell_layer;  // per cell, in [0, num_layers)
  std::vector<int> node_layer;  // per node, in [0, num_layers)
  int              num_layers = 1;
};

// Resolves cell + node layer indices. `coords` is the flat 3*N node
// buffer. Never fails: a cell whose connectivity cannot be read falls
// back to layer 0, which is documented in the header.
Layers resolve_layers(const souxmar_mesh_t* mesh,
                      const double*         coords,
                      std::size_t           num_nodes,
                      std::size_t           num_cells,
                      const Process&        p) {
  Layers out;
  const Vec3&  b  = p.build_dir;
  const double lh = p.layer_height;

  // Bounding-box minimum along the build axis.
  double s_min = 0.0;
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double s = coords[i * 3 + 0] * b[0] + coords[i * 3 + 1] * b[1] +
                     coords[i * 3 + 2] * b[2];
    if (i == 0 || s < s_min) s_min = s;
  }
  const auto bin = [s_min, lh](double s) {
    const double q = (s - s_min) / lh;
    if (!(q > 0.0)) return 0;  // NaN-safe
    const double f = std::floor(q);
    if (f > static_cast<double>(kMaxLayers - 1)) return kMaxLayers - 1;
    return static_cast<int>(f);
  };
  const auto node_s = [coords, &b](std::size_t i) {
    return coords[i * 3 + 0] * b[0] + coords[i * 3 + 1] * b[1] +
           coords[i * 3 + 2] * b[2];
  };

  out.cell_layer.assign(num_cells, 0);
  out.node_layer.assign(num_nodes, -1);

  int max_layer = 0;
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
    std::array<std::uint64_t, kMaxCellNodes> ids{};
    const bool have_nodes =
        n > 0 && n <= kMaxCellNodes &&
        souxmar_mesh_cell_nodes(mesh, c, ids.data(), ids.size()).code == SOUXMAR_OK;

    int layer = 0;
    const std::int32_t tag = souxmar_mesh_cell_tag(mesh, c);
    if (tag >= 0) {
      // 1. An explicit cell tag *is* the layer index (mesher.am.layered).
      layer = std::min<int>(tag, kMaxLayers - 1);
    } else if (have_nodes) {
      // 2. Otherwise bin the centroid's projection on the build axis.
      double sum   = 0.0;
      int    count = 0;
      for (std::size_t j = 0; j < n; ++j) {
        if (ids[j] >= num_nodes) continue;
        sum += node_s(static_cast<std::size_t>(ids[j]));
        ++count;
      }
      if (count > 0) layer = bin(sum / static_cast<double>(count));
    }
    out.cell_layer[c] = layer;
    if (layer > max_layer) max_layer = layer;

    // A node belongs to the lowest layer that touches it: the top
    // surface of layer i, which is the surface the laser melts.
    if (have_nodes) {
      for (std::size_t j = 0; j < n; ++j) {
        if (ids[j] >= num_nodes) continue;
        int& nl = out.node_layer[static_cast<std::size_t>(ids[j])];
        if (nl < 0 || layer < nl) nl = layer;
      }
    }
  }

  // Nodes touched by no cell (and every node of a cell-less mesh) fall
  // back to coordinate binning.
  for (std::size_t i = 0; i < num_nodes; ++i) {
    if (out.node_layer[i] >= 0) continue;
    const int layer   = bin(node_s(i));
    out.node_layer[i] = layer;
    if (layer > max_layer) max_layer = layer;
  }

  out.num_layers = max_layer + 1;
  if (p.num_layers_input > 0) {
    out.num_layers = std::min(out.num_layers, p.num_layers_input);
  }
  out.num_layers = std::clamp(out.num_layers, 1, kMaxLayers);

  const int top = out.num_layers - 1;
  for (std::size_t c = 0; c < num_cells; ++c) {
    out.cell_layer[c] = std::clamp(out.cell_layer[c], 0, top);
  }
  for (std::size_t i = 0; i < num_nodes; ++i) {
    out.node_layer[i] = std::clamp(out.node_layer[i], 0, top);
  }
  return out;
}

// ---------------------------------------------------------------------
// solver.am.thermal.lpbf
// ---------------------------------------------------------------------

souxmar_status_t am_thermal_solve(const souxmar_mesh_t*           mesh,
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

  Process p;
  const souxmar_status_t ps = read_process(inputs, &p);
  if (ps.code != SOUXMAR_OK) return ps;

  std::size_t   flat_size = 0;
  const double* coords    = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  const Layers      layers    = resolve_layers(mesh, coords, num_nodes, num_cells, p);
  const std::size_t steps     = static_cast<std::size_t>(layers.num_layers);

  if (num_nodes > kMaxFieldEntries / steps) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "num_nodes * num_layers exceeds the 2e7-entry field budget; "
        "coarsen the mesh or cap the `num_layers` input");
  }

  // ---- Layer-wise temperature table -------------------------------
  const Source s        = source_of(p);
  const double dT_melt  = p.melt_c - p.baseplate_c;   // > 0, checked above
  const double dT_track = rosenthal_surface_peak_dT(s, 0.5 * p.hatch_spacing);
  const double cool     = rosenthal_cooling_rate(s, dT_melt);

  // Equivalent lumped time constant, floored by the conduction-limited
  // cooling rate under the source (see header (5)/(6)).
  const double phi_exact = retained_fraction(p, p.interlayer_time);
  double       tau       = (p.interlayer_time > 0.0)
                               ? p.interlayer_time / std::log(1.0 / phi_exact)
                               : 0.0;
  tau = std::max(tau, dT_track / std::max(cool, 1.0e-12));
  const double phi_step =
      (p.interlayer_time > 0.0 && tau > 0.0)
          ? std::clamp(std::exp(-p.interlayer_time / tau), 0.0, 1.0)
          : 1.0;

  // Layers [0, first_printed) are baseplate: pinned at
  // baseplate_temperature at every step.
  const int first_printed = std::min(p.baseplate_layers, layers.num_layers);

  std::vector<double> layer_peak(steps, p.preheat_c);
  double              t_sub = p.baseplate_c;  // substrate under layer `first_printed`
  for (int i = first_printed; i < layers.num_layers; ++i) {
    const double peak = std::min(t_sub + dT_track, kVaporisationCapC);
    layer_peak[static_cast<std::size_t>(i)] = peak;
    // Geometric recursion, ratio phi_step < 1 → converges. Clamped at
    // the melt temperature: saturation means the lumped interlayer model
    // has left its validity range, not that the part is molten.
    t_sub = std::min(p.baseplate_c + (peak - p.baseplate_c) * phi_step, p.melt_c);
  }

  // phi_step^n, accumulated as a running product in index order.
  std::vector<double> phi_pow(steps, 1.0);
  for (std::size_t n = 1; n < steps; ++n) {
    phi_pow[n] = phi_pow[n - 1] * phi_step;
  }

  souxmar_field_t* field = souxmar_field_new(
      "temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, num_nodes, steps);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * steps) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout: data[step * num_nodes + node_i] (components == 1).
  std::vector<double> layer_t(steps, p.preheat_c);
  for (std::size_t step = 0; step < steps; ++step) {
    const int j = static_cast<int>(step);
    for (int i = 0; i < layers.num_layers; ++i) {
      double t;
      if (i < first_printed) {
        t = p.baseplate_c;  // baseplate material
      } else if (i > j) {
        t = p.preheat_c;  // powder, not yet fused
      } else if (i == j) {
        t = layer_peak[static_cast<std::size_t>(i)];  // just scanned
      } else {
        const double dt = layer_peak[static_cast<std::size_t>(i)] - p.baseplate_c;
        t = p.baseplate_c + dt * phi_pow[static_cast<std::size_t>(j - i)];
      }
      layer_t[static_cast<std::size_t>(i)] =
          std::clamp(t, -kKelvinOffset, kVaporisationCapC);
    }
    double* dst = data + step * num_nodes;
    for (std::size_t n = 0; n < num_nodes; ++n) {
      dst[n] = layer_t[static_cast<std::size_t>(layers.node_layer[n])];
    }
  }

  *out_field = field;
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------
// postproc.am.melt_pool
// ---------------------------------------------------------------------

// Melt-pool geometry for one substrate temperature. Cached per T0 rounded
// to 1 K because the depth + width solves cost ~1200 evaluations each.
struct PoolGeometry {
  double depth      = 0.0;  // m
  double half_width = 0.0;  // m
};

souxmar_status_t melt_pool_compute(const souxmar_mesh_t*             mesh,
                                   const souxmar_field_t*            input_field,
                                   const souxmar_value_t*            inputs,
                                   const souxmar_postproc_options_t* /*options*/,
                                   souxmar_field_t**                 out_field,
                                   void*                             /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no cells");
  }
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }

  Process p;
  const souxmar_status_t ps = read_process(inputs, &p);
  if (ps.code != SOUXMAR_OK) return ps;

  std::size_t   flat_size = 0;
  const double* coords    = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  const Layers layers        = resolve_layers(mesh, coords, num_nodes, num_cells, p);
  const int    first_printed = std::min(p.baseplate_layers, layers.num_layers);

  // Upstream temperature field. §3.4 consumes `temperature` from §3.3
  // (nodal scalar, one step per layer), but stays composable: a cell-
  // located scalar field is read per cell, component 0 of a
  // vector/tensor field is used, a step index past the end of the field
  // is clamped, and a field whose count does not match the mesh is
  // ignored in favour of baseplate_temperature.
  const double* tdata = input_field ? souxmar_field_data_const(input_field) : nullptr;
  const std::size_t tcount = input_field ? souxmar_field_count(input_field) : 0;
  const std::size_t tcomps =
      input_field ? static_cast<std::size_t>(souxmar_field_components(input_field)) : 0;
  const std::size_t tsteps =
      input_field ? souxmar_field_num_time_steps(input_field) : 0;
  const std::uint8_t tloc =
      input_field ? souxmar_field_location(input_field) : SOUXMAR_FL_NODAL;
  const bool nodal_ok = tdata && tcomps > 0 && tsteps > 0 &&
                        tloc == SOUXMAR_FL_NODAL && tcount == num_nodes &&
                        souxmar_field_data_size(input_field) == tcount * tcomps * tsteps;
  const bool cell_ok = tdata && tcomps > 0 && tsteps > 0 &&
                       tloc == SOUXMAR_FL_CELL && tcount == num_cells &&
                       souxmar_field_data_size(input_field) == tcount * tcomps * tsteps;

  const auto sample = [&](std::size_t index, int step) {
    const std::size_t st = std::min(static_cast<std::size_t>(std::max(step, 0)),
                                    tsteps - 1);
    return tdata[st * tcount * tcomps + index * tcomps];
  };

  souxmar_field_t* out = souxmar_field_new("melt_pool", SOUXMAR_FL_CELL,
                                           SOUXMAR_FK_VECTOR, num_cells,
                                           /*num_time_steps=*/1);
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           out_data = souxmar_field_data(out);
  const std::size_t out_size = souxmar_field_data_size(out);
  if (!out_data || out_size != num_cells * 3) {
    souxmar_field_free(out);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  const Source s = source_of(p);

  // Normalised enthalpy is a process-parameter group: it has no spatial
  // dependence in this model, so the keyhole sub-score is the same for
  // every printed cell.
  const double dh_hs = normalised_enthalpy(p);
  const double keyhole =
      clamp01((dh_hs - kKeyholeOnset) / (kKeyholeSevere - kKeyholeOnset));

  // Cache key granularity [K]: 1 K, orders of magnitude finer than the
  // model's accuracy, and an integer key keeps std::map iteration and
  // lookup deterministic.
  std::map<std::int64_t, PoolGeometry> cache;

  for (std::size_t c = 0; c < num_cells; ++c) {
    const int layer = layers.cell_layer[c];
    if (layer < first_printed) {
      // Baseplate material: not printed, nothing to score.
      out_data[c * 3 + 0] = 0.0;
      out_data[c * 3 + 1] = 0.0;
      out_data[c * 3 + 2] = 0.0;
      continue;
    }

    // Local pre-scan substrate temperature under the laser.
    double t0 = p.baseplate_c;
    if (cell_ok) {
      t0 = sample(c, layer);
    } else if (nodal_ok) {
      const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
      std::array<std::uint64_t, kMaxCellNodes> ids{};
      if (n > 0 && n <= kMaxCellNodes &&
          souxmar_mesh_cell_nodes(mesh, c, ids.data(), ids.size()).code == SOUXMAR_OK) {
        bool found = false;
        for (std::size_t j = 0; j < n; ++j) {
          const std::uint64_t nid = ids[j];
          if (nid >= num_nodes) continue;
          if (layers.node_layer[static_cast<std::size_t>(nid)] >= layer) continue;
          const double tv = sample(static_cast<std::size_t>(nid), layer);
          if (!found || tv < t0) {
            t0    = tv;
            found = true;
          }
        }
      }
    }
    // Clamped: a substrate above 60% of the melt temperature is outside
    // the model's validity range (and would report an absurdly deep pool).
    t0 = std::clamp(t0, -kKelvinOffset, 0.6 * p.melt_c);

    const double dT_melt = std::max(p.melt_c - t0, 1.0);
    const std::int64_t key = static_cast<std::int64_t>(std::llround(t0));
    auto it = cache.find(key);
    if (it == cache.end()) {
      PoolGeometry g;
      g.depth      = melt_pool_depth(s, dT_melt);
      g.half_width = melt_pool_half_width(s, dT_melt);
      it           = cache.emplace(key, g).first;
    }
    const double depth = std::max(it->second.depth, 1.0e-12);
    const double width = std::max(2.0 * it->second.half_width, 1.0e-12);

    // Tang et al. (2017): (h/W)^2 + (t/D)^2 <= 1 is fully fused, with t
    // the machine powder-layer thickness (process_layer_height) — the
    // simulation layer_height is a bookkeeping bin, not a fusion depth.
    const double hw    = p.hatch_spacing / width;
    const double td    = p.process_layer_height / depth;
    const double i_lof = hw * hw + td * td;
    const double lof   = clamp01((i_lof - kLofCriterion) / (kLofSevere - kLofCriterion));

    out_data[c * 3 + 0] = it->second.depth;
    out_data[c * 3 + 1] = dh_hs;
    out_data[c * 3 + 2] = std::max(lof, keyhole);
  }

  *out_field = out;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kThermalVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &am_thermal_solve,
    nullptr,
};

constexpr souxmar_postproc_vtable_t kMeltPoolVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &melt_pool_compute,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t solver = souxmar_registry_add_solver(
      registry, "solver.am.thermal.lpbf", &kThermalVtable, /*user_data=*/nullptr);
  if (solver.code != SOUXMAR_OK) return 1;
  const souxmar_status_t postproc = souxmar_registry_add_postproc(
      registry, "postproc.am.melt_pool", &kMeltPoolVtable, /*user_data=*/nullptr);
  return postproc.code == SOUXMAR_OK ? 0 : 1;
}
