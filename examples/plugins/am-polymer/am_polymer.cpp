// SPDX-License-Identifier: Apache-2.0
//
// am-polymer — polymer additive manufacturing: FFF/FDM interlayer thermal
// cycling and the interlayer bond it produces.
//
// Registers two capabilities from one plugin:
//   * `solver.am.polymer.fff`     → nodal scalar "interface_temperature" (°C)
//   * `postproc.am.bond_strength` → per-cell vector "bond_quality"
//
// The pair exists because in fused-filament fabrication the part is a stack
// of welded roads, and the Z-direction strength is set by the *thermal
// history at the road interface*, not by the bulk polymer. Large-format
// marine polymer AM (PEKK / PA12-CF fairings, see
// examples/am-polymer-auv-fairing) lives or dies on that weld, so the
// thermal stage and the healing stage are deliberately separate: the
// temperature history is the physical result, the healing law is one
// interpretation of it and can be swapped without re-running the thermal
// stage.
//
// ---------------------------------------------------------------------------
// What it computes — `solver.am.polymer.fff`
// ---------------------------------------------------------------------------
//
// Lumped-capacitance (Newtonian) cooling of a deposited road, evaluated once
// per layer.
//
//   1. Characteristic length of one road, from the road cross-section. Per
//      unit road length the volume is h·w and the *convecting* area is
//      w + 2h — the bottom face is welded to the layer below, not exposed:
//
//          L_c = (h·w) / (w + 2h)                       [m]
//
//   2. Lumped time constant (Incropera & DeWitt, "Fundamentals of Heat and
//      Mass Transfer", lumped-capacitance method):
//
//          tau = rho·c·L_c / h_conv                     [s]
//
//      With the defaults (PEKK-like: rho 1010, c 1800, k 0.25, h_conv 30,
//      h 0.2 mm, w 0.4 mm) L_c = 1.0e-4 m, tau = 6.06 s, and the Biot number
//      Bi = h_conv·L_c/k = 0.012 << 0.1, i.e. the lumped assumption is
//      admissible for the whole default parameter block. It stops being
//      admissible for a thick large-format bead — Bi = 0.4 at w = h = 10 mm,
//      four times past the usual limit, where the real bead has a thermal
//      gradient through it that this model cannot see. The plugin does not
//      enforce the Biot criterion; it is stated here.
//
//   3. Environment temperature each layer relaxes toward. The bed is a hot
//      boundary whose influence dies out with height. The decay length is the
//      steady conduction-vs-convection (fin / extended-surface) length of the
//      road stack, which is the conduction penetration depth over one cooling
//      time constant:
//
//          z_bed    = sqrt(alpha·tau) = sqrt(k·L_c/h_conv),  alpha = k/(rho·c)
//          T_env(j) = T_chamber + (T_bed − T_chamber)·exp(−z_j / z_bed)
//          z_j      = (j + 0.5)·layer_height            [m above the bed]
//
//      With the defaults z_bed = 0.91 mm, so the bed still leads over the
//      first ~5 layers and the chamber owns everything above ~3 mm. Note what is
//      deliberately *not* in z_bed: `layer_time`. Scaling the bed's reach with
//      the layer interval would make a slower build look better bonded low
//      down, which would fight the (correct) cooling penalty and leave the
//      model non-monotone in layer_time. The fin length is the steady answer
//      and carries no time.
//
//   4. Interface temperature when the next road lands. A fresh road at
//      T_nozzle meets a substrate at T_sub; the interface starts at the mean
//      of the two (equal thermal properties on both sides — the classic
//      contacting-semi-infinite-bodies result reduces to the arithmetic mean
//      when rho·c·k matches, which it does for polymer-on-polymer):
//
//          T_iface(j) = ½·(T_nozzle + T_sub(j−1))
//          T_sub(j)   = T_env(j) + (T_iface(j) − T_env(j))·exp(−t_layer/tau)
//
//      Layer 0 (or the first layer above `baseplate_layers`) lands on the
//      bed, so its substrate is T_bed.
//
//   5. Output. Step `j` is the state just after layer `j` is deposited
//      (same convention as `solver.am.thermal.lpbf`):
//
//          node in layer L == j : T_iface(L)
//          node in layer L <  j : T_env(L) + (T_iface(L) − T_env(L))
//                                 · exp(−(j − L)·t_layer/tau)
//          node in layer L >  j : chamber_temperature (not yet an interface)
//          node in a baseplate/raft layer : bed_temperature at every step
//
//      A node's layer is the *highest* layer index among the cells that
//      touch it — i.e. the layer whose bottom interface the node plane
//      lies on, which is the weld that plane actually is. The node plane
//      at the very top of the part has no layer above it and therefore
//      shares the top layer's history with the plane below it.
//
// References: Sun, Rizvi, Bellehumeur & Gu, "Effect of processing conditions
// on the bonding quality of FDM polymer filaments", Rapid Prototyping J. 14
// (2008) 72 — the FDM bond-formation study this cooling-and-interface
// construction follows, and the source of the neck-growth geometry used
// downstream; Yang & Pitchumani, "Healing of thermoplastic polymers at an
// interface under nonisothermal conditions", Macromolecules 35 (2002) 3213 —
// the healing law applied by `postproc.am.bond_strength` below.
//
// ---------------------------------------------------------------------------
// What it computes — `postproc.am.bond_strength`
// ---------------------------------------------------------------------------
//
// Yang & Pitchumani reptation healing, in its non-isothermal ("reduced
// weld time") form. Isothermally the interfacial strength grows as
// (t/t_rep)^¼ until it saturates at the bulk strength; non-isothermally the
// time is reduced by the local reptation time before the exponent is applied:
//
//     t_rep(T) = t_rep_ref · exp( (E_a/R)·(1/T − 1/T_ref) )     [s, T in K]
//     xi       = ∫_{T > Tg} dt / t_rep(T(t))                    [-]
//     D_h      = min(1, xi^¼)                                   [-]
//
// Only time above the glass transition counts — below Tg the chains are
// frozen and no reptation occurs. The Z-direction strength fraction adds the
// geometric penalty that healing cannot fix, the inter-bead void left between
// necked roads:
//
//     z_strength_fraction = 0.85 · D_h
//
// The temperature history comes from the input field, sampled once per layer
// with spacing `layer_time`, and the reduced-time integral is evaluated with a
// fixed number of midpoint sub-samples per interval.
//
// Between two samples the trace is reconstructed as the relaxation it
// physically is, not as a straight line. §3.8 gives the postproc no thermal
// properties, so tau cannot be recomputed here — but it can be *read off the
// samples*, because a relaxation toward a floor is fixed by its endpoints:
//
//     T_floor  = min of the remaining history          (the floor it decays to)
//     r        = (T[s+1] − T_floor) / (T[s] − T_floor) (decay over one interval)
//     T(f)     = T_floor + (T[s] − T_floor)·r^f,  f in [0, 1]
//
// so the Tg crossing inside the interval is f* = ln((Tg − T_floor)/(T[s] −
// T_floor)) / ln r. On this plugin's own output that recovers the analytic
// tau·ln((T_iface − T_env)/(Tg − T_env)) to well under a percent. A straight
// line instead would overstate the weld time by roughly
// (t_layer/tau)/(1 − exp(−t_layer/tau)) — a factor of ten at the default
// 20 s layer time — and would make a *slower* build look better welded, which
// is backwards.
//
// Two further reduction rules make that history per-cell and physical:
//
//   * A cell is bounded by two weld planes whose histories are one step out
//     of phase, so averaging its nodes would flatten exactly the peak the
//     healing law is exponentially sensitive to. Instead each cell takes the
//     single hottest of its nodes' histories (ties → lowest node index) —
//     the best-welded interface bounding that cell — and keeps its decay
//     shape intact.
//   * Only cooling intervals (T[s+1] <= T[s]) contribute. A rising interval
//     is the sampling artefact of a road that appears at its deposition
//     temperature *between* two recorded steps; counting it would invent
//     pre-heat time the process does not have.
//
// Output components (per §2.4 / the `postproc.mesh_quality` precedent):
//     [0] degree_of_healing   (0..1)
//     [1] z_strength_fraction (0..1)
//     [2] seconds_above_tg    (s)
//
// ---------------------------------------------------------------------------
// What this is NOT
// ---------------------------------------------------------------------------
//
//   * Not a thermal FE solution. There is no conduction between roads, no
//     in-layer scan path, no radiation, no temperature-dependent properties,
//     and no latent heat — so for the semi-crystalline polymers this plugin
//     is aimed at (PEKK, PA12-CF) the crystallisation plateau near Tc is
//     missing and the model cools too fast through it.
//   * A buried road keeps decaying with the same *convective* time constant
//     as an exposed one, so the interior of a thick part is over-cooled. A
//     real model switches to conduction into the surrounding solid once a
//     road is covered.
//   * One temperature per layer. Everything in-plane — corners, sparse
//     infill, perimeter-vs-infill dwell, the difference between a 20 mm and a
//     2 m long road at the same "layer time" — is collapsed into
//     `layer_time`. A node's history depends only on its layer index.
//   * "interface_temperature" is an interface field, not a material-
//     temperature field. A node plane reads `chamber_temperature` until the
//     layer above it lands, so the top surface of the freshly deposited layer
//     reads cold even though its road just left the nozzle. What the field
//     tracks is the weld at each plane, from the instant that weld is made.
//   * One sample per layer is coarse. The relaxation reconstruction recovers
//     the sub-interval shape well *while the trace still has somewhere to
//     fall*, but the floor is estimated from the remaining history, so cells
//     in the top one or two layers — whose history ends before they have
//     relaxed — get a conservative, under-estimated weld time. Believe the
//     ordering between two process windows; do not believe the absolute
//     healing number without a coupon test.
//   * The weld-time accumulation stops at the end of the recorded history.
//     A part still above Tg on the last step is truncated there — the
//     post-build cooldown and any anneal are not modelled.
//   * `z_strength_fraction` is a documented heuristic, not a qualified
//     allowable. The 0.85 contact fraction is a single fixed number for every
//     raster, and no printed-part knockdown (voids, moisture, anisotropic
//     modulus, layer-boundary stress concentration) is applied beyond it.
//   * §2.2 defines a cell's layer index as its cell tag when the tag is
//     non-negative. The ABI documents `tag` as the inherited geometry-entity
//     id, so a mesh that carries geometry tags rather than layer indices will
//     be mis-binned. Mesh with `mesher.am.layered`, or clear the tags and let
//     the centroid fallback do the binning.
//
// ---------------------------------------------------------------------------
// Inputs — `solver.am.polymer.fff` (souxmar_value_t map; every key optional)
// ---------------------------------------------------------------------------
//   nozzle_temperature           °C,          250      clamped [20, 500]
//   bed_temperature              °C,          100      clamped [0, 300]
//   chamber_temperature          °C,          40       clamped [0, 300]
//   layer_time                   s,           20       clamped [1e-3, 1e5]
//   layer_height                 m,           2.0e-4   clamped [1e-6, 0.1]
//   road_width                   m,           4.0e-4   clamped [1e-6, 0.1]
//   convection_coefficient       W/(m²·K),    30       clamped [0.1, 1000]
//   density                      kg/m³,       1010     clamped [1, 1e5]
//   specific_heat                J/(kg·K),    1800     clamped [1, 1e5]
//   thermal_conductivity         W/(m·K),     0.25     clamped [1e-4, 500]
//   glass_transition_temperature °C,          145      accepted, unused here
//   build_direction              list of 3,   [0,0,1]  normalised internally
//   baseplate_layers             int,         0        clamped [0, num_layers]
//   num_layers                   int,         0        0 ⇒ resolve from mesh
//
// Inputs — `postproc.am.bond_strength`
// ---------------------------------------------------------------------------
//   glass_transition_temperature °C,          145      clamped [-100, 400]
//   reptation_time_reference     s,           2.0      clamped [1e-6, 1e9]
//   reptation_reference_temperature °C,       260      clamped [-100, 600]
//   activation_energy            J/mol,       8.0e4    clamped [1e3, 1e6]
//   layer_time                   s,           20       clamped [1e-3, 1e5]
//
// `layer_time` is re-declared on the postproc stage because ABI v1 fields
// carry no time axis — the postproc cannot recover the sample spacing from
// the field and must be told. Keep it equal to the solver's `layer_time`.
//
// Determinism: no RNG, no wall clock, no environment, no unordered
// container; every accumulation runs in index order and every quadrature
// sub-division count is a compile-time constant.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// 0 °C expressed in kelvin (K). House rule: temperatures are °C at the YAML
// and field boundary; the Arrhenius shift below needs absolute temperature
// and converts with this.
constexpr double kZeroCelsiusInKelvin = 273.15;

// Molar gas constant R, J/(mol·K) — CODATA 2018 (exact by the 2019 SI
// redefinition).
constexpr double kGasConstant = 8.314462618;

// Interlayer contact-area fraction of a fully necked road bed (-). Sun et
// al. (2008) report fully-developed necks covering roughly 80–90 % of the
// interlayer plane for a standard raster; the remainder stays as the
// triangular inter-bead void, which no amount of healing recovers. 0.85 is
// the mid-point of that range and is *not* calibrated for a specific raster.
constexpr double kInterlayerContactFraction = 0.85;

// Reptation healing exponent (-): sigma/sigma_bulk ~ (t/t_rep)^(1/4).
// Wool & O'Connor (1981) / Yang & Pitchumani (2002).
constexpr double kHealingExponent = 0.25;

// Midpoint sub-samples per recorded layer interval when integrating the
// reduced weld time (-). Fixed rather than adaptive so the quadrature is
// byte-identical on every platform; 16 is well past convergence for one
// monotone relaxation segment of a smooth 1/t_rep(T).
constexpr int kSubStepsPerInterval = 16;

// Floor on the per-interval relaxation ratio (T[s+1] − floor)/(T[s] − floor)
// (-). A trace that reaches its floor within one recorded interval carries no
// rate information; 1e-6 caps how much cooling a single interval is credited
// with at 13.8 time constants, which is already far past the point where the
// samples are distinguishable from the floor.
constexpr double kRelaxRatioFloor = 1.0e-6;

// Cap on the dimensionless reduced-time accumulator (-). Anything >= 1
// already means full healing, so clamping at 1e6 keeps the accumulator
// finite for absurd input combinations without changing any answer.
constexpr double kReducedTimeCap = 1.0e6;

// Largest layer index the plugin honours (-). `mesher.am.layered` caps a
// build at 200 000 cells, so 200 000 layers is already unreachable; the
// bound exists so a mesh whose cell tags are geometry-entity ids cannot ask
// for a multi-terabyte field.
constexpr int kMaxLayers = 200000;

// Largest nodal history the solver will allocate, in doubles (~320 MB at
// 8 bytes each). Above this the request is rejected rather than served — a
// per-layer nodal history is num_nodes × num_layers and grows fast.
constexpr std::size_t kMaxFieldEntries = 40u * 1000u * 1000u;

// Sentinel layer index meaning "no cell has claimed this node yet".
constexpr int kUnclaimedLayer = -1;

// ---------------------------------------------------------------------------
// Value-bag helpers (read_number / read_int verbatim from modal-stub)
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
// default at the single point where the key is read.
double read_clamped(const souxmar_value_t* inputs, const char* key, double dv,
                    double lo, double hi) {
  return std::clamp(read_number(inputs, key, dv), lo, hi);
}

// §2.1 build direction. A key that is absent, or present but not a
// 3-element list of numbers, leaves `*out` untouched (same lenient handling
// as cfd-stub's `flow_direction`). A zero-length vector is rejected by the
// caller — it cannot be normalised.
void read_vec3(const souxmar_value_t* inputs, const char* key,
               std::array<double, 3>* out) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return;
  if (souxmar_value_list_size(v) != 3) return;
  std::array<double, 3> parsed{};
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return;
    parsed[i] = souxmar_value_as_number(c);
  }
  *out = parsed;
}

// ---------------------------------------------------------------------------
// §3.7 — process/material block and the pure thermal model
// ---------------------------------------------------------------------------

struct FffProcess {
  double nozzle_temperature_c   = 250.0;   // °C
  double bed_temperature_c      = 100.0;   // °C
  double chamber_temperature_c  = 40.0;    // °C
  double layer_time_s           = 20.0;    // s
  double layer_height_m         = 2.0e-4;  // m
  double road_width_m           = 4.0e-4;  // m
  double convection_w_m2k       = 30.0;    // W/(m²·K)
  double density_kg_m3          = 1010.0;  // kg/m³
  double specific_heat_j_kgk    = 1800.0;  // J/(kg·K)
  double conductivity_w_mk      = 0.25;    // W/(m·K)
};

// Lumped-capacitance time constant tau = rho·c·L_c/h_conv (s), with
// L_c = h·w/(w + 2h) (m) — the road's volume-to-convecting-area ratio with
// the welded bottom face excluded. Every denominator is a clamped input, so
// this cannot divide by zero.
double lumped_time_constant(const FffProcess& p) {
  const double lc = (p.layer_height_m * p.road_width_m) /
                    (p.road_width_m + 2.0 * p.layer_height_m);  // m
  return p.density_kg_m3 * p.specific_heat_j_kgk * lc / p.convection_w_m2k;
}

// Height (m) over which the heated bed still dominates the environment
// temperature: the conduction penetration depth sqrt(alpha·tau) with
// alpha = k/(rho·c) (m²/s), which is algebraically the fin length
// sqrt(k·L_c/h_conv) of the road stack — the steady balance between
// conduction up from the bed and convection off the sides. Independent of
// `layer_time` on purpose (see the header). Floored so the exponential that
// uses it never divides by zero for extreme clamped inputs.
double bed_influence_length(const FffProcess& p) {
  const double alpha = p.conductivity_w_mk /
                       (p.density_kg_m3 * p.specific_heat_j_kgk);  // m²/s
  return std::max(std::sqrt(alpha * lumped_time_constant(p)), 1.0e-12);
}

// Per-layer thermal history. Pure: no mesh, no ABI, no allocation beyond
// the three vectors it fills.
struct LayerHistory {
  std::vector<double> iface_c;  // °C, interface temperature at deposition of layer j
  std::vector<double> env_c;    // °C, environment layer j relaxes toward
  std::vector<double> decay;    // -,  exp(−d·t_layer/tau) for d = 0 .. num_layers-1
  double              tau_s = 0.0;  // s
};

LayerHistory build_layer_history(const FffProcess& p, int num_layers,
                                 int baseplate_layers) {
  LayerHistory h;
  const std::size_t n = static_cast<std::size_t>(num_layers);
  h.tau_s = lumped_time_constant(p);
  h.iface_c.assign(n, p.bed_temperature_c);
  h.env_c.assign(n, p.chamber_temperature_c);
  h.decay.assign(n, 0.0);

  for (std::size_t d = 0; d < n; ++d) {
    h.decay[d] = std::exp(-static_cast<double>(d) * p.layer_time_s / h.tau_s);
  }

  const double z_bed = bed_influence_length(p);
  for (std::size_t j = 0; j < n; ++j) {
    const double z_j = (static_cast<double>(j) + 0.5) * p.layer_height_m;  // m
    h.env_c[j] = p.chamber_temperature_c +
                 (p.bed_temperature_c - p.chamber_temperature_c) *
                     std::exp(-z_j / z_bed);
  }

  // Baseplate / raft layers are bed material: they sit at bed temperature and
  // the first printed layer lands on them exactly as it would on the bed.
  const double decay_one_interval = std::exp(-p.layer_time_s / h.tau_s);  // -
  double substrate_c = p.bed_temperature_c;
  for (std::size_t j = 0; j < n; ++j) {
    if (static_cast<int>(j) < baseplate_layers) {
      h.iface_c[j] = p.bed_temperature_c;
      h.env_c[j]   = p.bed_temperature_c;
      substrate_c  = p.bed_temperature_c;
      continue;
    }
    h.iface_c[j] = 0.5 * (p.nozzle_temperature_c + substrate_c);
    // Top of layer j at the instant layer j+1 lands, one `layer_time` later.
    substrate_c = h.env_c[j] + (h.iface_c[j] - h.env_c[j]) * decay_one_interval;
  }
  return h;
}

// Temperature (°C) at recorded step `step` of a node plane belonging to layer
// `layer`. `step == layer` returns that plane's interface temperature itself.
double layer_temperature_at_step(const LayerHistory& h, const FffProcess& p,
                                 int layer, int step, int baseplate_layers) {
  if (layer < baseplate_layers) return p.bed_temperature_c;
  if (layer > step) return p.chamber_temperature_c;  // not yet deposited
  const std::size_t l = static_cast<std::size_t>(layer);
  const std::size_t d = static_cast<std::size_t>(step - layer);
  return h.env_c[l] + (h.iface_c[l] - h.env_c[l]) * h.decay[d];
}

// ---------------------------------------------------------------------------
// §2.2 layer resolution
// ---------------------------------------------------------------------------

struct LayerResolution {
  std::vector<int> cell_layer;
  std::vector<int> node_layer;
  int              num_layers = 1;
};

// Bin a projection onto the build axis into a layer index. `s_min` is the
// mesh bounding-box minimum along that axis (§2.2 step 2).
int bin_layer(double s, double s_min, double layer_height) {
  const double raw = std::floor((s - s_min) / layer_height);
  if (!(raw > 0.0)) return 0;                             // also catches NaN
  if (raw > static_cast<double>(kMaxLayers - 1)) return kMaxLayers;  // → rejected
  return static_cast<int>(raw);
}

souxmar_status_t resolve_layers(const souxmar_mesh_t*        mesh,
                               const std::array<double, 3>& build_dir,
                               double                       layer_height,
                               int                          requested_layers,
                               LayerResolution*             out) {
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  // Bounding-box minimum along the build axis, accumulated in index order.
  double s_min = coords[0] * build_dir[0] + coords[1] * build_dir[1] +
                 coords[2] * build_dir[2];
  for (std::size_t i = 1; i < num_nodes; ++i) {
    const double s = coords[i * 3 + 0] * build_dir[0] +
                     coords[i * 3 + 1] * build_dir[1] +
                     coords[i * 3 + 2] * build_dir[2];
    if (s < s_min) s_min = s;
  }

  out->cell_layer.assign(num_cells, 0);
  out->node_layer.assign(num_nodes, kUnclaimedLayer);

  std::vector<std::uint64_t> cell_nodes;
  int max_layer = 0;
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t node_count = souxmar_mesh_cell_node_count(mesh, c);
    const std::int32_t tag = souxmar_mesh_cell_tag(mesh, c);

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
      cell_nodes.assign(node_count, 0);
      const souxmar_status_t cs =
          souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count);
      if (cs.code != SOUXMAR_OK) return cs;
      double s_sum = 0.0;
      for (std::size_t k = 0; k < node_count; ++k) {
        const std::uint64_t nid = cell_nodes[k];
        if (nid >= num_nodes) {
          return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                      "cell references out-of-range node id");
        }
        s_sum += coords[nid * 3 + 0] * build_dir[0] +
                 coords[nid * 3 + 1] * build_dir[1] +
                 coords[nid * 3 + 2] * build_dir[2];
      }
      layer = bin_layer(s_sum / static_cast<double>(node_count), s_min, layer_height);
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
  if (requested_layers > 0) num_layers = std::min(requested_layers, kMaxLayers);
  out->num_layers = num_layers;

  // A node plane belongs to the highest layer that touches it: that is the
  // layer whose *bottom* interface the plane is, i.e. the weld formed at that
  // plane when the layer lands. The top plane of the part has no layer above
  // it and so shares the top layer with the plane below.
  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t node_count = souxmar_mesh_cell_node_count(mesh, c);
    if (node_count == 0) continue;
    cell_nodes.assign(node_count, 0);
    const souxmar_status_t cs =
        souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count);
    if (cs.code != SOUXMAR_OK) return cs;
    const int layer = std::min(out->cell_layer[c], num_layers - 1);
    out->cell_layer[c] = layer;
    for (std::size_t k = 0; k < node_count; ++k) {
      const std::uint64_t nid = cell_nodes[k];
      if (nid >= num_nodes) {
        return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                    "cell references out-of-range node id");
      }
      if (layer > out->node_layer[nid]) out->node_layer[nid] = layer;
    }
  }

  // Nodes no cell references (an orphan node cannot inherit a cell's layer)
  // fall back to binning their own projection.
  for (std::size_t i = 0; i < num_nodes; ++i) {
    if (out->node_layer[i] != kUnclaimedLayer) continue;
    const double s = coords[i * 3 + 0] * build_dir[0] +
                     coords[i * 3 + 1] * build_dir[1] +
                     coords[i * 3 + 2] * build_dir[2];
    out->node_layer[i] = std::min(bin_layer(s, s_min, layer_height), num_layers - 1);
  }
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------------
// §3.8 — reptation healing (pure)
// ---------------------------------------------------------------------------

// Reduced-time rate 1/t_rep(T) in 1/s, with t_rep Arrhenius-shifted from its
// reference value: t_rep(T) = t_rep_ref·exp((Ea/R)(1/T − 1/T_ref)), T in K.
// Formed as a reciprocal so an unreachably long reptation time contributes
// zero instead of overflowing. Only ever called with `t_c` above the clamped
// glass transition (>= -100 °C), so the absolute temperature is >= 173 K and
// the reciprocal is safe.
double reciprocal_reptation_time(double t_c, double t_rep_ref_s, double t_ref_c,
                                 double activation_j_mol) {
  const double t_k     = t_c + kZeroCelsiusInKelvin;
  const double t_ref_k = t_ref_c + kZeroCelsiusInKelvin;
  const double arg =
      (activation_j_mol / kGasConstant) * (1.0 / t_k - 1.0 / t_ref_k);
  // Clamped so exp() stays finite for every clamped input combination
  // (exp(±700) is the double limit; -100 bounds the growth side).
  return std::exp(-std::clamp(arg, -100.0, 700.0)) / t_rep_ref_s;
}

struct BondResult {
  double degree_of_healing = 0.0;  // -
  double seconds_above_tg  = 0.0;  // s
};

// Walk a per-layer temperature history (°C, samples `interval_s` apart),
// accumulate the reduced weld time over the portions above Tg, and apply the
// quarter-power healing law. A single-sample history spans no time and
// therefore heals not at all — that is a real limitation, not a bug: run the
// solver over at least two layers.
//
// `relax_floor` is caller-provided scratch with room for `num_steps` doubles.
// It receives the suffix minimum of the history — the floor each interval is
// still relaxing toward — which is what turns two samples into a relaxation
// curve. Filling it runs backwards, but min() is exact and order-independent,
// so reproducibility is unaffected; the reduced-time sum below still
// accumulates in index order.
BondResult evaluate_bond(const double* history, std::size_t num_steps,
                         double interval_s, double tg_c, double t_rep_ref_s,
                         double t_ref_c, double activation_j_mol,
                         double* relax_floor) {
  BondResult r;
  if (num_steps == 0) return r;

  double running_min = std::numeric_limits<double>::infinity();
  for (std::size_t s = num_steps; s-- > 0;) {
    const double t = history[s];
    if (std::isfinite(t) && t < running_min) running_min = t;
    relax_floor[s] = running_min;
  }

  double xi = 0.0;  // dimensionless reduced weld time
  for (std::size_t s = 0; s + 1 < num_steps; ++s) {
    const double t0 = history[s];
    const double t1 = history[s + 1];
    // A non-finite sample (a broken upstream field) contributes nothing
    // rather than poisoning the whole cell with NaN.
    if (!std::isfinite(t0) || !std::isfinite(t1)) continue;
    // Cooling (or isothermal) intervals only — see the header: a rising
    // interval is the once-per-layer sampling artefact of a road appearing
    // at its deposition temperature, not physical pre-heat.
    if (t1 > t0) continue;
    if (t0 <= tg_c) continue;  // never above the glass transition

    // Relaxation reconstruction over this interval. `floor_c <= t1 <= t0` by
    // construction (the floor is a minimum of the remaining history), so the
    // amplitude is non-negative and the ratio is in [0, 1].
    const double floor_c   = relax_floor[s + 1];
    const double amplitude = t0 - floor_c;
    const bool   decaying  = amplitude > 0.0;
    // A trace that has fully relaxed inside one interval carries no rate
    // information at all; the floored ratio treats that as the most cooling
    // one interval can resolve (1e-6 ≈ 13.8 time constants).
    const double ratio =
        decaying ? std::clamp((t1 - floor_c) / amplitude, kRelaxRatioFloor, 1.0)
                 : 1.0;

    // Fraction of the interval spent above Tg. Reaching this with t1 <= tg_c
    // implies t1 < t0, so both crossing forms are safe.
    double f_hi = 1.0;
    if (t1 <= tg_c) {
      if (decaying && tg_c > floor_c) {
        f_hi = std::min(1.0, std::log((tg_c - floor_c) / amplitude) / std::log(ratio));
      } else {
        f_hi = (tg_c - t0) / (t1 - t0);
      }
    }
    const double span_s = f_hi * interval_s;
    if (!(span_s > 0.0)) continue;
    r.seconds_above_tg += span_s;

    const double dt = span_s / static_cast<double>(kSubStepsPerInterval);
    for (int k = 0; k < kSubStepsPerInterval; ++k) {
      const double frac = f_hi * ((static_cast<double>(k) + 0.5) /
                                 static_cast<double>(kSubStepsPerInterval));
      const double t_mid = decaying ? floor_c + amplitude * std::pow(ratio, frac)
                                    : t0 + (t1 - t0) * frac;  // °C
      xi = std::min(xi + dt * reciprocal_reptation_time(t_mid, t_rep_ref_s,
                                                        t_ref_c, activation_j_mol),
                    kReducedTimeCap);
    }
  }
  r.degree_of_healing = std::min(1.0, std::pow(xi, kHealingExponent));
  return r;
}

// ---------------------------------------------------------------------------
// `solver.am.polymer.fff`
// ---------------------------------------------------------------------------

souxmar_status_t polymer_fff_solve_impl(const souxmar_mesh_t*  mesh,
                                       const souxmar_value_t* inputs,
                                       souxmar_field_t**      out_field) {
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
  if (souxmar_mesh_num_cells(mesh) == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "mesh has no cells; layer resolution needs cells");
  }

  FffProcess p;
  p.nozzle_temperature_c  = read_clamped(inputs, "nozzle_temperature",     250.0,  20.0, 500.0);
  p.bed_temperature_c     = read_clamped(inputs, "bed_temperature",        100.0,   0.0, 300.0);
  p.chamber_temperature_c = read_clamped(inputs, "chamber_temperature",     40.0,   0.0, 300.0);
  p.layer_time_s          = read_clamped(inputs, "layer_time",              20.0, 1e-3, 1e5);
  p.layer_height_m        = read_clamped(inputs, "layer_height",          2.0e-4, 1e-6, 0.1);
  p.road_width_m          = read_clamped(inputs, "road_width",            4.0e-4, 1e-6, 0.1);
  p.convection_w_m2k      = read_clamped(inputs, "convection_coefficient",  30.0,  0.1, 1000.0);
  p.density_kg_m3         = read_clamped(inputs, "density",               1010.0,  1.0, 1e5);
  p.specific_heat_j_kgk   = read_clamped(inputs, "specific_heat",         1800.0,  1.0, 1e5);
  p.conductivity_w_mk     = read_clamped(inputs, "thermal_conductivity",    0.25, 1e-4, 500.0);

  // The glass transition belongs to this input block (both AM polymer stages
  // are configured from one YAML anchor) but only `postproc.am.bond_strength`
  // acts on it — the temperature history itself does not depend on Tg. Read
  // and discarded here so the key is accepted rather than silently unknown.
  (void) read_clamped(inputs, "glass_transition_temperature", 145.0, -100.0, 400.0);

  std::array<double, 3> build_dir{0.0, 0.0, 1.0};
  read_vec3(inputs, "build_direction", &build_dir);
  const double bd_norm = std::sqrt(build_dir[0] * build_dir[0] +
                                   build_dir[1] * build_dir[1] +
                                   build_dir[2] * build_dir[2]);
  if (!(bd_norm > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "build_direction has zero length");
  }
  for (std::size_t i = 0; i < 3; ++i) build_dir[i] /= bd_norm;

  const int requested_layers =
      std::clamp(read_int(inputs, "num_layers", 0), 0, kMaxLayers);

  LayerResolution lr;
  const souxmar_status_t rs =
      resolve_layers(mesh, build_dir, p.layer_height_m, requested_layers, &lr);
  if (rs.code != SOUXMAR_OK) return rs;

  const int num_layers      = lr.num_layers;
  const int baseplate_layers = std::clamp(read_int(inputs, "baseplate_layers", 0),
                                          0, num_layers);

  const std::size_t entries =
      num_nodes * static_cast<std::size_t>(num_layers);
  if (entries > kMaxFieldEntries) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "num_nodes * num_layers exceeds the 40M-sample history cap");
  }

  const LayerHistory h = build_layer_history(p, num_layers, baseplate_layers);

  souxmar_field_t* field = souxmar_field_new(
      "interface_temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, num_nodes,
      static_cast<std::size_t>(num_layers));
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != entries) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout: data[step * num_nodes + node] (scalar field, one component).
  for (int step = 0; step < num_layers; ++step) {
    const std::size_t step_off = static_cast<std::size_t>(step) * num_nodes;
    for (std::size_t i = 0; i < num_nodes; ++i) {
      data[step_off + i] = layer_temperature_at_step(
          h, p, lr.node_layer[i], step, baseplate_layers);
    }
  }
  *out_field = field;
  return souxmar_status_ok();
}

souxmar_status_t polymer_fff_solve(const souxmar_mesh_t*           mesh,
                                  const souxmar_value_t*          inputs,
                                  const souxmar_solver_options_t* /*options*/,
                                  souxmar_field_t**               out_field,
                                  void*                           /*user_data*/) {
  // Nothing below throws by design, but the per-layer history vectors are
  // sized from user input; a bad_alloc must become a status, not an
  // exception crossing the C ABI.
  try {
    return polymer_fff_solve_impl(mesh, inputs, out_field);
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "allocation failed while building the layer history");
  }
}

// ---------------------------------------------------------------------------
// `postproc.am.bond_strength`
// ---------------------------------------------------------------------------

souxmar_status_t bond_strength_compute_impl(const souxmar_mesh_t*  mesh,
                                           const souxmar_field_t* input_field,
                                           const souxmar_value_t* inputs,
                                           souxmar_field_t**      out_field) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!input_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "input_field is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no cells");
  }

  // The intended producer is `solver.am.polymer.fff` ("interface_temperature"),
  // but any scalar °C history over the same mesh is a valid input — the
  // healing law only needs a temperature-vs-time trace at the interface.
  const std::uint8_t location   = souxmar_field_location(input_field);
  const std::size_t  components = souxmar_field_components(input_field);
  if (components != 1 ||
      (location != SOUXMAR_FL_NODAL && location != SOUXMAR_FL_CELL)) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "postproc.am.bond_strength needs a nodal or cell scalar temperature history");
  }
  const std::size_t expected_count =
      (location == SOUXMAR_FL_NODAL) ? num_nodes : num_cells;
  const std::size_t num_steps = souxmar_field_num_time_steps(input_field);
  if (souxmar_field_count(input_field) != expected_count) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "input field count does not match the mesh");
  }
  const double* fdata = souxmar_field_data_const(input_field);
  if (!fdata ||
      souxmar_field_data_size(input_field) != expected_count * num_steps) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "input field buffer size mismatch");
  }

  const double tg_c = read_clamped(inputs, "glass_transition_temperature",
                                   145.0, -100.0, 400.0);
  const double t_rep_ref_s = read_clamped(inputs, "reptation_time_reference",
                                          2.0, 1e-6, 1e9);
  const double t_ref_c = read_clamped(inputs, "reptation_reference_temperature",
                                      260.0, -100.0, 600.0);
  const double activation_j_mol =
      read_clamped(inputs, "activation_energy", 8.0e4, 1e3, 1e6);
  const double layer_time_s = read_clamped(inputs, "layer_time", 20.0, 1e-3, 1e5);

  souxmar_field_t* out = souxmar_field_new("bond_quality", SOUXMAR_FL_CELL,
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

  std::vector<double>        history(num_steps, 0.0);
  std::vector<double>        relax_floor(num_steps, 0.0);
  std::vector<std::uint64_t> cell_nodes;

  for (std::size_t c = 0; c < num_cells; ++c) {
    if (location == SOUXMAR_FL_CELL) {
      for (std::size_t s = 0; s < num_steps; ++s) {
        history[s] = fdata[s * num_cells + c];
      }
    } else {
      const std::size_t node_count = souxmar_mesh_cell_node_count(mesh, c);
      if (node_count == 0) {
        // Unknown / out-of-range cell type — NaN says "not evaluated", the
        // same convention postproc.mesh_quality uses.
        for (std::size_t k = 0; k < 3; ++k) {
          out_data[c * 3 + k] = std::numeric_limits<double>::quiet_NaN();
        }
        continue;
      }
      cell_nodes.assign(node_count, 0);
      const souxmar_status_t cs =
          souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count);
      if (cs.code != SOUXMAR_OK) {
        souxmar_field_free(out);
        return cs;
      }
      // Representative node = the hottest history bounding this cell (ties
      // resolved by the lowest node index, so the choice is deterministic).
      // Averaging the cell's nodes instead would smear together two weld
      // planes that are one step out of phase and flatten the very peak the
      // healing law is exponentially sensitive to.
      std::uint64_t rep_node = 0;
      double        rep_peak = -std::numeric_limits<double>::infinity();
      bool          rep_set  = false;
      for (std::size_t k = 0; k < node_count; ++k) {
        const std::uint64_t nid = cell_nodes[k];
        if (nid >= num_nodes) {
          souxmar_field_free(out);
          return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                      "cell references out-of-range node id");
        }
        double peak = -std::numeric_limits<double>::infinity();
        for (std::size_t s = 0; s < num_steps; ++s) {
          const double t = fdata[s * num_nodes + nid];
          if (t > peak) peak = t;
        }
        if (!rep_set || peak > rep_peak) {
          rep_peak = peak;
          rep_node = nid;
          rep_set  = true;
        }
      }
      for (std::size_t s = 0; s < num_steps; ++s) {
        history[s] = fdata[s * num_nodes + rep_node];
      }
    }

    const BondResult r =
        evaluate_bond(history.data(), num_steps, layer_time_s, tg_c, t_rep_ref_s,
                      t_ref_c, activation_j_mol, relax_floor.data());
    out_data[c * 3 + 0] = r.degree_of_healing;
    out_data[c * 3 + 1] =
        std::clamp(kInterlayerContactFraction * r.degree_of_healing, 0.0, 1.0);
    out_data[c * 3 + 2] = r.seconds_above_tg;
  }

  *out_field = out;
  return souxmar_status_ok();
}

souxmar_status_t bond_strength_compute(
    const souxmar_mesh_t*             mesh,
    const souxmar_field_t*            input_field,
    const souxmar_value_t*            inputs,
    const souxmar_postproc_options_t* /*options*/,
    souxmar_field_t**                 out_field,
    void*                             /*user_data*/) {
  try {
    return bond_strength_compute_impl(mesh, input_field, inputs, out_field);
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "allocation failed while reducing the temperature history");
  }
}

constexpr souxmar_solver_vtable_t kFffVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &polymer_fff_solve,
    nullptr,
};

constexpr souxmar_postproc_vtable_t kBondVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &bond_strength_compute,
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
      registry, "solver.am.polymer.fff", &kFffVtable, /*user_data=*/nullptr);
  if (solver_status.code != SOUXMAR_OK) return 1;
  const souxmar_status_t postproc_status = souxmar_registry_add_postproc(
      registry, "postproc.am.bond_strength", &kBondVtable, /*user_data=*/nullptr);
  if (postproc_status.code != SOUXMAR_OK) return 1;
  return 0;
}
