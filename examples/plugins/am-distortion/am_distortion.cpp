// SPDX-License-Identifier: Apache-2.0
//
// am-distortion — part-scale residual distortion and residual stress for
// layer-by-layer metal additive manufacturing.
//
// Registers two capabilities from one plugin:
//   * `solver.am.distortion.inherent_strain` → nodal vector
//     "distortion_displacement" (m), one time step per build layer
//   * `postproc.am.residual_stress`          → per-cell vector
//     "residual_stress"
//
// The pair is split the same way `am-polymer` splits thermal from bonding:
// the displacement field is the physical result of the mechanical model, the
// stress field is one interpretation of it (an elastic read-back of the
// locked-in strain) and can be re-run with a different yield strength without
// re-running the mechanical stage.
//
// ---------------------------------------------------------------------------
// What it computes — `solver.am.distortion.inherent_strain`
// ---------------------------------------------------------------------------
//
// The inherent-strain (eigenstrain) method: the entire thermo-mechanical
// history of one deposited layer is lumped into a single stress-free strain
// which is then applied mechanically, layer by layer, on the growing part.
//
//   Keller, N. & Ploshikhin, V., "New Method for Fast Predictions of Residual
//   Stress and Distortion of AM Parts", Proc. 25th Solid Freeform Fabrication
//   Symposium (2014), pp. 1229-1237.
//
// 1. Inherent strain (equibiaxial, in the plane normal to the build axis):
//
//        eps* = -cte · (T_melt - T_preheat) · strain_calibration      [-]
//
//    (T_melt - T_preheat) is a temperature *difference*, so the house °C unit
//    needs no offset conversion — a Celsius difference is already in kelvin,
//    and no absolute temperature enters this model anywhere.
//
//    eps* is NEGATIVE: material that solidifies from the melt and cools wants
//    to shrink. With the 316L defaults (cte 1.6e-5 1/K, T_melt 1400 °C,
//    T_preheat 80 °C, calibration 0.30) eps* = -6.34e-3, i.e. -0.63 %, which
//    sits in the 1e-3 … 1e-2 band reported for LPBF steels.
//
//    `strain_calibration` is the lumping factor: the fraction of the free
//    thermal contraction that survives as a permanent (plastically
//    accommodated) strain instead of relaxing away. IT MUST BE CALIBRATED
//    AGAINST A MEASURED PART, per machine AND per material — 0.30 is a
//    plausible starting value for 316L on an LPBF machine, nothing more. It
//    also has to be re-calibrated whenever `layer_height` changes, because
//    the model accumulates one curvature increment per simulation layer
//    (step 2), so halving the layer height roughly doubles the predicted bow.
//    Supply `inherent_strain` directly to bypass the derivation entirely
//    (that is the form a calibration campaign reports).
//
// 2. Curvature accumulation, layer by layer. Each newly deposited layer of
//    thickness h is a shrinking film bonded to everything already there
//    (thickness t_below). For a two-layer strip of a *single* material — the
//    AM case, deposit and substrate being the same alloy — force and moment
//    balance give the exact curvature (Timoshenko, "Analysis of Bi-Metal
//    Thermostats", J. Opt. Soc. Am. 11 (1925) 233; equal-modulus limit):
//
//        dkappa = 6 · (-eps*) · h · t_below / (h + t_below)^3        [1/m]
//
//    For h << t_below this is exactly Stoney's thin-film relation
//
//        kappa = 6 · sigma_f · t_f / (M_s · t_s^2)
//              = 6 · (-eps*) · t_f / t_s^2      (equal biaxial moduli)
//
//    Stoney, G.G., "The Tension of Metallic Films Deposited by Electrolysis",
//    Proc. R. Soc. Lond. A 82 (1909) 172-175. Because the biaxial modulus
//    M = E/(1-nu) appears in both film and substrate it cancels: E and nu do
//    not scale this curvature (they DO drive `postproc.am.residual_stress`
//    and the Poisson thickening in step 4).
//
//    kappa(j) = sum of dkappa over every deposited layer up to and including
//    j. Every increment is positive, so distortion GROWS MONOTONICALLY with
//    layer count; each increment carries 1/t_below^2, so distortion FALLS as
//    the substrate gets thicker (a measured 1/t_s^1.8 over t_s = 10 → 40 mm
//    with a 2 mm deposit, approaching 1/t_s^2 as the deposit thins). Summing
//    increments against the *current* composite is the standard treatment for
//    sequentially deposited layers — Townsend, Barnett & Brunner, "Elastic
//    relationships in layered composite media ...", J. Appl. Phys. 62 (1987)
//    4438 — and it deliberately gives a larger bow than applying one lumped
//    eigenstrain to the finished cross-section (0.88 vs 0.24 1/m for the
//    20-layer demo below): the deposit stiffens the part only against
//    *later* layers, and that asymmetry is the physical reason
//    layer-lumped models under-predict distortion.
//
//    Sign: positive kappa means the free edges rise — the classic AM result
//    that a plate built on a baseplate springs into a "smile" the moment it
//    is cut free, because the deposit on top is the side in tension.
//
// 3. Membrane (in-plane) shrinkage. The deposit's contraction is diluted over
//    the full bonded thickness by force balance:
//
//        eps_m(j) = eps* · t_dep(j) / (t_dep(j) + t_s)               [-]
//
//    eps_m is negative, so the membrane part of the in-plane displacement
//    points at the part's centroid axis: the part shrinks toward its own
//    centre line. The bending rotation of step 4 adds to that above the
//    neutral plane and opposes it below, exactly as plate bending does — on a
//    thick part with a large curvature the bottom face can therefore net
//    *outward* while the top face and the height-average are inward.
//
// 4. Displacement ansatz. With p the in-plane offset of a node from the
//    part's centroid axis, r = |p|, b the unit build direction, s the node's
//    height above the datum plane and s_top the deposit thickness above the
//    datum at this step:
//
//        u_inplane = (eps_m·phi - kappa·(s - s_n)) · p
//        u_axial   =  ½·kappa·phi·r²  +  eps_zz·s
//        eps_zz    = -2·nu/(1-nu) · eps_m        (plane-stress thickening:
//                                                 in-plane shrinkage makes the
//                                                 part grow along b)
//
//    Two branches, selected by `baseplate_clamped` and `baseplate_layers`:
//
//    * ANCHORED (baseplate_clamped and baseplate_layers > 0). The datum is
//      the top of the baseplate layers, phi = s/s_top and s_n = 0. Every
//      baseplate node has s = 0 and therefore reads EXACTLY (0,0,0) at every
//      step — it is the fixture datum the part is measured against. The bow
//      then has to develop through the deposit thickness rather than tilting
//      the datum, which is the price of demanding zero on a whole clamped
//      face: this is the fixture-datum frame, not a free-body frame.
//    * FREE (the default, since `baseplate_layers` defaults to 0). phi = 1
//      and s_n = ½·s_top: the whole part bows about its own mid-plane with a
//      uniform curvature, in-plane strain reverses sign across the mid-plane,
//      and the rigid-body modes are fixed by pinning the centroid axis. This
//      is the released shape you would measure after cutting the part off.
//
//    Layers above the current build front are "quiet" (inactive) elements in
//    the sense of the layer-activation FE literature: they carry no stiffness
//    and ride along rigidly with the front (s is capped at s_top), so the
//    field stays continuous instead of stepping to zero across the front.
//
//    The value at a node is a closed-form function of that node's own
//    coordinates, so the field is continuous by construction — no per-cell
//    quantity is ever smeared onto nodes.
//
// 5. Worked magnitude, for orientation. Demo box 60 x 20 x 20 mm, 20 layers
//    of 1 mm, `baseplate_layers` 0 (so the free branch), defaults elsewhere,
//    `substrate_thickness` left at 0 so it comes from the mesh (20 mm):
//    eps* = -6.336e-3, kappa = 0.882 1/m, eps_m = -3.17e-3, eps_zz = +2.46e-3.
//    The far corner (r = 31.6 mm) rises ½·0.882·0.0316² + 2.46e-3·0.02
//    = 441 + 49 = 490 µm and moves 379 µm inward; the centre axis stays at
//    zero, i.e. the plate is a "smile". Tens of microns to a millimetre is
//    the range measured on real LPBF coupons, which is the only claim being
//    made here — and a 20 mm thick block would in practice bow far less than
//    this, which is precisely what `strain_calibration` is for.
//
// What this is NOT: an FE solve. There is no stiffness matrix, no element
// formulation, no equilibrium iteration, no plasticity, no temperature field
// (feed `solver.am.thermal.lpbf` a mesh if you want one), no scan-vector or
// support-structure effect, no baseplate-cutting simulation, no distinction
// between in-plane directions (the inherent strain is equibiaxial), and no
// geometry sensitivity beyond the bounding box, the layer bins and each
// node's distance from the centroid axis. The displacement ansatz in step 4
// is kinematically admissible but does not satisfy equilibrium; it reproduces
// the shape and the trends, not the field. A part whose plan-form is not
// roughly plate-like (a thin-walled tube, a lattice) is outside the plate
// idealisation the Stoney relation rests on. Small strains and small
// rotations are assumed throughout: the implied peak bending strain is
// kappa·(thickness/2) — 0.9 % on the demo box of step 5, already at the edge
// of small-strain validity — and if the reported bow approaches the part's own
// size (which a very thin `substrate_thickness` will do) the answer is outside
// the model and should be discarded. A plugin has no channel to warn on, so
// this is stated here instead.
//
// Inputs (souxmar_value_t map; every key optional, unknown keys ignored):
//   youngs_modulus       : number, Pa,   default 1.9e11  (clamped [1e6, 1e13])
//   poisson_ratio        : number, -,    default 0.28    (clamped [-0.45, 0.49])
//   cte                  : number, 1/K,  default 1.6e-5  (clamped [0, 1e-3])
//   melt_temperature     : number, °C,   default 1400    (clamped [-273.15, 1e4])
//   preheat_temperature  : number, °C,   default 80      (clamped [-273.15, 1e4])
//   inherent_strain      : number, -,    default 0.0 ⇒ derived (see step 1);
//                                        non-zero values clamped [-0.05, 0.05]
//                                        and used AS GIVEN — negative shrinks
//   strain_calibration   : number, -,    default 0.30    (clamped [0, 2])
//   layer_height         : number, m,    default 0.001   (clamped [1e-6, 1])
//   substrate_thickness  : number, m,    default 0.0 ⇒ from the mesh
//                                        (clamped [1e-5, 10])
//   build_direction      : list of 3,    default [0, 0, 1]; normalised
//                                        internally, zero length rejected
//   baseplate_layers     : int,          default 0       (clamped [0, layers])
//   baseplate_clamped    : bool,         default true
//   num_layers           : int,          default 0 ⇒ from the mesh
//
// ---------------------------------------------------------------------------
// What it computes — `postproc.am.residual_stress`
// ---------------------------------------------------------------------------
//
// Reads the LAST time step of a nodal 3-component displacement field (the
// as-built state produced above) and turns it into a per-cell equivalent
// stress:
//
//   1. Least-squares displacement gradient over the cell's own nodes:
//        G = B·M^-1,  M = sum (x_a - x̄)⊗(x_a - x̄),
//                     B = sum (u_a - ū)⊗(x_a - x̄)
//      A Tikhonov ridge of 1e-9·tr(M)/3 is added to M so the 3x3 inverse also
//      exists for a flat cell (Tri3/Quad4 surface mesh) or a collinear one
//      (Edge2); for a degenerate direction it sets that gradient column to
//      ~0 instead of exploding. On a well-conditioned cell the ridge changes
//      the result by ~1e-9 relative.
//   2. Total small strain eps = ½(G + Gᵗ).
//   3. Locked-in elastic strain = total strain minus the eigenstrain that was
//      applied to that cell. This is the inherent-strain method's stress
//      recovery, sigma = C:(eps - eps*):
//        eps*_cell = eps* · (I - b⊗b)  for deposited cells (layer >= baseplate_layers)
//        eps*_cell = 0                 for baseplate cells (never molten)
//   4. Isotropic Hooke: sigma = 2·mu·eps_el + lambda·tr(eps_el)·I, then the
//      von Mises equivalent
//        sigma_vm = sqrt(½[(sxx-syy)²+(syy-szz)²+(szz-sxx)²] + 3[sxy²+syz²+szx²])
//
// Output components (per cell, 1 time step):
//   [0] sigma_vm_Pa         = min(sigma_vm, yield_strength). An ideal-plastic
//                             (radial-return) cap: no real part holds more
//                             than yield, and an inherent strain of 6.3e-3 is
//                             ~3x the elastic strain capacity of 316L
//                             (sigma_y/(E/(1-nu)) = 1.9e-3), so the raw
//                             elastic value goes past yield over much of the
//                             part. Capping keeps this component a stress.
//   [1] sigma_vm_over_yield = [0] / yield_strength, i.e. the utilisation of
//                             the REPORTED stress, so it is bounded by 1 and
//                             [0]/yield reproduces it exactly. A cell reading
//                             1.0 is at yield and has plastically relieved;
//                             measured as-built LPBF 316L sits at 0.4-0.9 of
//                             yield, so a part that reads 1.0 nearly
//                             everywhere is the model saying "this recipe
//                             locks in more strain than the alloy can carry
//                             elastically", which for the 316L defaults it
//                             does: eps* = 6.3e-3 against an elastic capacity
//                             sigma_y/(E/(1-nu)) = 1.9e-3.
//
//                             The RAW elastic overshoot (sigma_elastic/yield,
//                             which can reach 3 on a well-conditioned mesh) is
//                             deliberately NOT reported. It is not a stress
//                             and it is not mesh-convergent: differentiating
//                             the displacement ansatz across the artificial
//                             discontinuity at a clamped baseplate produced
//                             86 x yield (43 GPa) on the 2 mm demo mesh, an
//                             artefact of the kinematic clamp rather than a
//                             prediction. Reporting a bounded utilisation
//                             keeps every value in this field a quantity the
//                             model can actually defend.
//   [2] layer_index         = the cell's resolved build layer (§2.2), as a
//                             double, so a viewer can slice the stress map by
//                             layer without a second field.
//
// What this is NOT: a measurement, and not a stress equilibrium solution. It
// inherits every limitation of the displacement ansatz above, adds a purely
// elastic constitutive read-back with an after-the-fact yield cap (no
// hardening, no kinematic redistribution, no stress relief from HIP or heat
// treatment), and reports one averaged strain per cell, so it cannot resolve
// the near-surface stress gradient that hole-drilling or XRD actually
// measures. With `baseplate_clamped` the datum layers are held at exactly
// zero displacement and therefore report exactly zero stress — the real
// baseplate carries the reaction and is usually where the part cracks off;
// set `baseplate_clamped: false` to see a non-zero baseplate.
//
// Extra input beyond the §3.5 keys above (which are all re-read here, since
// the ABI gives a postproc no channel to the solver stage that ran before it):
//   yield_strength : number, Pa, default 5.0e8 (clamped [1e5, 1e11])
//
// Determinism: no RNG, no clock, no environment, no unordered container, no
// std::pow / exp / log anywhere (only +-*/ and sqrt, which IEEE-754 requires
// to be correctly rounded). Every loop and every accumulation runs in index
// order, so the output is byte-identical across Linux/macOS/Windows.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// Layer-count cap, shared with the rest of the manufacturing block:
// `mesher.am.layered` refuses to build past 200 000 cells, so 200 000 layers
// is already unreachable. The bound exists so a mesh whose cell tags are
// geometry-entity ids rather than layer indices cannot ask for a
// multi-terabyte field.
constexpr int kMaxLayers = 200000;

// Largest nodal history the solver will allocate, in doubles (~960 MB at 8
// bytes each — the field is num_nodes x 3 x num_layers and grows fast). The
// request is rejected deterministically rather than depending on how much
// memory the machine happens to have free.
constexpr std::size_t kMaxFieldEntries = 120u * 1000u * 1000u;

// Tikhonov ridge on the 3x3 nodal moment matrix, relative to its mean
// eigenvalue. Big enough to dominate double rounding noise (~1e-16), small
// enough to be invisible on a well-shaped cell.
constexpr double kMomentRidge = 1e-9;

// Nodes within this fraction of the build height of the datum plane count as
// datum nodes. Absorbs the rounding of a non-axis-aligned build direction; a
// nanometre for a 1 m part, i.e. far below the 1e-6 m minimum layer height
// and far above double rounding noise.
constexpr double kDatumTolerance = 1e-9;

// ---------------------------------------------------------------------------
// Value-bag helpers (read_number / read_int verbatim from modal-stub;
// read_clamped / read_vec3 match am-polymer so the whole block reads the same)
// ---------------------------------------------------------------------------

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

// Every physical input goes through this so the clamp range sits next to the
// default at the single point where the key is read. A non-finite value falls
// back to the default rather than clamping, because std::clamp propagates NaN
// and a NaN must never reach the output field.
double read_clamped(const souxmar_value_t* inputs, const char* key, double dv,
                    double lo, double hi) {
  const double v = read_number(inputs, key, dv);
  if (!std::isfinite(v)) return dv;
  return std::clamp(v, lo, hi);
}

bool read_bool(const souxmar_value_t* inputs, const char* key, bool dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v) return dv;
  if (souxmar_value_kind(v) == SOUXMAR_VK_BOOL) {
    return souxmar_value_as_bool(v) != 0;
  }
  // `1` / `0` in YAML is a Number, not a Bool — accept it, same leniency the
  // rest of the block applies to list-shaped inputs.
  if (souxmar_value_kind(v) == SOUXMAR_VK_NUMBER) {
    return souxmar_value_as_number(v) != 0.0;
  }
  return dv;
}

// §2.1 build direction. A key that is absent, or present but not a 3-element
// list of numbers, leaves `*out` untouched (same lenient handling as
// cfd-stub's `flow_direction`). A zero-length vector is rejected by the
// caller — it cannot be normalised.
void read_vec3(const souxmar_value_t* inputs, const char* key, Vec3* out) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return;
  if (souxmar_value_list_size(v) != 3) return;
  Vec3 parsed{};
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return;
    parsed[i] = souxmar_value_as_number(c);
  }
  *out = parsed;
}

// ---------------------------------------------------------------------------
// §3.5 input block, shared by both capabilities
// ---------------------------------------------------------------------------

struct InherentStrainInputs {
  double youngs_modulus_pa    = 1.9e11;   // Pa
  double poisson_ratio        = 0.28;     // -
  double inherent_strain      = 0.0;      // - (negative = shrinkage)
  double layer_height_m       = 0.001;    // m  (simulation layer, not the
                                          //     powder recoat thickness)
  double substrate_thickness_m = 0.0;     // m  (0 ⇒ from the mesh)
  Vec3   build_direction{0.0, 0.0, 1.0};  // normalised
  int    baseplate_layers     = 0;
  bool   baseplate_clamped    = true;
  int    requested_layers     = 0;        // 0 ⇒ from the mesh
};

// Derives the inherent strain per §3.5 and range-checks everything else.
// Returns an error only for a build direction that cannot be normalised.
souxmar_status_t read_inherent_strain_inputs(const souxmar_value_t* inputs,
                                            InherentStrainInputs*  out) {
  out->youngs_modulus_pa =
      read_clamped(inputs, "youngs_modulus", 1.9e11, 1.0e6, 1.0e13);
  // nu is held strictly below 0.5 because the Lamé constant
  // lambda = E·nu/((1+nu)(1-2nu)) diverges at incompressibility, and above
  // -0.45 because C stops being positive definite below -0.5 (auxetic
  // materials do exist, hence the negative half of the range).
  out->poisson_ratio =
      read_clamped(inputs, "poisson_ratio", 0.28, -0.45, 0.49);

  const double cte =
      read_clamped(inputs, "cte", 1.6e-5, 0.0, 1.0e-3);            // 1/K
  const double t_melt =
      read_clamped(inputs, "melt_temperature", 1400.0, -273.15, 1.0e4);   // °C
  const double t_preheat =
      read_clamped(inputs, "preheat_temperature", 80.0, -273.15, 1.0e4);  // °C
  const double calibration =
      read_clamped(inputs, "strain_calibration", 0.30, 0.0, 2.0);   // -
  // A Celsius difference is already a kelvin difference — no offset applies.
  // A non-positive difference means the melt is colder than the preheat,
  // which yields eps* = 0 and therefore an all-zero displacement field: a
  // valid degenerate answer, and a hint to check the two temperatures.
  const double delta_t = std::max(t_melt - t_preheat, 0.0);         // K

  const double supplied = read_number(inputs, "inherent_strain", 0.0);
  if (std::isfinite(supplied) && supplied != 0.0) {
    // Used with its sign as given: negative shrinks, positive expands. The
    // 5 % bound is a sanity limit, not physics — measured AM inherent strains
    // are 1e-3 … 1e-2.
    out->inherent_strain = std::clamp(supplied, -0.05, 0.05);
  } else {
    out->inherent_strain = -cte * delta_t * calibration;
  }

  out->layer_height_m =
      read_clamped(inputs, "layer_height", 0.001, 1.0e-6, 1.0);
  out->substrate_thickness_m =
      read_clamped(inputs, "substrate_thickness", 0.0, 0.0, 10.0);

  read_vec3(inputs, "build_direction", &out->build_direction);
  const Vec3& b = out->build_direction;
  const double len2 = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
  if (!std::isfinite(len2) || !(len2 > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length; it cannot be normalised");
  }
  const double inv_len = 1.0 / std::sqrt(len2);
  out->build_direction = {b[0] * inv_len, b[1] * inv_len, b[2] * inv_len};

  out->baseplate_layers =
      std::clamp(read_int(inputs, "baseplate_layers", 0), 0, kMaxLayers);
  out->baseplate_clamped = read_bool(inputs, "baseplate_clamped", true);
  out->requested_layers =
      std::clamp(read_int(inputs, "num_layers", 0), 0, kMaxLayers);
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------------
// §2.2 layer resolution + build geometry
// ---------------------------------------------------------------------------

struct BuildGeometry {
  std::vector<double> node_height;  // node projection on b, minus the bbox
                                   // minimum along b [m], index order
  std::vector<int>    cell_layer;   // resolved layer index per cell
  std::vector<double> layer_top;    // running max node height per layer [m]
  Vec3                centroid{0.0, 0.0, 0.0};  // node-average position [m]
  double              height = 0.0;             // total extent along b [m]
  int                 num_layers = 1;
};

// Bin a height above the bounding-box minimum into a layer index (§2.2 step 2).
int bin_layer(double height_above_min, double layer_height) {
  const double raw = std::floor(height_above_min / layer_height);
  if (!(raw > 0.0)) return 0;                                       // and NaN
  if (raw > static_cast<double>(kMaxLayers - 1)) return kMaxLayers;  // rejected
  return static_cast<int>(raw);
}

souxmar_status_t build_geometry(const souxmar_mesh_t*       mesh,
                               const InherentStrainInputs& in,
                               BuildGeometry*              out) {
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "mesh has no cells; the layer model needs elements");
  }

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }
  const Vec3& b = in.build_direction;

  // Pass 1 — node heights along b, bounding box along b, node-average
  // centroid. Accumulated in index order (determinism gate).
  out->node_height.assign(num_nodes, 0.0);
  double h_min = 0.0;
  double h_max = 0.0;
  Vec3   sum{0.0, 0.0, 0.0};
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double x = coords[i * 3 + 0];
    const double y = coords[i * 3 + 1];
    const double z = coords[i * 3 + 2];
    const double h = x * b[0] + y * b[1] + z * b[2];
    out->node_height[i] = h;
    if (i == 0 || h < h_min) h_min = h;
    if (i == 0 || h > h_max) h_max = h;
    sum[0] += x;
    sum[1] += y;
    sum[2] += z;
  }
  const double inv_n = 1.0 / static_cast<double>(num_nodes);
  out->centroid = {sum[0] * inv_n, sum[1] * inv_n, sum[2] * inv_n};
  out->height = h_max - h_min;
  for (std::size_t i = 0; i < num_nodes; ++i) out->node_height[i] -= h_min;

  // Pass 2 — per-cell layer index (§2.2) plus the cell's own top height.
  out->cell_layer.assign(num_cells, 0);
  std::vector<double>        cell_top(num_cells, 0.0);
  std::vector<std::uint64_t> cell_nodes;
  int max_layer = 0;
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t   node_count = souxmar_mesh_cell_node_count(mesh, c);
    const std::int32_t  tag        = souxmar_mesh_cell_tag(mesh, c);
    double              top        = 0.0;
    double              sum_height = 0.0;
    if (node_count > 0) {
      cell_nodes.assign(node_count, 0);
      const souxmar_status_t cs =
          souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count);
      if (cs.code != SOUXMAR_OK) return cs;
      for (std::size_t k = 0; k < node_count; ++k) {
        const std::uint64_t nid = cell_nodes[k];
        if (nid >= num_nodes) {
          return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                      "cell references out-of-range node id");
        }
        const double h = out->node_height[nid];
        sum_height += h;
        if (k == 0 || h > top) top = h;
      }
    }
    cell_top[c] = top;

    int layer = 0;
    if (tag >= 0) {
      // §2.2 step 1 — a non-negative cell tag *is* the layer index.
      if (tag > kMaxLayers - 1) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "cell tag exceeds the 200000-layer cap; these tags are not layer indices");
      }
      layer = static_cast<int>(tag);
    } else if (node_count > 0) {
      // §2.2 step 2 — bin the centroid's projection on the build axis.
      layer = bin_layer(sum_height / static_cast<double>(node_count),
                        in.layer_height_m);
      if (layer >= kMaxLayers) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "mesh spans more than 200000 layers at this layer_height");
      }
    }
    out->cell_layer[c] = layer;
    if (layer > max_layer) max_layer = layer;
  }

  int num_layers = max_layer + 1;
  if (in.requested_layers > 0) {
    num_layers = std::min(num_layers, in.requested_layers);
  }
  out->num_layers = std::clamp(num_layers, 1, kMaxLayers);

  // Pass 3 — clamp the layer indices into the reported range and fold each
  // cell's top height into its layer. The running maximum afterwards gives a
  // non-decreasing build-front height even when a layer bin holds no cells.
  out->layer_top.assign(static_cast<std::size_t>(out->num_layers), 0.0);
  for (std::size_t c = 0; c < num_cells; ++c) {
    const int layer = std::min(out->cell_layer[c], out->num_layers - 1);
    out->cell_layer[c] = layer;
    double& top = out->layer_top[static_cast<std::size_t>(layer)];
    if (cell_top[c] > top) top = cell_top[c];
  }
  for (std::size_t l = 1; l < out->layer_top.size(); ++l) {
    if (out->layer_top[l] < out->layer_top[l - 1]) {
      out->layer_top[l] = out->layer_top[l - 1];
    }
  }
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------------
// The mechanical model (pure functions — no ABI, no state)
// ---------------------------------------------------------------------------

// Curvature added by one deposited layer of thickness `layer` bonded onto a
// stack of thickness `below`, both of the same alloy (see step 2 of the
// header). Positive for a shrinking layer (eps* < 0). Bounded for every
// positive (layer, below) pair — it peaks at below = layer/2 and decays as
// 1/below^2, so a thin substrate cannot make it diverge.
double curvature_increment(double inherent_strain, double layer, double below) {
  const double denom = layer + below;
  if (!(denom > 0.0)) return 0.0;
  return 6.0 * (-inherent_strain) * layer * below / (denom * denom * denom);
}

// Membrane strain of the bonded stack: the deposit's shrinkage diluted over
// the whole thickness by force balance. Negative for eps* < 0.
double membrane_strain(double inherent_strain, double deposit, double substrate) {
  const double total = deposit + substrate;
  if (!(total > 0.0)) return 0.0;
  return inherent_strain * deposit / total;
}

// Per-step scalars of the model.
struct LayerState {
  std::vector<double> kappa;    // accumulated curvature [1/m]
  std::vector<double> eps_m;    // membrane in-plane strain [-]
  std::vector<double> eps_zz;   // through-thickness (Poisson) strain [-]
  std::vector<double> s_top;    // deposit thickness above the datum [m]
  double              datum = 0.0;      // datum plane height above bbox min [m]
  bool                anchored = false; // datum nodes are pinned to zero
};

LayerState layer_states(const InherentStrainInputs& in, const BuildGeometry& geo,
                        int baseplate_layers, double substrate_thickness) {
  const std::size_t steps = static_cast<std::size_t>(geo.num_layers);
  const std::size_t n_bp  = static_cast<std::size_t>(baseplate_layers);
  const double      h_l   = in.layer_height_m;
  const double      nu    = in.poisson_ratio;

  LayerState st;
  st.kappa.assign(steps, 0.0);
  st.eps_m.assign(steps, 0.0);
  st.eps_zz.assign(steps, 0.0);
  st.s_top.assign(steps, h_l);
  st.datum = (n_bp > 0) ? geo.layer_top[n_bp - 1] : 0.0;
  st.anchored = in.baseplate_clamped && n_bp > 0;

  double kappa = 0.0;
  for (std::size_t j = 0; j < steps; ++j) {
    if (j >= n_bp) {
      // Layers are deposited one simulation layer at a time onto everything
      // already there. Deposited thickness is counted in `layer_height`
      // units, not from the mesh, because the inherent strain belongs to a
      // simulation layer of that thickness; keep `layer_height` equal to the
      // mesh's actual layer thickness (mesher.am.layered uses target_size
      // for both) or the two notions drift apart.
      const double below = substrate_thickness +
                           static_cast<double>(j - n_bp) * h_l;
      kappa += curvature_increment(in.inherent_strain, h_l, below);
    }
    st.kappa[j] = kappa;

    const double deposit = (j >= n_bp)
                             ? static_cast<double>(j - n_bp + 1) * h_l
                             : 0.0;
    st.eps_m[j]  = membrane_strain(in.inherent_strain, deposit, substrate_thickness);
    st.eps_zz[j] = -2.0 * nu / (1.0 - nu) * st.eps_m[j];
    st.s_top[j]  = std::max(geo.layer_top[j] - st.datum, h_l);
  }
  return st;
}

// ---------------------------------------------------------------------------
// `solver.am.distortion.inherent_strain`
// ---------------------------------------------------------------------------

souxmar_status_t distortion_solve_impl(const souxmar_mesh_t*  mesh,
                                      const souxmar_value_t* inputs,
                                      souxmar_field_t**      out_field) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  InherentStrainInputs in;
  const souxmar_status_t rs = read_inherent_strain_inputs(inputs, &in);
  if (rs.code != SOUXMAR_OK) return rs;

  BuildGeometry geo;
  const souxmar_status_t gs = build_geometry(mesh, in, &geo);
  if (gs.code != SOUXMAR_OK) return gs;

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t steps     = static_cast<std::size_t>(geo.num_layers);
  if (num_nodes > kMaxFieldEntries / (3u * steps)) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "num_nodes x 3 x num_layers exceeds the 120M-double field budget; "
        "reduce num_layers or coarsen the mesh");
  }

  // baseplate_layers == num_layers is legal and means "nothing is deposited":
  // every step then reports zero. Clamping to num_layers rather than
  // num_layers - 1 keeps that answer instead of silently inventing a deposit.
  const int    n_bp = std::min(in.baseplate_layers, geo.num_layers);
  const double datum_from_baseplate =
      (n_bp > 0) ? geo.layer_top[static_cast<std::size_t>(n_bp) - 1] : 0.0;
  // §3.5 `substrate_thickness` 0 ⇒ from the mesh: the modelled baseplate when
  // there is one, otherwise the part's own build height (the deposit is its
  // own substrate). The latter is a documented stand-in — a real machine has
  // a baseplate that is not in the mesh, and it is the single most
  // influential number in this model (1/t_s^2), so set it explicitly. Note
  // that leaving it at 0 makes a taller mesh raise the substrate as well as
  // the layer count: hold it fixed when you want the layer-count trend alone.
  const double substrate =
      std::clamp(in.substrate_thickness_m > 0.0
                     ? in.substrate_thickness_m
                     : (n_bp > 0 ? datum_from_baseplate : geo.height),
                 1.0e-5, 10.0);

  const LayerState st = layer_states(in, geo, n_bp, substrate);

  souxmar_field_t* field =
      souxmar_field_new("distortion_displacement", SOUXMAR_FL_NODAL,
                        SOUXMAR_FK_VECTOR, num_nodes, steps);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 3 * steps) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  const Vec3&  b         = in.build_direction;
  const Vec3&  centre    = geo.centroid;
  const double datum_tol = kDatumTolerance * std::max(geo.height, 1.0e-9);

  // Layout: data[step*num_nodes*3 + node*3 + component], same step-major
  // convention as heat-solver and modal-stub.
  for (std::size_t j = 0; j < steps; ++j) {
    const double kappa  = st.kappa[j];
    const double eps_m  = st.eps_m[j];
    const double eps_zz = st.eps_zz[j];
    const double s_top  = st.s_top[j];
    const double s_n    = st.anchored ? 0.0 : 0.5 * s_top;
    const std::size_t step_off = j * num_nodes * 3;
    for (std::size_t i = 0; i < num_nodes; ++i) {
      double s = geo.node_height[i] - st.datum;
      if (s <= datum_tol) s = 0.0;
      if (st.anchored && s == 0.0) {
        // Clamped datum node: exactly zero at every step, by construction and
        // not by rounding.
        data[step_off + i * 3 + 0] = 0.0;
        data[step_off + i * 3 + 1] = 0.0;
        data[step_off + i * 3 + 2] = 0.0;
        continue;
      }
      // Quiet-element cap: material above the build front rides along rigidly.
      const double sigma = std::min(s, s_top);
      const double phi   = st.anchored ? sigma / s_top : 1.0;

      // In-plane offset from the centroid axis.
      const double dx = coords[i * 3 + 0] - centre[0];
      const double dy = coords[i * 3 + 1] - centre[1];
      const double dz = coords[i * 3 + 2] - centre[2];
      const double along = dx * b[0] + dy * b[1] + dz * b[2];
      const double px = dx - along * b[0];
      const double py = dy - along * b[1];
      const double pz = dz - along * b[2];
      const double r2 = px * px + py * py + pz * pz;

      const double eps_ip = eps_m * phi - kappa * (sigma - s_n);
      const double u_axial = 0.5 * kappa * phi * r2 + eps_zz * sigma;
      data[step_off + i * 3 + 0] = eps_ip * px + u_axial * b[0];
      data[step_off + i * 3 + 1] = eps_ip * py + u_axial * b[1];
      data[step_off + i * 3 + 2] = eps_ip * pz + u_axial * b[2];
    }
  }

  *out_field = field;
  return souxmar_status_ok();
}

souxmar_status_t distortion_solve(const souxmar_mesh_t*           mesh,
                                 const souxmar_value_t*          inputs,
                                 const souxmar_solver_options_t* /*options*/,
                                 souxmar_field_t**               out_field,
                                 void*                           /*user_data*/) {
  // Nothing below throws by design, but the per-layer and per-node vectors are
  // sized from user input; a bad_alloc must become a status, not an exception
  // crossing the C ABI.
  try {
    return distortion_solve_impl(mesh, inputs, out_field);
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "allocation failed while building the distortion history");
  }
}

// ---------------------------------------------------------------------------
// `postproc.am.residual_stress`
// ---------------------------------------------------------------------------

// Cramer 3x3 inverse of a row-major matrix. Returns the determinant; `out` is
// only meaningful when it is non-zero. Fixed operation order, so the result is
// bit-identical on every platform.
double invert3(const double a[9], double out[9]) {
  const double c0 = a[4] * a[8] - a[5] * a[7];
  const double c1 = a[5] * a[6] - a[3] * a[8];
  const double c2 = a[3] * a[7] - a[4] * a[6];
  const double det = a[0] * c0 + a[1] * c1 + a[2] * c2;
  if (!std::isfinite(det) || det == 0.0) return 0.0;
  const double inv = 1.0 / det;
  out[0] = c0 * inv;
  out[3] = c1 * inv;
  out[6] = c2 * inv;
  out[1] = (a[2] * a[7] - a[1] * a[8]) * inv;
  out[4] = (a[0] * a[8] - a[2] * a[6]) * inv;
  out[7] = (a[1] * a[6] - a[0] * a[7]) * inv;
  out[2] = (a[1] * a[5] - a[2] * a[4]) * inv;
  out[5] = (a[2] * a[3] - a[0] * a[5]) * inv;
  out[8] = (a[0] * a[4] - a[1] * a[3]) * inv;
  return det;
}

// von Mises equivalent of a symmetric stress tensor in row-major storage.
double von_mises(const double s[9]) {
  const double d0 = s[0] - s[4];
  const double d1 = s[4] - s[8];
  const double d2 = s[8] - s[0];
  const double shear = s[1] * s[1] + s[5] * s[5] + s[2] * s[2];
  return std::sqrt(0.5 * (d0 * d0 + d1 * d1 + d2 * d2) + 3.0 * shear);
}

souxmar_status_t residual_stress_compute_impl(const souxmar_mesh_t*  mesh,
                                             const souxmar_field_t* input_field,
                                             const souxmar_value_t* inputs,
                                             souxmar_field_t**      out_field) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!input_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "input field is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);

  // Any nodal 3-component displacement field works — the name is documented
  // as "distortion_displacement" but an elastic-solver displacement field is
  // just as valid an input, so the shape is checked and the name is not.
  if (souxmar_field_location(input_field) != SOUXMAR_FL_NODAL ||
      souxmar_field_components(input_field) != 3) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "postproc.am.residual_stress needs a nodal 3-component displacement field "
        "(distortion_displacement)");
  }
  if (souxmar_field_count(input_field) != num_nodes) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "displacement field length does not match the mesh node count");
  }
  const std::size_t in_steps = souxmar_field_num_time_steps(input_field);
  const double*     in_data  = souxmar_field_data_const(input_field);
  if (in_steps == 0 || !in_data ||
      souxmar_field_data_size(input_field) != num_nodes * 3 * in_steps) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "displacement field buffer size mismatch");
  }
  // The as-built state is the last step: every layer has been deposited.
  const std::size_t step_off = (in_steps - 1) * num_nodes * 3;

  InherentStrainInputs in;
  const souxmar_status_t rs = read_inherent_strain_inputs(inputs, &in);
  if (rs.code != SOUXMAR_OK) return rs;
  const double yield_pa =
      read_clamped(inputs, "yield_strength", 5.0e8, 1.0e5, 1.0e11);  // Pa

  BuildGeometry geo;
  const souxmar_status_t gs = build_geometry(mesh, in, &geo);
  if (gs.code != SOUXMAR_OK) return gs;
  const int n_bp = std::min(in.baseplate_layers, geo.num_layers);

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  // Lamé constants — nu is clamped below 0.5 upstream so (1 - 2nu) > 0.
  const double e_mod = in.youngs_modulus_pa;
  const double nu    = in.poisson_ratio;
  const double mu    = e_mod / (2.0 * (1.0 + nu));
  const double lame  = e_mod * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
  const Vec3&  b     = in.build_direction;

  souxmar_field_t* out = souxmar_field_new("residual_stress", SOUXMAR_FL_CELL,
                                          SOUXMAR_FK_VECTOR, num_cells,
                                          /*num_time_steps=*/1);
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double* out_data = souxmar_field_data(out);
  if (!out_data || souxmar_field_data_size(out) != num_cells * 3) {
    souxmar_field_free(out);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  std::vector<std::uint64_t> cell_nodes;
  for (std::size_t c = 0; c < num_cells; ++c) {
    const int         layer      = geo.cell_layer[c];
    const std::size_t node_count = souxmar_mesh_cell_node_count(mesh, c);

    // Least-squares displacement gradient over the cell's own nodes.
    double moment[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double mixed[9]  = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double grad[9]   = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (node_count > 1) {
      cell_nodes.assign(node_count, 0);
      const souxmar_status_t cs =
          souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count);
      if (cs.code != SOUXMAR_OK) {
        souxmar_field_free(out);
        return cs;
      }
      Vec3 x_bar{0.0, 0.0, 0.0};
      Vec3 u_bar{0.0, 0.0, 0.0};
      for (std::size_t k = 0; k < node_count; ++k) {
        const std::uint64_t nid = cell_nodes[k];
        if (nid >= num_nodes) {
          souxmar_field_free(out);
          return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                      "cell references out-of-range node id");
        }
        for (std::size_t d = 0; d < 3; ++d) {
          x_bar[d] += coords[nid * 3 + d];
          u_bar[d] += in_data[step_off + nid * 3 + d];
        }
      }
      const double inv_k = 1.0 / static_cast<double>(node_count);
      for (std::size_t d = 0; d < 3; ++d) {
        x_bar[d] *= inv_k;
        u_bar[d] *= inv_k;
      }
      for (std::size_t k = 0; k < node_count; ++k) {
        const std::uint64_t nid = cell_nodes[k];
        const double dx[3] = {coords[nid * 3 + 0] - x_bar[0],
                              coords[nid * 3 + 1] - x_bar[1],
                              coords[nid * 3 + 2] - x_bar[2]};
        const double du[3] = {in_data[step_off + nid * 3 + 0] - u_bar[0],
                              in_data[step_off + nid * 3 + 1] - u_bar[1],
                              in_data[step_off + nid * 3 + 2] - u_bar[2]};
        for (std::size_t r = 0; r < 3; ++r) {
          for (std::size_t q = 0; q < 3; ++q) {
            moment[r * 3 + q] += dx[r] * dx[q];
            mixed[r * 3 + q]  += du[r] * dx[q];
          }
        }
      }
      // Ridge, then solve grad = mixed · moment^-1. A zero trace means every
      // node sits at the same point (a Vertex cell, or a fully collapsed
      // one): the gradient stays zero, which reports the fully constrained
      // stress C:(-eps*) — the honest answer when the cell carries no
      // geometric information at all.
      const double trace = moment[0] + moment[4] + moment[8];
      if (trace > 0.0) {
        const double ridge = kMomentRidge * trace / 3.0;
        moment[0] += ridge;
        moment[4] += ridge;
        moment[8] += ridge;
        double inv_moment[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        if (invert3(moment, inv_moment) != 0.0) {
          for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t q = 0; q < 3; ++q) {
              double acc = 0.0;
              for (std::size_t p = 0; p < 3; ++p) {
                acc += mixed[r * 3 + p] * inv_moment[p * 3 + q];
              }
              grad[r * 3 + q] = acc;
            }
          }
        }
      }
    }

    // Total small strain, minus the eigenstrain that was applied to this cell.
    double strain[9];
    for (std::size_t r = 0; r < 3; ++r) {
      for (std::size_t q = 0; q < 3; ++q) {
        strain[r * 3 + q] = 0.5 * (grad[r * 3 + q] + grad[q * 3 + r]);
      }
    }
    if (layer >= n_bp) {
      // Equibiaxial in the plane normal to b: eps* · (I - b⊗b).
      for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t q = 0; q < 3; ++q) {
          const double proj = (r == q ? 1.0 : 0.0) - b[r] * b[q];
          strain[r * 3 + q] -= in.inherent_strain * proj;
        }
      }
    }

    const double trace_e = strain[0] + strain[4] + strain[8];
    double stress[9];
    for (std::size_t r = 0; r < 3; ++r) {
      for (std::size_t q = 0; q < 3; ++q) {
        stress[r * 3 + q] = 2.0 * mu * strain[r * 3 + q] +
                            (r == q ? lame * trace_e : 0.0);
      }
    }
    const double vm = von_mises(stress);
    // Ideal-plastic (radial-return) cap: no real part holds more than yield.
    // Component [1] is the ratio OF THE REPORTED STRESS, so it stays in
    // [0, 1] and [0] / yield_strength reproduces it exactly — see the header
    // comment for why the raw elastic overshoot is deliberately not reported.
    const double vm_capped = std::min(vm, yield_pa);
    out_data[c * 3 + 0] = vm_capped;
    out_data[c * 3 + 1] = vm_capped / yield_pa;
    out_data[c * 3 + 2] = static_cast<double>(layer);
  }

  *out_field = out;
  return souxmar_status_ok();
}

souxmar_status_t residual_stress_compute(
    const souxmar_mesh_t*             mesh,
    const souxmar_field_t*            input_field,
    const souxmar_value_t*            inputs,
    const souxmar_postproc_options_t* /*options*/,
    souxmar_field_t**                 out_field,
    void*                             /*user_data*/) {
  try {
    return residual_stress_compute_impl(mesh, input_field, inputs, out_field);
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "allocation failed while recovering residual stress");
  }
}

constexpr souxmar_solver_vtable_t kDistortionVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &distortion_solve,
    nullptr,
};

constexpr souxmar_postproc_vtable_t kResidualStressVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &residual_stress_compute,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t solver_status = souxmar_registry_add_solver(
      registry, "solver.am.distortion.inherent_strain", &kDistortionVtable,
      /*user_data=*/nullptr);
  if (solver_status.code != SOUXMAR_OK) return 1;
  const souxmar_status_t postproc_status = souxmar_registry_add_postproc(
      registry, "postproc.am.residual_stress", &kResidualStressVtable,
      /*user_data=*/nullptr);
  if (postproc_status.code != SOUXMAR_OK) return 1;
  return 0;
}
