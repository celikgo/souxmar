// SPDX-License-Identifier: Apache-2.0
//
// marine — seawater material integrity for additively-manufactured subsea
// parts, plus the advisory qualification dossier.
//
// Registered by souxmar_plugin_register_v1 in marine_loads.cpp (one exported
// symbol per plugin); the two vtables are published through marine_data.hpp.
//
//   solver.marine.corrosion              per-cell corrosion / pitting /
//                                        galvanic indicators
//   writer.marine.qualification_report   Markdown AM qualification dossier
//
// ===========================================================================
// solver.marine.corrosion
// ===========================================================================
//
// What it computes:
//   Three per-cell indicators for a part sitting in seawater for
//   `service_life_years`. The alloy tables, the galvanic series and their
//   sources are in marine_data.hpp.
//
//   [0] thickness_loss_mm — the total WALL PENETRATION estimate over the
//       service life, i.e. what you compare against your corrosion
//       allowance. It is the sum of three terms:
//
//         uniform : rate_ref(alloy) * f_T * f_O2 * f_S * f_v * f_surface
//                   * years
//             f_T        = 2^((T - 10 degC)/15 K), clamped [0.5, 8] — the
//                          "rate doubles every 10-15 K" rule of thumb for
//                          aqueous corrosion (ASM Handbook Vol. 13A).
//             f_O2       = O2 / 8 mg/L, clamped [0.05, 2.5] — oxygen
//                          reduction is the cathodic rate-determining step
//                          in aerated seawater (LaQue 1975).
//             f_S        = sqrt(S / 35 PSU), clamped — a deliberately weak
//                          conductivity/chloride term.
//             f_v        = (v / 0.5 m/s)^0.35, clamped [0.5, 2.5] — mass
//                          transfer; then, above the alloy's critical
//                          velocity, an erosion-corrosion multiplier
//                          1 + 1.5*(v/v_crit - 1) capped at 6. Copper
//                          alloys are the only entries with a real limit
//                          (Efird, Corrosion 33(1), 1977).
//             f_surface  = 1.3 when `as_built_surface` (default) else 1.0.
//
//         galvanic : the couple is assumed OXYGEN-LIMITED, which is the
//             normal case in aerated seawater: the cathode can only consume
//             electrons as fast as oxygen arrives at it, so
//                 i_anode = i_lim * f_O2 * f_v * area_ratio * min(drive, 1)
//                 rate    = i_anode * k_ec(alloy)
//             i_lim = 0.15 A/m^2, the initial design current density for
//             temperate open seawater in DNV-RP-B401 (cathodic-protection
//             design); k_ec is the Faraday thickness-loss coefficient in
//             mm/a per A/m^2 (1.16 for iron — the textbook value).
//             `drive` = |dE| / 0.25 V, capped at 1, because past that
//             driving force the couple is already limited by oxygen supply
//             and extra volts buy no extra current. 0.25 V is the
//             unlike-metal coupling limit of MIL-STD-889C for general
//             marine service (0.15 V for critical applications). Only the
//             ANODIC member loses metal, so this term is zero when the part
//             is the noble member of the couple.
//
//         pitting : power-law pit growth, depth = k_pit * sqrt(years),
//             weighted by pitting_risk^2. The sqrt-of-time form is the one
//             used throughout the marine immersion literature (Melchers,
//             Corrosion 59-60, 2003-2004). k_pit is 0.25 mm/a^0.5 for
//             stainless / Ni-Cr-Mo, 0.30 for aluminium, 0.15 for the copper
//             alloys, 0.01 for titanium. The risk^2 weight is an explicit
//             tuning choice with no derivation: it suppresses the term when
//             initiation is unlikely.
//
//       Coatings scale the uniform + galvanic terms by (1 - efficiency);
//       cathodic protection scales them by 0.05 (residual, for imperfect
//       potential distribution and shielded geometry).
//
//       This is rate x life with NO wall-thickness limit, because the wall
//       thickness is not an input. A result larger than your wall means the
//       part perforates somewhere inside the service life — it does not mean
//       that much metal is available to lose. A small anode wired to a large
//       cathode (area ratio 10 or 100) will produce exactly such a number,
//       and that is the model telling you the couple is unacceptable.
//
//   [1] pitting_risk (0..1) — a scaled indicator, NOT a probability.
//       For the stainless / Ni-Cr-Mo alloys it is a logistic in
//       (T - T_crit) with an 8 K width, where
//           PREN     = %Cr + 3.3*%Mo + 16*%N
//           CPT      = 2.24*PREN - 47.1  [degC]
//       the CPT-vs-PREN regression on ASTM G48 (6 % FeCl3) data used
//       throughout the duplex literature (Bernhardsson / Rondelli). It
//       reads 0.5 exactly at the critical temperature. Stagnant water
//       lowers the critical temperature toward the CRITICAL CREVICE
//       temperature — deposits and biofilm make crevices — by up to 20 K,
//       faded in linearly below 0.3 m/s. Titanium is treated as immune
//       below the ~80 degC seawater limit; the copper alloys and aluminium
//       get a flat base risk because PREN does not order them at all.
//       `as_built_surface` adds a 10 % share of the remaining risk.
//
//   [2] galvanic_risk (0..1) — drive * sqrt(area_ratio) folded through
//       x/(1+x), so it reads 0.5 for a 0.25 V couple at equal areas and
//       saturates for a small anode wired to a large cathode. When the part
//       is the NOBLE member the number drops to a small residual (the
//       mating part is the one dissolving) — but see the caveats.
//
//   Every cell carries the same triple: the inputs are part-level, so this
//   is an analytical answer painted onto the mesh, not a mesh-resolved one.
//
//   Determinism note: this file's only cross-libm exposure is three calls —
//   std::exp in the logistic and std::pow for the temperature and velocity
//   factors. Everything else is add/multiply/sqrt, which are correctly
//   rounded IEEE-754 operations. std::pow and std::exp may differ in the last
//   bit or two between platform libms; there is no bit-exact stdlib-only
//   replacement, and plugins may not link Boost. solver.marine.hydrostatic
//   and solver.marine.hull_collapse are written to avoid them entirely.
//
// What this is NOT:
//   * Not design data. Every rate is an indicative literature value chosen
//     to let alloys be COMPARED. A design corrosion allowance comes from a
//     project corrosion assessment, exposure data and (for CP) a
//     DNV-RP-B401-style current-demand calculation.
//   * No microbiologically-influenced corrosion, no sulfide-polluted
//     seawater, no chlorination, no crevice geometry, no weld metallurgy,
//     no sensitisation, no stress-corrosion cracking, no hydrogen
//     embrittlement of the cathodic member (which matters when titanium or
//     a high-strength steel is the noble half of a couple), no fatigue
//     interaction.
//   * The galvanic term is a bounding estimate: it assumes the couple stays
//     polarised for the whole life, ignores anode consumption, current
//     distribution and the resistivity of the electrolyte path. A part
//     nowhere near its mating metal is not galvanically coupled at all.
//   * The as-built-surface penalty is engineering judgement. The AM
//     corrosion literature is genuinely split: fully-dense LPBF 316L often
//     out-performs wrought 316L (fine cellular structure, no MnS
//     inclusions), while as-built porosity, lack-of-fusion voids and
//     partially-melted powder create crevices that make it worse. This
//     model takes the pessimistic side and says so.
//
// Inputs (souxmar_value_t map):
//   alloy                   : string, default "316L"; 2507, NAB, Ti6Al4V,
//                             IN625, AlSi10Mg, CuNi90-10 (aliases in
//                             marine_data.hpp). An unrecognised name falls
//                             back to the explicit composition inputs and to
//                             316L electrochemistry.
//   chromium_pct            : number, %, default from the alloy table
//   molybdenum_pct          : number, %, default from the alloy table
//   nitrogen_pct            : number, %, default from the alloy table
//   mating_alloy            : string, default "" (no couple). Must resolve in
//                             the galvanic series, else
//                             SOUXMAR_E_INVALID_ARGUMENT.
//   area_ratio_cathode_anode: number, -, default 1.0 (clamped [0.01, 1000])
//   seawater_temperature    : number, degC, default 10 (clamped [-2, 40])
//   salinity_psu            : number, PSU, default 35 (clamped [0, 42])
//   flow_velocity           : number, m/s, default 0.5 (clamped [0, 30])
//   oxygen_mg_per_l         : number, mg/L, default 8.0 (clamped [0, 20])
//   service_life_years      : number, a, default 25 (clamped [0, 100])
//   cathodic_protection     : bool, default false
//   coating_efficiency      : number, -, default 0.0 (clamped [0, 0.99])
//   as_built_surface        : bool, default true
//
// Output: cell vector Field "corrosion", 1 time step, components
//   [0] thickness_loss_mm  [1] pitting_risk  [2] galvanic_risk
//
// ===========================================================================
// writer.marine.qualification_report
// ===========================================================================
//
// What it computes: nothing. It renders a Markdown dossier that states what
// evidence an AM part for marine service normally has to carry, echoes the
// declared part data, summarises whichever simulation field it was handed,
// and fingerprints the analysis model so the document can be tied back to it.
//
// What this is NOT — and this is the whole point of the file:
//   * souxmar is not a classification society, notified body, or certifying
//     authority. Running this tool qualifies nothing.
//   * The checklist is derived from PUBLICLY DOCUMENTED AM qualification
//     practice (DNV-ST-B203, the ABS Guide for Additive Manufacturing,
//     ISO/ASTM 52920 and 52930). It is not a reproduction of any of them and
//     it does not demonstrate compliance with any of them.
//   * No certificate number, stamp, surveyor name, approval reference or
//     test result is generated. Every such field is left blank for the
//     responsible engineer. The generator refuses to invent them.
//   * The content digest is FNV-1a 64-bit — a non-cryptographic content
//     hash, not a signature. It detects an accidentally different mesh; it
//     does not detect a deliberately altered one.
//   * No wall-clock time, no absolute path, no environment value is written,
//     so two runs on two machines produce byte-identical output.
//
// Inputs (souxmar_value_t map):
//   path               : string, REQUIRED
//   part_name          : string, default "AM part"
//   process            : string, default "lpbf"
//   alloy              : string, default "316L"
//   application        : string, hull | propulsion | piping | structural |
//                        non_structural; default "structural"
//   criticality        : int 1..3, default 2 — 1 = HIGHEST consequence of
//                        failure, 3 = lowest. Stated explicitly because
//                        different class frameworks number their categories
//                        in opposite directions; map to yours yourself.
//   design_depth       : number, m, default 300
//   service_life_years : number, a, default 25
//   class_framework    : string, default "generic" — echoed as declared,
//                        never verified.
//   redundancy         : bool, default false

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <locale>
#include <sstream>
#include <string>
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

using souxmar_marine::AlloyEntry;
using souxmar_marine::GalvanicEntry;
using souxmar_marine::PittingModel;
using souxmar_marine::ascii_iequals;
using souxmar_marine::read_bool;
using souxmar_marine::read_int;
using souxmar_marine::read_number;
using souxmar_marine::read_string;

// ---------------------------------------------------------------------------
// Corrosion model constants. Every one carries its unit and its source.
// ---------------------------------------------------------------------------

// Oxygen-diffusion-limited cathodic current density for bare steel in
// temperate open seawater, at the reference velocity and oxygen content:
// DNV-RP-B401 "Cathodic protection design" initial design current density,
// 0.15 A/m^2 for 7-11 degC open-ocean water.
constexpr double kOxygenLimitedCurrentDensityAPerM2 = 0.15;

// MIL-STD-889C unlike-metal coupling limit for general marine service. Used
// as the reference galvanic driving force, not as a pass/fail threshold.
constexpr double kGalvanicReferenceDriveV = 0.25;

// CPT-vs-PREN regression on ASTM G48 (6 % FeCl3) data, degC.
constexpr double kCptSlopeCPerPren  = 2.24;
constexpr double kCptInterceptC     = -47.1;
// Crevice initiation sits ~20 K below pitting initiation for the same alloy.
constexpr double kCreviceShiftK     = 20.0;
// Logistic width of the initiation indicator, K.
constexpr double kInitiationWidthK  = 8.0;
// Below this velocity deposits/biofilm are assumed to build up, fading the
// critical temperature from CPT toward CCT.
constexpr double kStagnantVelocityMPerS = 0.3;

// Pit-depth coefficients, mm per sqrt(year).
constexpr double kPitCoeffStainlessMmPerSqrtA = 0.25;
constexpr double kPitCoeffAluminiumMmPerSqrtA = 0.30;
constexpr double kPitCoeffCopperMmPerSqrtA    = 0.15;
constexpr double kPitCoeffTitaniumMmPerSqrtA  = 0.01;

// Flat base initiation indicators for the alloy families PREN cannot order.
constexpr double kBaseRiskCopperAlloy = 0.15;
constexpr double kBaseRiskAluminium   = 0.60;
constexpr double kBaseRiskTitanium    = 0.02;

// As-built AM surfaces take this share of the remaining initiation risk.
constexpr double kAsBuiltRiskShare = 0.10;
// ... and this multiplier on the uniform rate (roughness + near-surface
// porosity). Engineering judgement, documented in the header comment.
constexpr double kAsBuiltRateFactor = 1.3;
// Residual anodic dissolution left under working cathodic protection.
constexpr double kCathodicProtectionResidual = 0.05;
// Cathodic protection also suppresses pit initiation, but not to zero
// (shielded crevices see less current than the open surface).
constexpr double kCathodicProtectionPitFactor = 0.20;
// A coating with efficiency c leaves (1 - 0.8 c) of the initiation risk:
// holidays and damage are exactly where pits start.
constexpr double kCoatingPitCredit = 0.8;

// Blend a penalty into a 0..1 indicator without ever leaving [0, 1].
double add_share(double risk, double share) { return risk + (1.0 - risk) * share; }

double logistic(double x) { return 1.0 / (1.0 + std::exp(-x)); }

// ---------------------------------------------------------------------------
// solver.marine.corrosion
// ---------------------------------------------------------------------------

struct CorrosionResult {
  double thickness_loss_mm = 0.0;
  double pitting_risk      = 0.0;
  double galvanic_risk     = 0.0;
};

souxmar_status_t corrosion_evaluate(const souxmar_value_t* inputs,
                                    CorrosionResult*       out) {
  const char*       alloy_name  = read_string(inputs, "alloy", "316L");
  const AlloyEntry* alloy       = souxmar_marine::find_alloy(alloy_name);
  const bool        alloy_known = (alloy != nullptr);
  const AlloyEntry& base        = alloy_known ? *alloy : souxmar_marine::default_alloy();

  // Composition: alloy-table defaults, overridable per element. An
  // unrecognised alloy name therefore lands on the explicit inputs, whose
  // own defaults are 316L's composition (contract §3.17).
  const double cr = std::clamp(read_number(inputs, "chromium_pct", base.chromium_pct),
                               0.0, 40.0);   // wt%, no engineering alloy exceeds this
  const double mo = std::clamp(read_number(inputs, "molybdenum_pct", base.molybdenum_pct),
                               0.0, 20.0);   // wt%
  const double n  = std::clamp(read_number(inputs, "nitrogen_pct", base.nitrogen_pct),
                               0.0, 1.0);    // wt%
  const double pren_value = souxmar_marine::pren(cr, mo, n);

  const double temp_c = std::clamp(read_number(inputs, "seawater_temperature",
                                               souxmar_marine::kRefTemperatureC),
                                   souxmar_marine::kSeaTemperatureMinC,
                                   souxmar_marine::kSeaTemperatureMaxC);
  const double salinity = std::clamp(read_number(inputs, "salinity_psu",
                                                 souxmar_marine::kRefSalinityPsu),
                                     souxmar_marine::kSalinityMinPsu,
                                     souxmar_marine::kSalinityMaxPsu);
  const double velocity = std::clamp(read_number(inputs, "flow_velocity",
                                                 souxmar_marine::kRefVelocityMPerS),
                                     0.0, 30.0);   // m/s
  const double oxygen   = std::clamp(read_number(inputs, "oxygen_mg_per_l",
                                                 souxmar_marine::kRefOxygenMgPerL),
                                     0.0, 20.0);   // mg/L (saturation ~11 at 10 degC)
  const double years    = std::clamp(read_number(inputs, "service_life_years", 25.0),
                                     0.0, 100.0);  // a
  const bool   cp       = read_bool(inputs, "cathodic_protection", false);
  const double coating  = std::clamp(read_number(inputs, "coating_efficiency", 0.0),
                                     0.0, 0.99);
  const bool   as_built = read_bool(inputs, "as_built_surface", true);
  const double area_ratio =
      std::clamp(read_number(inputs, "area_ratio_cathode_anode", 1.0), 0.01, 1000.0);

  // ---- environment factors on the uniform rate --------------------------
  const double f_temp = std::clamp(
      std::pow(2.0, (temp_c - souxmar_marine::kRefTemperatureC) / 15.0), 0.5, 8.0);
  const double f_oxygen = std::clamp(oxygen / souxmar_marine::kRefOxygenMgPerL,
                                     0.05, 2.5);
  const double f_salinity =
      std::sqrt(std::clamp(salinity / souxmar_marine::kRefSalinityPsu, 0.3, 1.2));
  const double f_transfer = std::clamp(
      std::pow(std::max(velocity, 0.01) / souxmar_marine::kRefVelocityMPerS, 0.35),
      0.5, 2.5);
  double f_erosion = 1.0;
  if (velocity > base.critical_velocity_m_s) {
    f_erosion = std::min(1.0 + 1.5 * (velocity / base.critical_velocity_m_s - 1.0), 6.0);
  }
  const double f_surface = as_built ? kAsBuiltRateFactor : 1.0;

  const double uniform_rate_mm_per_a = base.uniform_rate_mm_per_year * f_temp *
                                       f_oxygen * f_salinity * f_transfer *
                                       f_erosion * f_surface;

  // ---- galvanic couple ---------------------------------------------------
  const char* mating_name = read_string(inputs, "mating_alloy", "");
  double galvanic_rate_mm_per_a = 0.0;
  double galvanic_risk          = 0.0;
  if (mating_name[0] != '\0') {
    const GalvanicEntry* self_entry = souxmar_marine::find_galvanic(
        alloy_known ? base.galvanic_key : alloy_name);
    const GalvanicEntry* mate_entry = souxmar_marine::find_galvanic(mating_name);
    if (!self_entry) {
      // The part alloy is not in the series, so there is no potential to
      // difference. Refuse rather than fabricate a couple.
      return souxmar_status_error(
          SOUXMAR_E_INVALID_ARGUMENT,
          "mating_alloy given but `alloy` is not in the galvanic series; use one of "
          "316L, 2507, NAB, CuNi90-10, IN625, Ti6Al4V, AlSi10Mg, CarbonSteel, Cu, "
          "Zn, AlZnIn, Mg, Ni200, Monel400, CuNi70-30, Graphite, Pt");
    }
    if (!mate_entry) {
      return souxmar_status_error(
          SOUXMAR_E_INVALID_ARGUMENT,
          "mating_alloy is not in the galvanic series; use one of 316L, 2507, NAB, "
          "CuNi90-10, IN625, Ti6Al4V, AlSi10Mg, CarbonSteel, Cu, Zn, AlZnIn, Mg, "
          "Ni200, Monel400, CuNi70-30, Graphite, Pt");
    }
    const double delta_e_v = mate_entry->potential_v() - self_entry->potential_v();
    const double drive     = std::abs(delta_e_v) / kGalvanicReferenceDriveV;
    const bool   part_is_anode = delta_e_v > 0.0;  // part is the more ACTIVE metal
    if (part_is_anode) {
      // Oxygen-limited: extra driving force past the reference cannot pull
      // more current than the cathode's oxygen supply delivers.
      const double i_anode = kOxygenLimitedCurrentDensityAPerM2 * f_oxygen *
                             f_transfer * area_ratio * std::min(drive, 1.0);
      galvanic_rate_mm_per_a = i_anode * base.ec_mm_per_year_per_a_m2;
      const double severity  = drive * std::sqrt(area_ratio);
      galvanic_risk          = severity / (1.0 + severity);
    } else {
      // The part is the noble member: it is cathodically protected by the
      // couple. The residual is for hydrogen uptake on the part and for the
      // mating part being consumed (which is usually the real failure).
      galvanic_rate_mm_per_a = 0.0;
      galvanic_risk          = std::min(0.05 * drive, 0.15);
    }
  }

  // ---- pit / crevice initiation indicator -------------------------------
  double pitting_risk = 0.0;
  // A table alloy declares its own family (kAlloys.pren_applicable is false for
  // the copper, aluminium and titanium entries). An unrecognised alloy arrived
  // with an explicit Cr/Mo/N composition, so PREN is taken to apply to it.
  const PittingModel effective_model =
      alloy_known ? base.pitting_model : PittingModel::PrenBased;
  double pit_coeff_mm_per_sqrt_a = kPitCoeffStainlessMmPerSqrtA;
  switch (effective_model) {
    case PittingModel::PrenBased: {
      const double cpt_c = kCptSlopeCPerPren * pren_value + kCptInterceptC;
      // Stagnation fades the critical temperature from CPT toward CCT.
      const double stagnation = std::clamp(
          (kStagnantVelocityMPerS - velocity) / kStagnantVelocityMPerS, 0.0, 1.0);
      const double t_crit_c = cpt_c - kCreviceShiftK * stagnation;
      pitting_risk = logistic((temp_c - t_crit_c) / kInitiationWidthK);
      pit_coeff_mm_per_sqrt_a = kPitCoeffStainlessMmPerSqrtA;
      break;
    }
    case PittingModel::CopperAlloy:
      // PREN does not apply. Copper alloys corrode generally rather than by
      // pitting; the base risk covers selective-phase attack and, when
      // stagnant, sulfide-driven deposit attack.
      pitting_risk = add_share(
          kBaseRiskCopperAlloy,
          0.25 * std::clamp((kStagnantVelocityMPerS - velocity) /
                                kStagnantVelocityMPerS, 0.0, 1.0));
      pit_coeff_mm_per_sqrt_a = kPitCoeffCopperMmPerSqrtA;
      break;
    case PittingModel::Aluminium:
      // Chloride pitting of aluminium is essentially unavoidable unprotected.
      pitting_risk            = kBaseRiskAluminium;
      pit_coeff_mm_per_sqrt_a = kPitCoeffAluminiumMmPerSqrtA;
      break;
    case PittingModel::TitaniumImmune:
      // Titanium does not pit in seawater below ~80 degC.
      pitting_risk            = kBaseRiskTitanium;
      pit_coeff_mm_per_sqrt_a = kPitCoeffTitaniumMmPerSqrtA;
      break;
  }
  if (as_built) pitting_risk = add_share(pitting_risk, kAsBuiltRiskShare);
  if (cp) pitting_risk *= kCathodicProtectionPitFactor;
  pitting_risk *= (1.0 - kCoatingPitCredit * coating);
  pitting_risk = std::clamp(pitting_risk, 0.0, 1.0);

  // ---- total wall penetration -------------------------------------------
  const double protection = (1.0 - coating) * (cp ? kCathodicProtectionResidual : 1.0);
  const double general_mm =
      (uniform_rate_mm_per_a + galvanic_rate_mm_per_a) * years * protection;
  const double pit_mm = pitting_risk * pitting_risk * pit_coeff_mm_per_sqrt_a *
                        std::sqrt(years);

  out->thickness_loss_mm = general_mm + pit_mm;
  out->pitting_risk      = pitting_risk;
  out->galvanic_risk     = std::clamp(galvanic_risk, 0.0, 1.0);
  return souxmar_status_ok();
}

souxmar_status_t corrosion_solve(const souxmar_mesh_t*           mesh,
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

  CorrosionResult result{};
  const souxmar_status_t st = corrosion_evaluate(inputs, &result);
  if (st.code != SOUXMAR_OK) return st;

  souxmar_field_t* field = souxmar_field_new("corrosion", SOUXMAR_FL_CELL,
                                             SOUXMAR_FK_VECTOR, num_cells,
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
    data[c * 3 + 0] = result.thickness_loss_mm;
    data[c * 3 + 1] = result.pitting_risk;
    data[c * 3 + 2] = result.galvanic_risk;
  }
  *out_field = field;
  return souxmar_status_ok();
}

// ---------------------------------------------------------------------------
// writer.marine.qualification_report — helpers
// ---------------------------------------------------------------------------

// FNV-1a 64-bit over an explicit little-endian byte order, so the digest is
// endianness-independent as well as platform-independent. NON-CRYPTOGRAPHIC.
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime       = 1099511628211ULL;

void fnv1a_mix_u64(std::uint64_t* h, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    *h ^= static_cast<std::uint64_t>((value >> (byte * 8)) & 0xFFU);
    *h *= kFnvPrime;
  }
}

void fnv1a_mix_double(std::uint64_t* h, double value) {
  std::uint64_t bits = 0;
  // -0.0 and +0.0 hash differently; that is a content digest doing its job.
  std::memcpy(&bits, &value, sizeof(bits));
  fnv1a_mix_u64(h, bits);
}

struct ComponentStats {
  double min  = 0.0;
  double max  = 0.0;
  double mean = 0.0;
};

// min / max / mean of each component over every location and time step.
std::vector<ComponentStats> component_stats(const souxmar_field_t* field) {
  std::vector<ComponentStats> stats;
  if (!field) return stats;
  const std::size_t comps = souxmar_field_components(field);
  const std::size_t count = souxmar_field_count(field);
  const std::size_t steps = souxmar_field_num_time_steps(field);
  const double*     data  = souxmar_field_data_const(field);
  if (!data || comps == 0 || count == 0 || steps == 0) return stats;
  if (souxmar_field_data_size(field) != comps * count * steps) return stats;

  stats.assign(comps, ComponentStats{});
  std::vector<double> sums(comps, 0.0);
  for (std::size_t c = 0; c < comps; ++c) {
    stats[c].min = data[c];
    stats[c].max = data[c];
  }
  // Index order accumulation — the determinism gate forbids reordering.
  for (std::size_t s = 0; s < steps; ++s) {
    for (std::size_t i = 0; i < count; ++i) {
      for (std::size_t c = 0; c < comps; ++c) {
        const double v = data[(s * count + i) * comps + c];
        if (v < stats[c].min) stats[c].min = v;
        if (v > stats[c].max) stats[c].max = v;
        sums[c] += v;
      }
    }
  }
  const double n = static_cast<double>(count * steps);
  for (std::size_t c = 0; c < comps; ++c) stats[c].mean = sums[c] / n;
  return stats;
}

// Per-step maximum of component 0 — used to lay the hydrostatic load cases
// out as the rows they are.
std::vector<double> per_step_max(const souxmar_field_t* field, std::size_t component) {
  std::vector<double> out;
  if (!field) return out;
  const std::size_t comps = souxmar_field_components(field);
  const std::size_t count = souxmar_field_count(field);
  const std::size_t steps = souxmar_field_num_time_steps(field);
  const double*     data  = souxmar_field_data_const(field);
  if (!data || component >= comps || count == 0 || steps == 0) return out;
  if (souxmar_field_data_size(field) != comps * count * steps) return out;
  out.assign(steps, 0.0);
  for (std::size_t s = 0; s < steps; ++s) {
    double m = data[(s * count) * comps + component];
    for (std::size_t i = 1; i < count; ++i) {
      const double v = data[(s * count + i) * comps + component];
      if (v > m) m = v;
    }
    out[s] = m;
  }
  return out;
}

const char* element_type_name(std::size_t element_type) {
  switch (element_type) {
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
    default:                   return "unknown";
  }
}

const char* field_location_name(std::uint8_t location) {
  switch (location) {
    case SOUXMAR_FL_NODAL:       return "nodal";
    case SOUXMAR_FL_CELL:        return "per-cell";
    case SOUXMAR_FL_FACE:        return "per-face";
    case SOUXMAR_FL_GAUSS_POINT: return "gauss-point";
    default:                     return "unknown";
  }
}

const char* collapse_mode_name(int code) {
  switch (code) {
    case 0:  return "interframe elastic instability";
    case 1:  return "membrane yield";
    case 2:  return "general (long-cylinder) instability";
    case 3:  return "sphere elastic buckling";
    default: return "unrecognised mode code";
  }
}

const char* printability_limit_name(int code) {
  switch (code) {
    case 0:  return "none";
    case 1:  return "overhang";
    case 2:  return "thin wall";
    case 3:  return "build-volume fit";
    case 4:  return "aspect ratio / height";
    case 5:  return "corrosion allowance";
    default: return "unrecognised limiting-factor code";
  }
}

// Criticality rigour class. Documented mapping, not a class rule.
struct RigourClass {
  int         level;    // 3 = most rigour
  const char* label;
  const char* ndt;
  const char* coupons;
  const char* tests;
};

RigourClass rigour_for(const char* application, int criticality, bool redundancy) {
  int base = 2;
  if (ascii_iequals(application, "hull") || ascii_iequals(application, "propulsion")) {
    base = 3;  // pressure boundary / rotating power transmission
  } else if (ascii_iequals(application, "piping") ||
             ascii_iequals(application, "structural")) {
    base = 2;
  } else if (ascii_iequals(application, "non_structural")) {
    base = 1;
  }
  // criticality 1 = highest consequence (see the header comment).
  const int crit_adj = (criticality == 1) ? 1 : ((criticality == 3) ? -1 : 0);
  const int red_adj  = redundancy ? -1 : 0;
  const int level    = std::clamp(base + crit_adj + red_adj, 1, 3);
  switch (level) {
    case 3:
      return {3, "A — highest rigour",
              "100 % volumetric NDT of the load path (CT or RT), plus surface method",
              "witness coupons in every build, in every orientation used",
              "full matrix: tensile (XY + Z), impact, hardness, fatigue, plus "
              "corrosion testing for the service water"};
    case 2:
      return {2, "B — intermediate rigour",
              "volumetric NDT of critical sections, surface method over the whole part",
              "witness coupons per build campaign, in the governing orientation",
              "tensile (XY + Z) and hardness per build campaign"};
    default:
      return {1, "C — lowest rigour",
              "visual and dimensional inspection; surface NDT if a load path exists",
              "coupons per powder lot",
              "tensile per powder lot"};
  }
}

// ---------------------------------------------------------------------------
// writer.marine.qualification_report — renderer
// ---------------------------------------------------------------------------

void render_field_section(std::ostringstream& out, const souxmar_field_t* field) {
  if (!field) {
    out << "No simulation field was supplied to this dossier — the pipeline stage\n"
        << "that produced it was either absent or mesh-only. **This section is empty,\n"
        << "which means the dossier carries no simulation evidence at all.**\n";
    return;
  }
  const char* raw_name = souxmar_field_name(field);
  const std::string name = raw_name ? raw_name : "";
  const std::size_t comps = souxmar_field_components(field);
  const std::size_t count = souxmar_field_count(field);
  const std::size_t steps = souxmar_field_num_time_steps(field);
  const std::vector<ComponentStats> stats = component_stats(field);

  out << "Field `" << (name.empty() ? "(unnamed)" : name) << "` — "
      << field_location_name(souxmar_field_location(field)) << ", " << comps
      << " component(s), " << count << " location(s), " << steps << " time step(s).\n\n";

  if (stats.empty()) {
    out << "The field carries no readable data (zero locations, zero steps, or a\n"
        << "buffer size inconsistent with its own metadata).\n";
    return;
  }

  // Each named branch asserts the component count the contract fixes for that
  // field; a field carrying the right name but the wrong shape falls through to
  // the generic summary rather than reading past the end of `stats`.
  const bool vector_shape = stats.size() >= 3;
  if (name == "hydrostatic_pressure" && !stats.empty()) {
    const std::vector<double> maxima = per_step_max(field, 0);
    out << "Hydrostatic load cases, deepest node in each case:\n\n"
        << "| load case (step) | peak pressure (MPa) | equivalent head of standard "
           "seawater (m at 1025 kg/m^3) |\n"
        << "|---|---|---|\n";
    for (std::size_t s = 0; s < maxima.size(); ++s) {
      // 1025 kg/m^3 * 9.80665 m/s^2 = 10051.8 Pa per metre of head.
      out << "| " << s << " | " << (maxima[s] * 1.0e-6) << " | "
          << (maxima[s] / (1025.0 * 9.80665)) << " |\n";
    }
    out << "\nPressures are magnitudes, compressive on the wetted surface.\n";
  } else if (name == "collapse_margin" && vector_shape) {
    const int mode = static_cast<int>(std::lround(stats[2].min));
    out << "- Governing collapse pressure: " << (stats[0].min * 1.0e-6) << " MPa\n"
        << "- Margin over the factored design pressure: " << stats[1].min << "\n"
        << "- Governing mode: " << collapse_mode_name(mode) << " (code " << mode << ")\n"
        << "\nThis is preliminary sizing arithmetic from `solver.marine.hull_collapse`,\n"
        << "uniform over the mesh. It is not a classification-society collapse\n"
        << "calculation, and general instability of a ring-stiffened hull is NOT\n"
        << "covered by it.\n";
  } else if (name == "corrosion" && vector_shape) {
    out << "- Wall penetration over the service life: " << stats[0].max << " mm\n"
        << "- Pit / crevice initiation indicator: " << stats[1].max << " (0..1)\n"
        << "- Galvanic severity indicator: " << stats[2].max << " (0..1)\n"
        << "\nIndicative literature rates for alloy comparison, not design values.\n";
  } else if (name == "temperature" && !stats.empty()) {
    out << "- Peak nodal temperature: " << stats[0].max << " degC\n"
        << "- Minimum nodal temperature: " << stats[0].min << " degC\n";
  } else if (name == "melt_pool" && vector_shape) {
    out << "- Peak melt-pool depth: " << (stats[0].max * 1.0e6) << " um\n"
        << "- Peak normalised enthalpy: " << stats[1].max << "\n"
        << "- Peak porosity risk: " << stats[2].max << " (0..1)\n";
  } else if (name == "distortion_displacement" && vector_shape) {
    out << "- Displacement range per axis (mm): x [" << (stats[0].min * 1.0e3) << ", "
        << (stats[0].max * 1.0e3) << "], y [" << (stats[1].min * 1.0e3) << ", "
        << (stats[1].max * 1.0e3) << "], z [" << (stats[2].min * 1.0e3) << ", "
        << (stats[2].max * 1.0e3) << "]\n";
  } else if (name == "residual_stress" && vector_shape) {
    out << "- Peak von-Mises-equivalent residual stress: " << (stats[0].max * 1.0e-6)
        << " MPa\n"
        << "- Peak fraction of yield: " << stats[1].max << "\n";
  } else if (name == "interface_temperature" && !stats.empty()) {
    out << "- Interface temperature range: " << stats[0].min << " .. " << stats[0].max
        << " degC\n";
  } else if (name == "bond_quality" && vector_shape) {
    out << "- Minimum degree of healing: " << stats[0].min << " (0..1)\n"
        << "- Minimum Z-strength fraction: " << stats[1].min << "\n"
        << "- Maximum time above Tg: " << stats[2].max << " s\n";
  } else if (name == "overhang" && vector_shape) {
    out << "- Minimum downskin tilt: " << stats[0].min << " deg\n"
        << "- Cells flagged as needing support (mean of the 0/1 flag): " << stats[1].mean
        << "\n"
        << "- Total downskin area sampled per cell: max " << stats[2].max << " m^2\n";
  } else if (name == "printability" && vector_shape) {
    const int worst = static_cast<int>(std::lround(stats[1].max));
    out << "- Minimum printability score: " << stats[0].min << " (0..1)\n"
        << "- Most severe limiting factor code present: " << worst << " ("
        << printability_limit_name(worst) << ")\n"
        << "- Minimum wall-thickness proxy: " << (stats[2].min * 1.0e3) << " mm\n";
  } else if (name == "buildtime" && vector_shape) {
    out << "- Total build time: " << (stats[1].max / 3600.0) << " h\n"
        << "- Largest layer cross-section: " << stats[2].max << " m^2\n";
  } else {
    out << "Unrecognised field name — generic summary:\n\n"
        << "| component | min | max | mean |\n|---|---|---|---|\n";
    for (std::size_t c = 0; c < stats.size(); ++c) {
      out << "| " << c << " | " << stats[c].min << " | " << stats[c].max << " | "
          << stats[c].mean << " |\n";
    }
  }
}

souxmar_status_t qualification_report_write(const souxmar_mesh_t*  mesh,
                                            const souxmar_field_t* field,
                                            const souxmar_value_t* inputs,
                                            void*                  /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer inputs must be a map");
  }
  const souxmar_value_t* path_value = souxmar_value_map_get(inputs, "path");
  if (!path_value || souxmar_value_kind(path_value) != SOUXMAR_VK_STRING) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "writer.marine.qualification_report requires `path: <string>`");
  }
  const char* path = souxmar_value_as_string(path_value);
  if (!path || path[0] == '\0') {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "`path` must not be empty");
  }

  const char* part_name   = read_string(inputs, "part_name", "AM part");
  const char* process     = read_string(inputs, "process", "lpbf");
  const char* alloy       = read_string(inputs, "alloy", "316L");
  const char* application = read_string(inputs, "application", "structural");
  const int   criticality = std::clamp(read_int(inputs, "criticality", 2), 1, 3);
  const double design_depth = std::clamp(read_number(inputs, "design_depth", 300.0),
                                         0.0, 11000.0);
  const double service_life = std::clamp(read_number(inputs, "service_life_years", 25.0),
                                         0.0, 100.0);
  const char* framework  = read_string(inputs, "class_framework", "generic");
  const bool  redundancy = read_bool(inputs, "redundancy", false);

  bool application_known =
      ascii_iequals(application, "hull") || ascii_iequals(application, "propulsion") ||
      ascii_iequals(application, "piping") || ascii_iequals(application, "structural") ||
      ascii_iequals(application, "non_structural");
  if (!application_known) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "application must be hull, propulsion, piping, structural or non_structural");
  }
  const RigourClass rigour = rigour_for(application, criticality, redundancy);

  const souxmar_marine::AlloyEntry* alloy_entry = souxmar_marine::find_alloy(alloy);

  try {
    std::ostringstream out;
    // The default global locale can put a comma in every number and break
    // both determinism and downstream parsing. Pin it.
    out.imbue(std::locale::classic());
    out << std::defaultfloat;
    out.precision(6);

    out << "# Additive-manufacturing qualification dossier (advisory)\n\n";

    // -- disclaimer, first thing anyone reads ------------------------------
    out << "> **ADVISORY ONLY — THIS IS NOT A CLASSIFICATION-SOCIETY DOCUMENT.**\n"
        << ">\n"
        << "> souxmar is not a classification society, a notified body, or a\n"
        << "> certifying authority. Generating this dossier does not qualify,\n"
        << "> approve, certify, or accept the part it describes, and it does not\n"
        << "> demonstrate compliance with any rule, standard or specification.\n"
        << ">\n"
        << "> The checklist below is derived from publicly documented additive-\n"
        << "> manufacturing qualification practice (DNV-ST-B203 *Additive\n"
        << "> manufacturing of metallic parts*; ABS *Guide for Additive\n"
        << "> Manufacturing*; ISO/ASTM 52920 and 52930). It is neither a\n"
        << "> reproduction of those documents nor evidence of conformity with\n"
        << "> them. Read the applicable rule; it governs, this file does not.\n"
        << ">\n"
        << "> No certificate number, approval reference, stamp, surveyor name or\n"
        << "> test result appears anywhere in this file. Those fields are left\n"
        << "> blank on purpose: they belong to the responsible engineer and the\n"
        << "> attending surveyor, and a generator that filled them in would be\n"
        << "> manufacturing evidence.\n\n";

    // -- part identification ----------------------------------------------
    out << "## 1. Part identification (as declared)\n\n"
        << "| field | declared value |\n|---|---|\n"
        << "| Part name | " << part_name << " |\n"
        << "| AM process | " << process << " |\n"
        << "| Alloy | " << alloy;
    if (alloy_entry) {
      out << " (" << alloy_entry->label << ")";
    } else {
      out << " (not in the plugin's alloy table — properties unverified)";
    }
    out << " |\n"
        << "| Application | " << application << " |\n"
        << "| Criticality | " << criticality
        << " (1 = highest consequence of failure, 3 = lowest) |\n"
        << "| Design depth | " << design_depth << " m |\n"
        << "| Service life | " << service_life << " a |\n"
        << "| Redundancy in the installation | " << (redundancy ? "yes" : "no") << " |\n"
        << "| Class framework named by the requester | " << framework
        << " (echoed as declared; souxmar does not verify it) |\n\n"
        << "Every value in this table came from the pipeline input. Nothing in it has\n"
        << "been checked against a drawing, a purchase order or a rule.\n\n";

    // -- criticality rationale --------------------------------------------
    out << "## 2. Criticality-class rationale\n\n"
        << "Qualification rigour class: **" << rigour.label << "** (this plugin's "
        << "level " << rigour.level << " of 3)\n\n"
        << "How that was reached — a documented heuristic in this plugin, not a rule\n"
        << "requirement:\n\n"
        << "1. Application `" << application << "` sets the base rigour: a pressure\n"
        << "   boundary (`hull`) or a power-transmission part (`propulsion`) starts at\n"
        << "   the top, `piping` and `structural` in the middle, `non_structural` at\n"
        << "   the bottom.\n"
        << "2. Declared criticality " << criticality << " "
        << (criticality == 1 ? "raises" : (criticality == 3 ? "lowers" : "does not move"))
        << " it.\n"
        << "3. Redundancy " << (redundancy ? "is declared, which lowers it one step"
                                          : "is not declared, so no credit is taken")
        << ".\n\n"
        << "Evidence depth implied by this class:\n\n"
        << "- NDT: " << rigour.ndt << "\n"
        << "- Witness coupons: " << rigour.coupons << "\n"
        << "- Mechanical testing: " << rigour.tests << "\n\n"
        << "If your class framework assigns this part a different category, that\n"
        << "framework wins. Note also that criticality numbering runs in opposite\n"
        << "directions between published frameworks — check which way yours counts.\n\n";

    // -- evidence checklist ------------------------------------------------
    out << "## 3. Qualification-evidence checklist\n\n"
        << "Unticked by design. souxmar cannot observe any of this, so it cannot tick\n"
        << "anything; each line names the record a reviewer will ask to see.\n\n"
        << "### 3.1 Feedstock traceability\n"
        << "- [ ] Powder / wire lot identification, tied to this build by number\n"
        << "- [ ] Certificate of analysis: chemistry against the alloy specification\n"
        << "- [ ] Particle-size distribution, morphology, flowability (powder)\n"
        << "- [ ] Reuse history: number of reuse cycles, sieving, oxygen/nitrogen pickup\n"
        << "- [ ] Storage and handling record (moisture, contamination control)\n\n"
        << "### 3.2 Machine and process qualification\n"
        << "- [ ] Machine identification, build volume, and the qualification status\n"
        << "      of that specific machine (IQ / OQ / PQ per ISO/ASTM 52930 practice)\n"
        << "- [ ] Frozen parameter set: power, speed, hatch, layer thickness, gas flow,\n"
        << "      preheat — with the change-control record\n"
        << "- [ ] Calibration records: laser/beam power, scanner, bed level, thermal\n"
        << "- [ ] Build log for THIS build, including interruptions and alarms\n"
        << "- [ ] Support strategy and build-plate orientation drawing\n\n"
        << "### 3.3 Witness coupons\n"
        << "- [ ] " << rigour.coupons << "\n"
        << "- [ ] Coupon position map on the build plate\n"
        << "- [ ] Coupons carried through the same post-processing as the part\n\n"
        << "### 3.4 Non-destructive testing\n"
        << "- [ ] Method(s) and coverage: " << rigour.ndt << "\n"
        << "- [ ] Acceptance criteria stated BEFORE inspection, with their source\n"
        << "- [ ] Procedure qualification and inspector certification\n"
        << "- [ ] Detectability statement: smallest defect the chosen method can find\n"
        << "      in this wall thickness and this geometry\n"
        << "- [ ] Internal-geometry access limitation noted where NDT cannot reach\n\n"
        << "### 3.5 Mechanical test matrix\n"
        << "- [ ] " << rigour.tests << "\n"
        << "- [ ] Test standard and specimen geometry for each test\n"
        << "- [ ] Acceptance values with their source (rule, specification or contract)\n\n"
        << "### 3.6 Build-direction property declaration\n"
        << "- [ ] Build direction relative to the part's principal load path, drawn\n"
        << "- [ ] Declared anisotropy: in-plane vs build-direction strength, ductility\n"
        << "      and, where fatigue governs, fatigue strength\n"
        << "- [ ] The knockdown actually used in the analysis, and where it came from\n"
        << "      (`am_anisotropy_knockdown` in `solver.marine.hull_collapse` defaults\n"
        << "      to 0.90, which is a placeholder, not your data)\n\n"
        << "### 3.7 Post-processing record\n"
        << "- [ ] Stress relief / solution anneal: cycle, furnace chart, atmosphere\n"
        << "- [ ] Hot isostatic pressing, if used: pressure, temperature, hold, and\n"
        << "      the statement of what it did and did not close\n"
        << "- [ ] Support removal, machining, surface finish achieved on each face\n"
        << "- [ ] Surface treatment / coating, with its own qualification\n"
        << "- [ ] Final dimensional report against the drawing\n\n"
        << "### 3.8 Marine-service specifics\n"
        << "- [ ] Seawater corrosion assessment for the alloy AND the surface state\n"
        << "      actually delivered (as-built is not machined)\n"
        << "- [ ] Galvanic compatibility of every mating metal in the assembly, with\n"
        << "      area ratios and isolation details\n"
        << "- [ ] Cathodic-protection or coating scheme, and its interaction with the\n"
        << "      part (shielding, hydrogen uptake on the noble member)\n"
        << "- [ ] Pressure test record, if the part is a pressure boundary\n\n";

    // -- simulation evidence ----------------------------------------------
    out << "## 4. Simulation evidence supplied to this dossier\n\n";
    render_field_section(out, field);
    out << "\nSimulation is not qualification evidence on its own. It is an argument\n"
        << "about the design, and it is only as good as the model, the material data\n"
        << "and the validation behind it. Every souxmar marine capability is a\n"
        << "closed-form preliminary model; each one states its own limits in its\n"
        << "source header, and those limits carry into this dossier unchanged.\n\n";

    // -- model identification ---------------------------------------------
    const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
    const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
    std::uint64_t digest = kFnvOffsetBasis;
    double bbox_min[3] = {0.0, 0.0, 0.0};
    double bbox_max[3] = {0.0, 0.0, 0.0};
    std::size_t flat_size = 0;
    const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
    if (coords && flat_size == num_nodes * 3 && num_nodes > 0) {
      for (std::size_t c = 0; c < 3; ++c) {
        bbox_min[c] = coords[c];
        bbox_max[c] = coords[c];
      }
      for (std::size_t i = 0; i < num_nodes; ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
          const double v = coords[i * 3 + c];
          if (v < bbox_min[c]) bbox_min[c] = v;
          if (v > bbox_max[c]) bbox_max[c] = v;
          fnv1a_mix_double(&digest, v);
        }
      }
    }
    // Element histogram + connectivity, both walked in index order.
    std::array<std::size_t, 18> histogram{};
    std::vector<std::uint64_t> cell_nodes;
    for (std::size_t c = 0; c < num_cells; ++c) {
      const std::size_t etype = souxmar_mesh_cell_type(mesh, c);
      if (etype < histogram.size()) ++histogram[etype];
      fnv1a_mix_u64(&digest, static_cast<std::uint64_t>(etype));
      const std::size_t node_count = souxmar_mesh_cell_node_count(mesh, c);
      if (node_count == 0) continue;
      cell_nodes.assign(node_count, 0);
      if (souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), node_count).code !=
          SOUXMAR_OK) {
        continue;
      }
      for (std::size_t k = 0; k < node_count; ++k) fnv1a_mix_u64(&digest, cell_nodes[k]);
    }

    out << "## 5. Analysis-model identification\n\n"
        << "| property | value |\n|---|---|\n"
        << "| Nodes | " << num_nodes << " |\n"
        << "| Cells | " << num_cells << " |\n"
        << "| Bounding box min (m) | " << bbox_min[0] << ", " << bbox_min[1] << ", "
        << bbox_min[2] << " |\n"
        << "| Bounding box max (m) | " << bbox_max[0] << ", " << bbox_max[1] << ", "
        << bbox_max[2] << " |\n";
    out << "| Element types |";
    bool first = true;
    for (std::size_t t = 0; t < histogram.size(); ++t) {
      if (histogram[t] == 0) continue;
      out << (first ? " " : ", ") << element_type_name(t) << " x " << histogram[t];
      first = false;
    }
    if (first) out << " none";
    out << " |\n";
    out << "| Content digest | FNV-1a-64 `0x" << std::hex << digest << std::dec
        << "` |\n\n"
        << "The digest covers node coordinates, cell types and connectivity, mixed in\n"
        << "index order with an explicit little-endian byte order, so it is identical\n"
        << "on every platform. It is a NON-CRYPTOGRAPHIC content hash: it catches an\n"
        << "accidentally different mesh, and it is not a signature, not tamper-evident\n"
        << "and not an identity.\n\n";

    // -- what this does not establish -------------------------------------
    out << "## 6. What this document does NOT establish\n\n"
        << "- That the part is fit for service, at any depth, for any duration.\n"
        << "- That any rule, class notation, standard or specification is satisfied.\n"
        << "- That the evidence in section 3 exists. Nothing above is ticked.\n"
        << "- That the simulation in section 4 is validated against a test.\n"
        << "- That the alloy data used anywhere in this pipeline matches the metal\n"
        << "  that was actually built.\n"
        << "- General instability of a ring-stiffened pressure hull, which souxmar's\n"
        << "  `solver.marine.hull_collapse` does not evaluate at all.\n"
        << "- Anything about welds, joints, seals, penetrations or fasteners.\n\n";

    // -- references --------------------------------------------------------
    out << "## 7. Where the checklist structure comes from\n\n"
        << "Publicly documented practice, cited so you can go and read the real thing:\n\n"
        << "- DNV-ST-B203, *Additive manufacturing of metallic parts* — part\n"
        << "  criticality categories, feedstock and machine qualification, coupon and\n"
        << "  NDT expectations.\n"
        << "- ABS, *Guide for Additive Manufacturing* — marine and offshore AM\n"
        << "  qualification pathway.\n"
        << "- ISO/ASTM 52920, *Additive manufacturing — Qualification principles —\n"
        << "  Requirements for industrial additive manufacturing processes and\n"
        << "  production sites*.\n"
        << "- ISO/ASTM 52930, *Additive manufacturing — Qualification principles —\n"
        << "  Installation, operation and performance (IQ/OQ/PQ) of PBF-LB equipment*.\n"
        << "- ASTM F3122, *Guide for Evaluating Mechanical Properties of Metal\n"
        << "  Materials Made via Additive Manufacturing Processes*.\n"
        << "- Lloyd's Register, *Guidance Notes for Additive Manufacturing*.\n\n"
        << "Citing a document is not complying with it. souxmar has no relationship\n"
        << "with any of these organisations.\n";

    const std::string text = out.str();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      return souxmar_status_error(SOUXMAR_E_IO, "could not open `path` for writing");
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
      return souxmar_status_error(SOUXMAR_E_IO, "write to `path` failed");
    }
  } catch (...) {
    // No exception may cross a plugin entry point.
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "caught an exception while rendering the dossier "
                                "(most likely an allocation failure)");
  }
  return souxmar_status_ok();
}

}  // namespace

namespace souxmar_marine {

const souxmar_solver_vtable_t kCorrosionVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &corrosion_solve,
    nullptr,
};

const souxmar_writer_vtable_t kQualificationReportVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &qualification_report_write,
    nullptr,
};

}  // namespace souxmar_marine
