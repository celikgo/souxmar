// SPDX-License-Identifier: Apache-2.0
//
// marine-data — static reference tables + shared input-bag helpers for the
// `marine` example plugin (dev.souxmar.examples.marine).
//
// The plugin ships four capabilities across two translation units:
//
//   marine_loads.cpp      solver.marine.hydrostatic
//                         solver.marine.hull_collapse
//                         + the single souxmar_plugin_register_v1
//   marine_integrity.cpp  solver.marine.corrosion
//                         writer.marine.qualification_report
//
// This header carries what both need: the souxmar_value_t readers, the
// seawater equation of state, the galvanic series, the alloy table, and the
// two extern vtable declarations that let the register function in
// marine_loads.cpp publish capabilities implemented in marine_integrity.cpp
// — so the shared object exports exactly one symbol.
//
// What it computes:
//   * seawater_density_kg_per_m3(S, T) — the one-atmosphere International
//     Equation of State of Seawater (EOS-80), i.e. the Millero & Poisson
//     (1981) polynomial reproduced in UNESCO Technical Paper in Marine
//     Science 44 (1983), §3. Valid 0 <= S <= 42 PSU, -2 <= T <= 40 degC at
//     1 atm; the published standard error of the fit is 3.6e-3 kg/m^3.
//     rho(35 PSU, 10 degC) = 1026.95 kg/m^3.
//   * pren(Cr, Mo, N) = %Cr + 3.3*%Mo + 16*%N — the pitting-resistance
//     equivalent number (the "PREN-16N" variant used throughout the duplex
//     stainless literature, e.g. IMOA "Practical Guidelines for the
//     Fabrication of Duplex Stainless Steels", 3rd ed., §2).
//   * kGalvanicSeries — corrosion potentials in *flowing* seawater
//     (2.4-4.0 m/s, 10-27 degC) versus a saturated Ag/AgCl reference, taken
//     as the mid-point of each published band. Source: F. L. LaQue, "Marine
//     Corrosion: Causes and Prevention", Wiley 1975, the galvanic-series
//     chart reproduced in ASM Handbook Vol. 13A ("Galvanic Corrosion") and
//     in MIL-STD-889C Table I. Sorted most-active-first; the table order IS
//     the galvanic series.
//   * kAlloys — composition + indicative seawater corrosion behaviour for
//     the seven alloys named in the marine capability contract. Sorted by
//     ASCII key so lookups and any listing are order-stable.
//
// What this is NOT: a materials database, and not a source of design values.
//   * Every corrosion rate here is an indicative literature value for
//     *comparison between alloys*. Real design allowances come from a
//     project corrosion assessment, and pitting/crevice behaviour in
//     seawater is dominated by microbiology, deposits, weld metallurgy and
//     crevice geometry, none of which are inputs.
//   * The galvanic-series numbers are band mid-points. Real couples shift
//     with flow, temperature, biofilm, pollution (sulfide) and passive-film
//     history; a stainless steel that goes active moves ~0.4 V negative and
//     changes the sign of the couple.
//   * The seawater equation of state is the one-atmosphere form: it ignores
//     the in-situ compression of the water column, which raises density by
//     roughly 0.5 % per 1000 m of depth.
//   * PREN is only defined for Fe/Ni-Cr-Mo-N alloys. For the copper,
//     aluminium and titanium entries `pren_applicable` is false and the
//     number must not be quoted.
//
// Determinism: every table is `inline constexpr`, every lookup is a linear
// scan in table order with ASCII-only case folding (no std::tolower, which
// is locale-dependent), and no function here touches the environment, the
// clock or an allocator.

#ifndef SOUXMAR_EXAMPLES_MARINE_DATA_HPP
#define SOUXMAR_EXAMPLES_MARINE_DATA_HPP

#include <array>
#include <cmath>
#include <cstddef>

#include "souxmar-c/abi.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/value.h"
#include "souxmar-c/writer.h"

namespace souxmar_marine {

// ---------------------------------------------------------------------------
// Value-bag readers. read_number / read_int are copied verbatim from
// examples/plugins/modal-stub/modal_stub.cpp; read_bool / read_string /
// read_vec3 follow the same shape (missing key or wrong kind => default).
// ---------------------------------------------------------------------------

inline double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

inline int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

inline bool read_bool(const souxmar_value_t* inputs, const char* key, bool dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v) return dv;
  if (souxmar_value_kind(v) == SOUXMAR_VK_BOOL) return souxmar_value_as_bool(v) != 0;
  // A YAML `1` / `0` is a Number, not a Bool — accept it rather than
  // silently falling back to the default.
  if (souxmar_value_kind(v) == SOUXMAR_VK_NUMBER) return souxmar_value_as_number(v) != 0.0;
  return dv;
}

// Returns the borrowed string, or `dv` (which must itself have static
// storage duration) when the key is missing / not a string / empty.
inline const char* read_string(const souxmar_value_t* inputs, const char* key,
                              const char* dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_STRING) return dv;
  const char* s = souxmar_value_as_string(v);
  if (!s || s[0] == '\0') return dv;
  return s;
}

// Reads a 3-number list. Leaves `out` untouched and returns false unless the
// value is a list of exactly three numbers.
inline bool read_vec3(const souxmar_value_t* inputs, const char* key,
                      std::array<double, 3>* out) {
  if (!out || !inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return false;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return false;
  if (souxmar_value_list_size(v) != 3) return false;
  std::array<double, 3> tmp{};
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return false;
    tmp[i] = souxmar_value_as_number(c);
  }
  *out = tmp;
  return true;
}

// ---------------------------------------------------------------------------
// ASCII-only helpers. std::tolower / std::strcmp under a non-C locale are a
// determinism hazard (LC_CTYPE / LC_COLLATE), so key matching is spelled out.
// ---------------------------------------------------------------------------

constexpr char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

constexpr int ascii_cmp(const char* a, const char* b) {
  if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
  while (*a != '\0' && *a == *b) { ++a; ++b; }
  return static_cast<int>(static_cast<unsigned char>(*a)) -
         static_cast<int>(static_cast<unsigned char>(*b));
}

constexpr bool ascii_iequals(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a != '\0' && *b != '\0' && ascii_lower(*a) == ascii_lower(*b)) { ++a; ++b; }
  return *a == '\0' && *b == '\0';
}

// ---------------------------------------------------------------------------
// Seawater equation of state (EOS-80, one atmosphere).
// ---------------------------------------------------------------------------

// Validity band of the Millero & Poisson (1981) fit; inputs are clamped to
// it so a nonsense YAML value cannot produce a negative density.
inline constexpr double kSalinityMinPsu     = 0.0;
inline constexpr double kSalinityMaxPsu     = 42.0;
inline constexpr double kSeaTemperatureMinC = -2.0;
inline constexpr double kSeaTemperatureMaxC = 40.0;

// Reference seawater used for every "reference environment" rate below.
inline constexpr double kRefSalinityPsu      = 35.0;   // PSU, open ocean
inline constexpr double kRefTemperatureC     = 10.0;   // degC, temperate
inline constexpr double kRefOxygenMgPerL     = 8.0;    // mg/L, aerated
inline constexpr double kRefVelocityMPerS    = 0.5;    // m/s

inline double seawater_density_kg_per_m3(double salinity_psu, double temperature_c) {
  const double s = (salinity_psu < kSalinityMinPsu)
                       ? kSalinityMinPsu
                       : (salinity_psu > kSalinityMaxPsu ? kSalinityMaxPsu : salinity_psu);
  const double t = (temperature_c < kSeaTemperatureMinC)
                       ? kSeaTemperatureMinC
                       : (temperature_c > kSeaTemperatureMaxC ? kSeaTemperatureMaxC
                                                              : temperature_c);
  // Pure-water density, kg/m^3 (UNESCO 44 eq. in §3, coefficients verbatim).
  const double rho_w = 999.842594 + 6.793952e-2 * t - 9.095290e-3 * t * t +
                       1.001685e-4 * t * t * t - 1.120083e-6 * t * t * t * t +
                       6.536332e-9 * t * t * t * t * t;
  // Salinity terms: A*S + B*S^1.5 + C*S^2.
  const double a = 0.824493 - 4.0899e-3 * t + 7.6438e-5 * t * t -
                   8.2467e-7 * t * t * t + 5.3875e-9 * t * t * t * t;
  const double b = -5.72466e-3 + 1.0227e-4 * t - 1.6546e-6 * t * t;
  const double c = 4.8314e-4;
  return rho_w + a * s + b * s * std::sqrt(s) + c * s * s;
}

// ---------------------------------------------------------------------------
// Galvanic series — flowing seawater, 2.4-4.0 m/s, 10-27 degC,
// V versus saturated Ag/AgCl. Sorted most-active (most negative) first.
// ---------------------------------------------------------------------------

struct GalvanicEntry {
  const char* key;                 // canonical key (matched case-insensitively)
  const char* label;               // human label for the qualification report
  double      e_active_v;          // active end of the published band
  double      e_noble_v;           // noble end of the published band
  const char* aliases[3];          // extra accepted spellings, nullptr-padded

  constexpr double potential_v() const { return 0.5 * (e_active_v + e_noble_v); }
};

inline constexpr std::array<GalvanicEntry, 17> kGalvanicSeries = {{
    {"Mg",         "magnesium alloy (sacrificial anode)", -1.67, -1.60,
     {"magnesium", nullptr, nullptr}},
    {"AlZnIn",     "Al-Zn-In sacrificial anode",          -1.10, -1.05,
     {"al-anode", "aluminium-anode", nullptr}},
    {"Zn",         "zinc (sacrificial anode)",            -1.05, -0.98,
     {"zinc", nullptr, nullptr}},
    // The aluminium entry covers both Al-Si cast (AlSi10Mg) and 5xxx wrought
    // hulls; published bands for the two overlap within ~0.05 V.
    {"AlSi10Mg",   "aluminium alloy (Al-Si cast / 5xxx)", -0.85, -0.75,
     {"al", "aluminium", "aluminum"}},
    {"CarbonSteel", "carbon / low-alloy steel, cast iron", -0.71, -0.60,
     {"steel", "mild-steel", "cast-iron"}},
    {"Cu",         "copper",                              -0.36, -0.31,
     {"copper", nullptr, nullptr}},
    {"NAB",        "nickel-aluminium bronze (C95800)",    -0.35, -0.25,
     {"c95800", "ni-al-bronze", "nialbronze"}},
    {"CuNi90-10",  "90-10 copper-nickel (C70600)",        -0.28, -0.20,
     {"c70600", "cuni9010", "cuni90/10"}},
    {"CuNi70-30",  "70-30 copper-nickel (C71500)",        -0.25, -0.20,
     {"c71500", "cuni7030", "cuni70/30"}},
    {"Ni200",      "nickel 200 (passive)",                -0.20, -0.10,
     {"nickel", nullptr, nullptr}},
    {"Monel400",   "Ni-Cu alloy 400 (passive)",           -0.14, -0.04,
     {"monel", "alloy400", "n04400"}},
    {"316L",       "316L austenitic stainless (passive)", -0.10, -0.05,
     {"316", "s31603", "1.4404"}},
    {"2507",       "2507 super-duplex stainless (passive)", -0.08, -0.03,
     {"s32750", "sdss", "super-duplex"}},
    {"IN625",      "Ni-Cr-Mo alloy 625 (passive)",        -0.10, 0.05,
     {"inconel625", "alloy625", "n06625"}},
    {"Ti6Al4V",    "titanium grade 5 (passive)",          -0.05, 0.05,
     {"ti-6al-4v", "titanium", "grade5"}},
    {"Graphite",   "graphite / carbon-fibre composite",    0.20, 0.30,
     {"cfrp", "carbon-fibre", "carbon-fiber"}},
    {"Pt",         "platinum",                             0.25, 0.35,
     {"platinum", nullptr, nullptr}},
}};

// The table order is load-bearing documentation (it *is* the series), so the
// ordering is asserted at compile time rather than trusted.
constexpr bool galvanic_series_is_sorted() {
  for (std::size_t i = 1; i < kGalvanicSeries.size(); ++i) {
    if (kGalvanicSeries[i - 1].potential_v() > kGalvanicSeries[i].potential_v()) {
      return false;
    }
  }
  return true;
}
static_assert(galvanic_series_is_sorted(),
              "kGalvanicSeries must stay sorted most-active-first");

inline const GalvanicEntry* find_galvanic(const char* name) {
  if (!name || name[0] == '\0') return nullptr;
  for (std::size_t i = 0; i < kGalvanicSeries.size(); ++i) {
    const GalvanicEntry& e = kGalvanicSeries[i];
    if (ascii_iequals(e.key, name)) return &e;
    for (std::size_t a = 0; a < 3; ++a) {
      if (e.aliases[a] && ascii_iequals(e.aliases[a], name)) return &e;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Alloy table.
// ---------------------------------------------------------------------------

// How the alloy loses metal locally. PREN only orders the Fe/Ni-Cr-Mo-N
// alloys; copper, aluminium and titanium need their own statement.
enum class PittingModel : int {
  PrenBased      = 0,  // stainless / Ni-Cr-Mo: CPT correlation vs PREN
  CopperAlloy    = 1,  // general + selective-phase + deposit attack
  Aluminium      = 2,  // chloride pitting, essentially unavoidable
  TitaniumImmune = 3,  // no chloride pitting below ~80 degC in seawater
};

struct AlloyEntry {
  const char*  key;
  const char*  label;
  const char*  galvanic_key;    // matching entry in kGalvanicSeries

  double       chromium_pct;    // wt%, mid-specification
  double       molybdenum_pct;  // wt%
  double       nitrogen_pct;    // wt%
  bool         pren_applicable;

  // Uniform (general) corrosion rate in the reference environment defined
  // above: 35 PSU, 10 degC, 8 mg/L O2, 0.5 m/s, no galvanic couple,
  // machined surface. mm/year.
  double       uniform_rate_mm_per_year;

  // Onset of erosion-corrosion / impingement attack. Copper alloys have a
  // genuine design limit here (Efird, Corrosion 33(1), 1977); for the
  // passive stainless / Ni / Ti alloys no practical seawater limit exists,
  // so the sentinel 1e3 m/s disables the term.
  double       critical_velocity_m_s;

  // Faraday thickness-loss coefficient: mm/year of wall per A/m^2 of anodic
  // current density, = M / (n * F * rho) * 3.15576e7 s/a * 1000 mm/m.
  // Iron n=2 gives the textbook 1.16 mm/a per A/m^2.
  double       ec_mm_per_year_per_a_m2;

  PittingModel pitting_model;
  const char*  aliases[3];
};

// Sorted by ASCII key. Sources for the uniform rates and velocity limits:
//   * F. L. LaQue, "Marine Corrosion: Causes and Prevention", Wiley 1975.
//   * ASM Handbook Vol. 13C, "Corrosion in Seawater" / "Corrosion of
//     Stainless Steels in Marine Environments".
//   * Copper Development Association Pub. 106, "Copper Nickel Alloys:
//     Properties and Applications" (90-10 and NAB seawater rates,
//     critical velocities).
//   * Compositions: ASTM A240 (316L), ASTM A790/UNS S32750 (2507),
//     ASTM B148 C95800 (NAB), ASTM B265 Gr.5 (Ti-6Al-4V), UNS N06625,
//     ASTM B26/EN 1706 AlSi10Mg, ASTM B466 C70600.
inline constexpr std::array<AlloyEntry, 7> kAlloys = {{
    {"2507", "2507 super-duplex stainless steel", "2507",
     25.0, 3.8, 0.27, true,
     0.001, 1.0e3, 1.16, PittingModel::PrenBased,
     {"s32750", "super-duplex", "sdss"}},
    {"316L", "316L austenitic stainless steel", "316L",
     16.9, 2.4, 0.045, true,
     0.003, 1.0e3, 1.16, PittingModel::PrenBased,
     {"316", "s31603", "1.4404"}},
    {"AlSi10Mg", "AlSi10Mg cast aluminium", "AlSi10Mg",
     0.0, 0.0, 0.0, false,
     0.020, 2.0, 1.09, PittingModel::Aluminium,
     {"al", "aluminium", "aluminum"}},
    {"CuNi90-10", "90-10 copper-nickel", "CuNi90-10",
     0.0, 0.0, 0.0, false,
     0.025, 3.5, 2.32, PittingModel::CopperAlloy,
     {"c70600", "cuni9010", "cuni90/10"}},
    {"IN625", "Ni-Cr-Mo alloy 625", "IN625",
     21.5, 9.0, 0.0, true,
     0.0005, 1.0e3, 1.14, PittingModel::PrenBased,
     {"inconel625", "alloy625", "n06625"}},
    {"NAB", "nickel-aluminium bronze C95800", "NAB",
     0.0, 0.0, 0.0, false,
     0.030, 4.3, 2.32, PittingModel::CopperAlloy,
     {"c95800", "ni-al-bronze", "nialbronze"}},
    {"Ti6Al4V", "Ti-6Al-4V (grade 5)", "Ti6Al4V",
     0.0, 0.0, 0.015, false,
     0.0002, 1.0e3, 0.87, PittingModel::TitaniumImmune,
     {"ti-6al-4v", "titanium", "grade5"}},
}};

constexpr bool alloys_are_sorted() {
  for (std::size_t i = 1; i < kAlloys.size(); ++i) {
    if (ascii_cmp(kAlloys[i - 1].key, kAlloys[i].key) >= 0) return false;
  }
  return true;
}
static_assert(alloys_are_sorted(), "kAlloys must stay sorted by ASCII key");

inline const AlloyEntry* find_alloy(const char* name) {
  if (!name || name[0] == '\0') return nullptr;
  for (std::size_t i = 0; i < kAlloys.size(); ++i) {
    const AlloyEntry& e = kAlloys[i];
    if (ascii_iequals(e.key, name)) return &e;
    for (std::size_t a = 0; a < 3; ++a) {
      if (e.aliases[a] && ascii_iequals(e.aliases[a], name)) return &e;
    }
  }
  return nullptr;
}

// The default alloy, used verbatim when `alloy` is unknown: composition
// falls back to the explicit chromium_pct / molybdenum_pct / nitrogen_pct
// inputs (whose defaults are these numbers) per contract §3.17.
inline const AlloyEntry& default_alloy() { return kAlloys[1]; }  // 316L

// PREN = %Cr + 3.3*%Mo + 16*%N. Only meaningful for Fe/Ni-Cr-Mo-N alloys.
inline double pren(double chromium_pct, double molybdenum_pct, double nitrogen_pct) {
  return chromium_pct + 3.3 * molybdenum_pct + 16.0 * nitrogen_pct;
}

// ---------------------------------------------------------------------------
// Cross-translation-unit capability wiring. Defined in marine_integrity.cpp,
// registered by souxmar_plugin_register_v1 in marine_loads.cpp so the shared
// object exports exactly one symbol.
// ---------------------------------------------------------------------------

extern const souxmar_solver_vtable_t kCorrosionVtable;
extern const souxmar_writer_vtable_t kQualificationReportVtable;

}  // namespace souxmar_marine

#endif  // SOUXMAR_EXAMPLES_MARINE_DATA_HPP
