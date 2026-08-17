// SPDX-License-Identifier: Apache-2.0
//
// Tool: propose_am_setup
//
// Additive-manufacturing planner (tool 19 of the ADR-0010 additive
// ratchet). From {process, material, machine} it returns a *complete,
// runnable* pipeline proposal — a stage list with plugin ids and input
// maps — for the AM / marine capability set frozen in the manufacturing
// capability contract (18 ids; see docs/MANUFACTURING.md and
// docs/MARINE.md).
//
// Two hard rules this file enforces mechanically:
//   1. Every plugin id it emits is checked against kKnownCapabilities
//      below. A stage naming an id outside that list is a bug in this
//      file, not a user error, and is surfaced as INTERNAL rather than
//      handed to the model — the planner must never invent capability
//      ids the host cannot dispatch.
//   2. `postproc.*` stages always carry `field: {from: <stage>}`. The
//      registry dispatcher treats a missing `field` on a postproc stage
//      as a dispatch error (not a NULL), so a proposal without it would
//      be un-runnable.
//
// Confirmation::Auto. The confirmation tiers in tool.h are
// {Auto, ConfirmOnce, ConfirmAlways}; the contract's `Confirmation::None`
// for this tool maps onto `Auto` (the no-prompt tier). Justification: the
// planner runs no solver, writes no file, and reaches no network. It does
// stage its own output under session_state["manufacturing"] so
// estimate_build_cost / set_build_orientation can consume it, but that
// write is idempotent (the same input produces the same key) and additive
// (no other session key is touched), which is the same reasoning that
// keeps `propose_pipeline` at Auto.
//
// What this is NOT: it is not a process-parameter optimiser. The machine
// numbers are representative published machine-class values and the
// material rows are indicative literature properties — every one carries
// its source in a comment. They are a defensible starting point for a
// simulation, not qualified process parameters for a build.

#include "souxmar/ai/tool.h"

#include <cstddef>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

// ---------------------------------------------------------------------
// The frozen capability set. Anything this planner emits must be here.
// Sorted for readability; membership is a linear scan over 18 entries.
// ---------------------------------------------------------------------
constexpr std::string_view kKnownCapabilities[] = {
    "mesher.am.layered",
    "postproc.am.bond_strength",
    "postproc.am.melt_pool",
    "postproc.am.residual_stress",
    "reader.lattice",
    "solver.am.buildtime",
    "solver.am.distortion.inherent_strain",
    "solver.am.overhang",
    "solver.am.polymer.fff",
    "solver.am.printability",
    "solver.am.thermal.lpbf",
    "solver.marine.corrosion",
    "solver.marine.hull_collapse",
    "solver.marine.hydrostatic",
    "writer.am.cli",
    "writer.am.gcode",
    "writer.am.report",
    "writer.marine.qualification_report",
};

bool is_known_capability(std::string_view id) {
  for (const auto& known : kKnownCapabilities) {
    if (known == id)
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------
// Material table. Mirror of examples/materials/am-marine.toml — kept
// static here so the planner works with no project on disk.
//
// Every number is an indicative literature / datasheet value for the
// *as-built* AM condition where one is published, and a wrought or cast
// value otherwise. Sources are named per row. These are NOT design
// allowables: AM property scatter with build direction, machine, and
// post-processing routinely exceeds 20 %.
// ---------------------------------------------------------------------
struct AmMaterial {
  std::string_view id;
  std::string_view family;   // steel | titanium | nickel | aluminium | bronze | polymer
  bool polymer;              // routes the process-family compatibility check
  double density;            // kg/m^3
  double thermal_conductivity;  // W/(m*K) at room temperature
  double specific_heat;      // J/(kg*K)
  double melt_temperature;   // deg C — liquidus (metals) / melt peak (polymers)
  double glass_transition;   // deg C — polymers only, 0.0 for metals
  double print_temperature;  // deg C — FFF nozzle / SLS chamber, 0.0 for metals
  double bed_temperature;    // deg C — polymers only, 0.0 for metals
  double youngs_modulus;     // Pa
  double poisson_ratio;      // -
  double cte;                // 1/K
  double yield_strength;     // Pa
  double absorptivity;       // - at 1064 nm (metals) / IR (polymers)
  double cost_per_kg;        // currency units per kg of feedstock
  double chromium_pct;       // % — corrosion PREN inputs, 0.0 when not alloyed
  double molybdenum_pct;     // %
  double nitrogen_pct;       // %
};

constexpr AmMaterial kMaterials[] = {
    // 316L austenitic stainless. rho/E/nu/cte/k/cp: ASM Handbook Vol. 1
    // (AISI 316L) + ASTM F3184 (LPBF 316L); liquidus ~1400 C (solidus
    // 1375 C); yield 500 MPa is the mid-band of published as-built LPBF
    // 316L (450-550 MPa, higher than the 205 MPa wrought minimum because
    // of the fine LPBF cell structure); absorptivity 0.35 for 1064 nm on
    // a powder bed (Trapp et al. 2017, Appl. Mater. Today).
    {"316L", "steel", false, 7990.0, 15.0, 500.0, 1400.0, 0.0, 0.0, 0.0, 1.90e11, 0.28, 1.60e-5,
     5.0e8, 0.35, 90.0, 17.0, 2.5, 0.05},
    // Ti-6Al-4V grade 23. rho/E/nu/k/cp/cte: ASM Handbook Vol. 2 +
    // ASTM F3001; liquidus 1660 C; yield 900 MPa is mid-band for
    // stress-relieved LPBF (as-built 950-1100, wrought minimum 828);
    // absorptivity 0.40 (Trapp et al. 2017).
    {"Ti6Al4V", "titanium", false, 4430.0, 6.7, 560.0, 1650.0, 0.0, 0.0, 0.0, 1.14e11, 0.342,
     8.60e-6, 9.0e8, 0.40, 350.0, 0.0, 0.0, 0.0},
    // INCONEL 625. rho/E/nu/k/cp/cte: Special Metals INCONEL alloy 625
    // datasheet (Pub. SMC-063); liquidus 1350 C (solidus 1290 C); yield
    // 490 MPa = annealed minimum (as-built LPBF runs 600-800 MPa);
    // absorptivity 0.36 by analogy with Ni-base powder beds.
    {"IN625", "nickel", false, 8440.0, 9.8, 410.0, 1350.0, 0.0, 0.0, 0.0, 2.05e11, 0.278, 1.28e-5,
     4.9e8, 0.36, 120.0, 21.5, 9.0, 0.0},
    // AlSi10Mg. rho/E/nu/cte: EOS Aluminium AlSi10Mg material datasheet;
    // k = 120 W/(m*K) as-built (cast AlSi10Mg is ~150); cp 910; liquidus
    // 596 C (solidus 557 C) per the Al-Si phase diagram; yield 240 MPa
    // as-built EOS datasheet; absorptivity 0.20 — Al is strongly
    // reflective at 1064 nm, which is why LPBF Al needs high power.
    {"AlSi10Mg", "aluminium", false, 2670.0, 120.0, 910.0, 600.0, 0.0, 0.0, 0.0, 7.0e10, 0.33,
     2.10e-5, 2.4e8, 0.20, 60.0, 0.0, 0.0, 0.0},
    // Nickel-aluminium bronze (CuAl10Ni5Fe4, ASTM B148 C95800) — the
    // classic marine propeller alloy. rho/E/nu/k/cp/cte: Copper
    // Development Association Pub. 82 (aluminium bronzes); liquidus
    // 1060 C; yield 240 MPa = C95800 cast minimum; absorptivity 0.25 —
    // Cu alloys are reflective at 1064 nm (hence green-laser LPBF).
    {"NAB", "bronze", false, 7580.0, 42.0, 420.0, 1050.0, 0.0, 0.0, 0.0, 1.20e11, 0.32, 1.62e-5,
     2.7e8, 0.25, 40.0, 0.0, 0.0, 0.0},
    // Super-duplex 2507 (UNS S32750). rho/E/nu/k/cp/cte + composition:
    // Outokumpu 2507 datasheet / ASTM A240 S32750; liquidus ~1400 C;
    // yield 550 MPa = A240 minimum. Composition gives
    // PREN = 25 + 3.3*3.8 + 16*0.27 = 41.9, the published ~42 for 2507.
    {"2507", "steel", false, 7800.0, 15.0, 480.0, 1400.0, 0.0, 0.0, 0.0, 2.00e11, 0.30, 1.30e-5,
     5.5e8, 0.35, 110.0, 25.0, 3.8, 0.27},
    // Carbon-fibre-filled PA12. rho/E/strength: EOS PA 2200 / carbon-
    // filled PA12 datasheets; Tm 180 C, Tg 50 C (PA12 semi-crystalline);
    // FFF nozzle 260 C, bed 100 C; k 0.35, cp 1600 (polyamide typical);
    // cte 4.0e-5 (fibre-direction dependent — this is the through-plane
    // value); absorptivity 0.90 for CO2/IR (carbon black loading).
    {"PA12-CF", "polymer", true, 1050.0, 0.35, 1600.0, 180.0, 50.0, 260.0, 100.0, 6.0e9, 0.39,
     4.00e-5, 7.0e7, 0.90, 100.0, 0.0, 0.0, 0.0},
    // PEKK (poly-ether-ketone-ketone). rho/E/strength/Tg: Arkema Kepstan
    // 6000/7000 series datasheets + Victrex PAEK processing notes; Tm
    // 360 C, Tg 162 C; nozzle 380 C, bed/chamber 130 C; k 0.25, cp 1800
    // (PAEK typical); cte 4.7e-5; absorptivity 0.90 (IR).
    {"PEKK", "polymer", true, 1300.0, 0.25, 1800.0, 360.0, 162.0, 380.0, 130.0, 4.0e9, 0.40,
     4.70e-5, 1.0e8, 0.90, 700.0, 0.0, 0.0, 0.0},
};

const AmMaterial* find_material(std::string_view id) {
  for (const auto& m : kMaterials) {
    if (m.id == id)
      return &m;
  }
  return nullptr;
}

std::string material_id_list() {
  std::string out;
  for (const auto& m : kMaterials) {
    if (!out.empty())
      out += " / ";
    out += std::string(m.id);
  }
  return out;
}

// ---------------------------------------------------------------------
// Machine presets. Representative of a machine *class*, not a specific
// serial number: an SLM-280-class 400 W LPBF system, a 0.4 mm-nozzle
// desktop/industrial FFF system, a robot arc-DED cell, and a
// polymer-powder-bed (SLS) system. Rates are indicative service-bureau
// hourly rates in unspecified currency units.
// ---------------------------------------------------------------------
struct MachinePreset {
  double laser_power;            // W (arc power for ded_waam)
  double scan_speed;             // m/s (powder-bed laser scan speed)
  double hatch_spacing;          // m
  double layer_height;           // m — the *simulation* layer (see contract 2.2)
  double process_layer_height;   // m — the physical machine layer
  double recoat_time;            // s per machine layer
  double print_speed;            // m/s — FFF extrusion feed rate
  double road_width;             // m — FFF road / SLS hatch
  double travel_speed;           // m/s — FFF non-printing move
  double filament_diameter;      // m
  double deposition_rate;        // kg/h — DED only
  double machine_power_overhead; // W — chiller/gas/heaters while building
  double machine_rate_per_hour;  // currency units per machine hour
  double build_volume[3];        // m
  double interlayer_time;        // s — powder-bed layer-to-layer dwell
  double layer_time;             // s — FFF per-layer time (bond model input)
};

// LPBF: SLM-280 / EOS M290 class. 400 W fibre laser run at 200 W with
// 0.8 m/s and 110 um hatch — the widely published 316L "nominal" recipe
// (Mukherjee et al. 2017); 30 um layers; ~8 s recoat.
constexpr MachinePreset kLpbf{200.0,  0.8,    1.1e-4, 0.001,  3.0e-5, 8.0,  0.0,
                              0.0,    0.0,    0.0,    0.0,    2500.0, 45.0, {0.25, 0.25, 0.30},
                              10.0,   0.0};
// FFF: 0.4 mm nozzle, 0.2 mm layers, 50 mm/s print / 150 mm/s travel,
// 1.75 mm filament — the de-facto desktop/industrial baseline.
constexpr MachinePreset kFff{0.0,    0.0,   0.0,   2.0e-4, 2.0e-4, 0.0,   0.05,
                             4.0e-4, 0.15,  1.75e-3, 0.0,  350.0,  12.0,  {0.30, 0.30, 0.30},
                             0.0,    20.0};
// Arc DED / WAAM: robot cell, 3 kg/h deposition at ~3 kW arc power,
// 2 mm effective layers, large envelope.
constexpr MachinePreset kDed{3000.0, 0.008, 0.004, 0.002, 0.002, 0.0,   0.0,
                             0.004,  0.0,   0.0,   3.0,   6000.0, 90.0, {1.00, 1.00, 1.00},
                             30.0,   0.0};
// SLS: polymer powder bed, 60 W CO2 laser, 5 m/s galvo, 250 um hatch,
// 100 um layers, ~9 s recoat (EOS P-396 class).
constexpr MachinePreset kSls{60.0,  5.0,  2.5e-4, 1.0e-4, 1.0e-4, 9.0,   0.0,
                             2.5e-4, 0.0, 0.0,    0.0,    3000.0, 25.0,  {0.34, 0.34, 0.60},
                             40.0,   40.0};

const MachinePreset& preset_for(std::string_view process) {
  if (process == "fff")
    return kFff;
  if (process == "ded_waam")
    return kDed;
  if (process == "sls")
    return kSls;
  return kLpbf;  // lpbf
}

// ---------------------------------------------------------------------
// Value helpers.
// ---------------------------------------------------------------------
const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

pipeline::Value num(double d) {
  return pipeline::Value::number(d);
}

pipeline::Value str(std::string s) {
  return pipeline::Value::string(std::move(s));
}

pipeline::Value vec3(const double v[3]) {
  return pipeline::Value::list({num(v[0]), num(v[1]), num(v[2])});
}

// A number from a map with a default; non-numbers fall back silently to
// the default (the caller already validated the shape of `machine`).
double num_or(const pipeline::Value* map, const char* key, double fallback) {
  if (map == nullptr)
    return fallback;
  const auto* v = map->kind() == pipeline::Value::Kind::Map ? map->find(key) : nullptr;
  return (v != nullptr && v->kind() == pipeline::Value::Kind::Number) ? v->as_number() : fallback;
}

struct Stage {
  std::string id;
  std::string plugin;
  std::map<std::string, pipeline::Value> input;
};

pipeline::Value stage_to_value(const Stage& s) {
  std::map<std::string, pipeline::Value> m;
  m.emplace("id", str(s.id));
  m.emplace("plugin", str(s.plugin));
  m.emplace("input", pipeline::Value::map(s.input));
  return pipeline::Value::map(std::move(m));
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

// ---------------------------------------------------------------------
// The planner proper.
// ---------------------------------------------------------------------
ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map) {
    return invalid("input must be a map",
                   "propose_am_setup input must be a map with `process` and `material`",
                   "{process: 'lpbf', material: '316L'}");
  }

  // ---- process ----
  std::string process = "lpbf";
  if (const auto* p = find(inputs, "process")) {
    if (p->kind() != pipeline::Value::Kind::String) {
      return invalid("process must be a string",
                     "`process` must be one of lpbf / fff / ded_waam / sls",
                     "omit it to default to 'lpbf'");
    }
    process = std::string(p->as_string());
  }
  if (process != "lpbf" && process != "fff" && process != "ded_waam" && process != "sls") {
    return invalid("unsupported process",
                   "`process` must be one of lpbf / fff / ded_waam / sls",
                   "got: '" + process + "'");
  }
  const bool polymer_process = (process == "fff" || process == "sls");

  // ---- material ----
  std::string material_id = polymer_process ? "PA12-CF" : "316L";
  if (const auto* m = find(inputs, "material")) {
    if (m->kind() != pipeline::Value::Kind::String) {
      return invalid("material must be a string",
                     "`material` must be a string naming a table entry",
                     "one of: " + material_id_list());
    }
    material_id = std::string(m->as_string());
  }
  const AmMaterial* mat = find_material(material_id);
  if (mat == nullptr) {
    return invalid("unknown material '" + material_id + "'",
                   "`material` '" + material_id + "' is not in the built-in AM material table",
                   "one of: " + material_id_list());
  }
  if (mat->polymer != polymer_process) {
    return invalid("material / process family mismatch",
                   "material '" + material_id + "' is a " + std::string(mat->family)
                       + " feedstock and process '" + process + "' is a "
                       + (polymer_process ? "polymer" : "metal") + " process",
                   polymer_process ? "polymer processes (fff / sls) take PA12-CF or PEKK"
                                   : "metal processes (lpbf / ded_waam) take 316L / Ti6Al4V / "
                                     "IN625 / AlSi10Mg / NAB / 2507");
  }

  // ---- machine overrides ----
  const auto* machine_in = find(inputs, "machine");
  if (machine_in != nullptr && machine_in->kind() != pipeline::Value::Kind::Map) {
    return invalid("machine must be a map",
                   "`machine` must be a map of machine parameters when set",
                   "{machine: {laser_power: 280, scan_speed: 1.0}}");
  }
  const MachinePreset& mp = preset_for(process);

  const double laser_power = num_or(machine_in, "laser_power", mp.laser_power);
  const double scan_speed = num_or(machine_in, "scan_speed", mp.scan_speed);
  const double hatch_spacing = num_or(machine_in, "hatch_spacing", mp.hatch_spacing);
  const double layer_height = num_or(machine_in, "layer_height", mp.layer_height);
  const double process_layer_height =
      num_or(machine_in, "process_layer_height", mp.process_layer_height);
  const double recoat_time = num_or(machine_in, "recoat_time", mp.recoat_time);
  const double print_speed = num_or(machine_in, "print_speed", mp.print_speed);
  const double road_width = num_or(machine_in, "road_width", mp.road_width);
  const double travel_speed = num_or(machine_in, "travel_speed", mp.travel_speed);
  const double filament_diameter = num_or(machine_in, "filament_diameter", mp.filament_diameter);
  const double deposition_rate = num_or(machine_in, "deposition_rate", mp.deposition_rate);
  const double machine_power_overhead =
      num_or(machine_in, "machine_power_overhead", mp.machine_power_overhead);
  const double machine_rate_per_hour =
      num_or(machine_in, "machine_rate_per_hour", mp.machine_rate_per_hour);
  const double interlayer_time = num_or(machine_in, "interlayer_time", mp.interlayer_time);
  const double layer_time = num_or(machine_in, "layer_time", mp.layer_time);
  if (layer_height <= 0.0) {
    return invalid("layer_height must be > 0",
                   "`machine.layer_height` must be a positive length in metres",
                   "typical: 1e-3 for a metal simulation layer, 2e-4 for FFF");
  }

  double build_volume[3] = {mp.build_volume[0], mp.build_volume[1], mp.build_volume[2]};
  if (machine_in != nullptr) {
    if (const auto* bv = machine_in->find("build_volume");
        bv != nullptr && bv->kind() == pipeline::Value::Kind::List && bv->as_list().size() == 3) {
      const auto l = bv->as_list();
      bool all_numbers = true;
      for (const auto& c : l) {
        if (c.kind() != pipeline::Value::Kind::Number)
          all_numbers = false;
      }
      if (!all_numbers) {
        return invalid("build_volume must be 3 numbers",
                       "`machine.build_volume` must be a list of three numbers in metres",
                       "{machine: {build_volume: [0.25, 0.25, 0.3]}}");
      }
      for (std::size_t i = 0; i < 3; ++i)
        build_volume[i] = l[i].as_number();
    }
  }

  // ---- build direction ----
  double build_direction[3] = {0.0, 0.0, 1.0};
  if (const auto* bd = find(inputs, "build_direction")) {
    if (bd->kind() != pipeline::Value::Kind::List || bd->as_list().size() != 3) {
      return invalid("build_direction must be 3 numbers",
                     "`build_direction` must be a list of three numbers",
                     "default is [0, 0, 1]");
    }
    const auto l = bd->as_list();
    double len2 = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
      if (l[i].kind() != pipeline::Value::Kind::Number) {
        return invalid("build_direction must be 3 numbers",
                       "every entry of `build_direction` must be a number",
                       "default is [0, 0, 1]");
      }
      build_direction[i] = l[i].as_number();
      len2 += build_direction[i] * build_direction[i];
    }
    if (len2 <= 0.0) {
      return invalid("build_direction must be non-zero",
                     "`build_direction` has zero length",
                     "use [0, 0, 1] for a Z-up build");
    }
  }

  // ---- optional marine add-on + misc ----
  bool marine = false;
  if (const auto* mv = find(inputs, "include_marine");
      mv != nullptr && mv->kind() == pipeline::Value::Kind::Bool) {
    marine = mv->as_bool();
  }
  double design_depth = 300.0;  // m — contract default (3.15)
  if (const auto* dd = find(inputs, "design_depth");
      dd != nullptr && dd->kind() == pipeline::Value::Kind::Number) {
    design_depth = dd->as_number();
    marine = true;  // supplying a depth is an explicit marine intent
  }
  std::string application = "structural";
  if (const auto* ap = find(inputs, "application"); ap != nullptr && ap->kind() == pipeline::Value::Kind::String) {
    application = std::string(ap->as_string());
    if (application == "hull" || application == "propulsion" || application == "piping")
      marine = true;
  }
  std::string output_dir = "results";
  if (const auto* od = find(inputs, "output_dir");
      od != nullptr && od->kind() == pipeline::Value::Kind::String) {
    output_dir = std::string(od->as_string());
  }
  std::string part_name = "AM part";
  if (const auto* pn = find(inputs, "part_name");
      pn != nullptr && pn->kind() == pipeline::Value::Kind::String) {
    part_name = std::string(pn->as_string());
  }
  const double overhang_threshold_deg = 45.0;  // contract default (3.9)

  std::vector<std::string> warnings;

  // ------------------------------------------------------------------
  // Stage 1 — the mesh source.
  //
  // `reader.lattice` when the caller supplies a lattice spec, otherwise
  // `mesher.am.layered`. Note the mesher gets no value bag from the ABI
  // (only target_size / optimize / element_order / random_seed), which
  // is why the simulation layer height rides in as `target_size`.
  // ------------------------------------------------------------------
  std::vector<Stage> stages;
  const std::string mesh_stage_id = "build_mesh";
  if (const auto* ls = find(inputs, "lattice_spec");
      ls != nullptr && ls->kind() == pipeline::Value::Kind::String) {
    Stage s;
    s.id = mesh_stage_id;
    s.plugin = "reader.lattice";
    s.input.emplace("path", str(std::string(ls->as_string())));
    stages.push_back(std::move(s));
  } else {
    Stage s;
    s.id = mesh_stage_id;
    s.plugin = "mesher.am.layered";
    s.input.emplace("target_size", num(layer_height));
    s.input.emplace("element_order", num(1.0));
    stages.push_back(std::move(s));
    if (const auto* gp = find(inputs, "geometry_path");
        gp != nullptr && gp->kind() == pipeline::Value::Kind::String) {
      warnings.emplace_back(
          "geometry_path was supplied but the AM capability set ships no CAD reader; prepend "
          "your own reader.* stage and wire its output into build_mesh's `geometry` input, or "
          "mesher.am.layered will fall back to its documented demo build box");
    }
  }

  const auto mesh_ref = [&]() { return pipeline::Value::stage_ref(mesh_stage_id); };

  // Shared thermophysical inputs (metal + polymer paths both need them).
  const auto add_material_thermal = [&](Stage& s) {
    s.input.emplace("density", num(mat->density));
    s.input.emplace("specific_heat", num(mat->specific_heat));
    s.input.emplace("thermal_conductivity", num(mat->thermal_conductivity));
  };
  const auto add_elastic = [&](Stage& s) {
    s.input.emplace("youngs_modulus", num(mat->youngs_modulus));
    s.input.emplace("poisson_ratio", num(mat->poisson_ratio));
  };

  std::string thermal_stage_id;

  if (!polymer_process) {
    // ---- metal thermal history ----
    if (process == "ded_waam") {
      // Rosenthal's moving point source *is* the classic arc-welding
      // solution, so it transfers to arc DED with the arc power and
      // travel speed substituted; the capability id keeps its `lpbf`
      // name because the id set is frozen.
      warnings.emplace_back(
          "ded_waam reuses solver.am.thermal.lpbf: the underlying Rosenthal moving-source "
          "solution is the arc-welding solution, but the melt-pool porosity thresholds are "
          "LPBF-calibrated so postproc.am.melt_pool is omitted for this process");
    }
    Stage th;
    th.id = "thermal";
    th.plugin = "solver.am.thermal.lpbf";
    th.input.emplace("mesh", mesh_ref());
    th.input.emplace("laser_power", num(laser_power));
    th.input.emplace("scan_speed", num(scan_speed));
    th.input.emplace("hatch_spacing", num(hatch_spacing));
    th.input.emplace("layer_height", num(layer_height));
    th.input.emplace("process_layer_height", num(process_layer_height));
    th.input.emplace("absorptivity", num(mat->absorptivity));
    th.input.emplace("melt_temperature", num(mat->melt_temperature));
    th.input.emplace("preheat_temperature", num(80.0));     // deg C, contract 3.3 default
    th.input.emplace("baseplate_temperature", num(80.0));   // deg C
    th.input.emplace("interlayer_time", num(interlayer_time));
    th.input.emplace("build_direction", vec3(build_direction));
    add_material_thermal(th);
    thermal_stage_id = th.id;
    stages.push_back(std::move(th));

    if (process != "ded_waam") {
      Stage mpz;
      mpz.id = "melt_pool";
      mpz.plugin = "postproc.am.melt_pool";
      mpz.input.emplace("field", pipeline::Value::stage_ref(thermal_stage_id));
      mpz.input.emplace("mesh", mesh_ref());
      mpz.input.emplace("laser_power", num(laser_power));
      mpz.input.emplace("scan_speed", num(scan_speed));
      mpz.input.emplace("hatch_spacing", num(hatch_spacing));
      mpz.input.emplace("layer_height", num(layer_height));
      mpz.input.emplace("process_layer_height", num(process_layer_height));
      mpz.input.emplace("absorptivity", num(mat->absorptivity));
      mpz.input.emplace("melt_temperature", num(mat->melt_temperature));
      add_material_thermal(mpz);
      stages.push_back(std::move(mpz));
    }

    Stage di;
    di.id = "distortion";
    di.plugin = "solver.am.distortion.inherent_strain";
    di.input.emplace("mesh", mesh_ref());
    di.input.emplace("cte", num(mat->cte));
    di.input.emplace("melt_temperature", num(mat->melt_temperature));
    di.input.emplace("preheat_temperature", num(80.0));
    di.input.emplace("strain_calibration", num(0.30));  // contract 3.5: MUST be calibrated
    di.input.emplace("layer_height", num(layer_height));
    di.input.emplace("baseplate_clamped", pipeline::Value::boolean(true));
    di.input.emplace("build_direction", vec3(build_direction));
    add_elastic(di);
    stages.push_back(std::move(di));
    warnings.emplace_back(
        "inherent-strain distortion runs with strain_calibration = 0.30, an uncalibrated "
        "placeholder; calibrate it against a measured build before trusting magnitudes");

    Stage rs;
    rs.id = "residual_stress";
    rs.plugin = "postproc.am.residual_stress";
    rs.input.emplace("field", pipeline::Value::stage_ref("distortion"));
    rs.input.emplace("mesh", mesh_ref());
    rs.input.emplace("yield_strength", num(mat->yield_strength));
    rs.input.emplace("cte", num(mat->cte));
    add_elastic(rs);
    stages.push_back(std::move(rs));
  } else {
    // ---- polymer interlayer thermal history ----
    if (process == "sls") {
      warnings.emplace_back(
          "sls reuses solver.am.polymer.fff for the interlayer thermal history with road_width "
          "set to the laser hatch spacing; the model is a lumped-capacitance road-on-road "
          "cooling law, not a powder-bed sintering model");
    }
    Stage th;
    th.id = "thermal";
    th.plugin = "solver.am.polymer.fff";
    th.input.emplace("mesh", mesh_ref());
    th.input.emplace("nozzle_temperature", num(mat->print_temperature));
    th.input.emplace("bed_temperature", num(mat->bed_temperature));
    th.input.emplace("chamber_temperature", num(process == "sls" ? mat->glass_transition : 40.0));
    th.input.emplace("layer_time", num(layer_time));
    th.input.emplace("layer_height", num(layer_height));
    th.input.emplace("road_width", num(road_width));
    th.input.emplace("convection_coefficient", num(30.0));  // W/(m^2*K), contract 3.7 default
    th.input.emplace("glass_transition_temperature", num(mat->glass_transition));
    th.input.emplace("build_direction", vec3(build_direction));
    add_material_thermal(th);
    thermal_stage_id = th.id;
    stages.push_back(std::move(th));

    Stage bs;
    bs.id = "bond_strength";
    bs.plugin = "postproc.am.bond_strength";
    bs.input.emplace("field", pipeline::Value::stage_ref(thermal_stage_id));
    bs.input.emplace("mesh", mesh_ref());
    bs.input.emplace("glass_transition_temperature", num(mat->glass_transition));
    bs.input.emplace("reptation_time_reference", num(2.0));            // s, contract 3.8
    bs.input.emplace("reptation_reference_temperature", num(260.0));   // deg C
    bs.input.emplace("activation_energy", num(8.0e4));                 // J/mol
    bs.input.emplace("layer_time", num(layer_time));
    stages.push_back(std::move(bs));
  }

  // ---- manufacturability (every process) ----
  {
    Stage oh;
    oh.id = "overhang";
    oh.plugin = "solver.am.overhang";
    oh.input.emplace("mesh", mesh_ref());
    oh.input.emplace("build_direction", vec3(build_direction));
    oh.input.emplace("overhang_threshold_deg", num(overhang_threshold_deg));
    oh.input.emplace("layer_height", num(layer_height));
    stages.push_back(std::move(oh));

    Stage pr;
    pr.id = "printability";
    pr.plugin = "solver.am.printability";
    pr.input.emplace("mesh", mesh_ref());
    pr.input.emplace("build_direction", vec3(build_direction));
    pr.input.emplace("overhang_threshold_deg", num(overhang_threshold_deg));
    // Minimum printable wall: ~2x the beam/road width is the usual DfAM
    // rule of thumb. Powder bed -> 5e-4 m (contract 3.10 default);
    // FFF -> two road widths.
    pr.input.emplace("min_wall_thickness", num(polymer_process ? 2.0 * road_width : 5.0e-4));
    pr.input.emplace("machine_build_volume", vec3(build_volume));
    pr.input.emplace("layer_height", num(layer_height));
    if (marine) {
      // 3 mm corrosion allowance is a common marine structural margin;
      // it is a *design* input, not a material property.
      pr.input.emplace("corrosion_allowance", num(0.003));
    }
    stages.push_back(std::move(pr));

    Stage bt;
    bt.id = "buildtime";
    bt.plugin = "solver.am.buildtime";
    bt.input.emplace("mesh", mesh_ref());
    bt.input.emplace("process", str(process));
    bt.input.emplace("layer_height", num(layer_height));
    bt.input.emplace("process_layer_height", num(process_layer_height));
    bt.input.emplace("scan_speed", num(scan_speed));
    bt.input.emplace("hatch_spacing", num(hatch_spacing));
    bt.input.emplace("recoat_time", num(recoat_time));
    bt.input.emplace("print_speed", num(print_speed));
    bt.input.emplace("road_width", num(road_width));
    bt.input.emplace("deposition_rate", num(deposition_rate));
    bt.input.emplace("laser_power", num(laser_power));
    bt.input.emplace("machine_power_overhead", num(machine_power_overhead));
    bt.input.emplace("material_cost_per_kg", num(mat->cost_per_kg));
    bt.input.emplace("machine_rate_per_hour", num(machine_rate_per_hour));
    bt.input.emplace("density", num(mat->density));
    bt.input.emplace("build_direction", vec3(build_direction));
    stages.push_back(std::move(bt));
  }

  // ---- process-specific machine output ----
  if (process == "fff") {
    Stage gc;
    gc.id = "gcode";
    gc.plugin = "writer.am.gcode";
    gc.input.emplace("mesh", mesh_ref());
    gc.input.emplace("path", str(output_dir + "/build.gcode"));
    gc.input.emplace("layer_height", num(layer_height));
    gc.input.emplace("road_width", num(road_width));
    gc.input.emplace("filament_diameter", num(filament_diameter));
    gc.input.emplace("nozzle_temperature", num(mat->print_temperature));
    gc.input.emplace("bed_temperature", num(mat->bed_temperature));
    gc.input.emplace("print_speed", num(print_speed));
    gc.input.emplace("travel_speed", num(travel_speed));
    stages.push_back(std::move(gc));
  } else if (process == "sls" || process == "lpbf") {
    Stage cli;
    cli.id = "slices";
    cli.plugin = "writer.am.cli";
    cli.input.emplace("mesh", mesh_ref());
    cli.input.emplace("path", str(output_dir + "/build.cli"));
    cli.input.emplace("layer_height", num(process_layer_height > 0.0 ? process_layer_height
                                                                    : layer_height));
    cli.input.emplace("build_direction", vec3(build_direction));
    cli.input.emplace("label", str(part_name));
    stages.push_back(std::move(cli));
  }

  // ---- marine add-on ----
  if (marine) {
    Stage hs;
    hs.id = "hydrostatic";
    hs.plugin = "solver.marine.hydrostatic";
    hs.input.emplace("mesh", mesh_ref());
    hs.input.emplace("design_depth", num(design_depth));
    // operating / test / collapse — contract 3.15 default factor set.
    hs.input.emplace("depth_factors", pipeline::Value::list({num(1.0), num(1.5), num(2.25)}));
    hs.input.emplace("salinity_psu", num(35.0));
    hs.input.emplace("seawater_temperature", num(10.0));
    hs.input.emplace("build_direction", vec3(build_direction));
    stages.push_back(std::move(hs));

    Stage hc;
    hc.id = "hull_collapse";
    hc.plugin = "solver.marine.hull_collapse";
    hc.input.emplace("mesh", mesh_ref());
    hc.input.emplace("hull_type", str("ring_stiffened_cylinder"));
    hc.input.emplace("design_depth", num(design_depth));
    hc.input.emplace("yield_strength", num(mat->yield_strength));
    // AM knockdowns: 0.75 geometric imperfection, 0.90 build-direction
    // anisotropy (contract 3.16 defaults, stated there as preliminary
    // sizing values, not class-society factors).
    hc.input.emplace("imperfection_knockdown", num(0.75));
    hc.input.emplace("am_anisotropy_knockdown", num(0.90));
    hc.input.emplace("safety_factor", num(1.5));
    add_elastic(hc);
    stages.push_back(std::move(hc));

    Stage co;
    co.id = "corrosion";
    co.plugin = "solver.marine.corrosion";
    co.input.emplace("mesh", mesh_ref());
    co.input.emplace("alloy", str(std::string(mat->id)));
    co.input.emplace("chromium_pct", num(mat->chromium_pct));
    co.input.emplace("molybdenum_pct", num(mat->molybdenum_pct));
    co.input.emplace("nitrogen_pct", num(mat->nitrogen_pct));
    co.input.emplace("seawater_temperature", num(10.0));
    co.input.emplace("salinity_psu", num(35.0));
    co.input.emplace("service_life_years", num(25.0));
    co.input.emplace("as_built_surface", pipeline::Value::boolean(true));
    stages.push_back(std::move(co));

    Stage qr;
    qr.id = "qualification";
    qr.plugin = "writer.marine.qualification_report";
    qr.input.emplace("mesh", mesh_ref());
    qr.input.emplace("field", pipeline::Value::stage_ref("corrosion"));
    qr.input.emplace("path", str(output_dir + "/qualification.md"));
    qr.input.emplace("part_name", str(part_name));
    qr.input.emplace("process", str(process));
    qr.input.emplace("alloy", str(std::string(mat->id)));
    qr.input.emplace("application", str(application));
    qr.input.emplace("design_depth", num(design_depth));
    qr.input.emplace("service_life_years", num(25.0));
    stages.push_back(std::move(qr));
    warnings.emplace_back(
        "the marine stages are advisory: souxmar is not a classification society and "
        "writer.marine.qualification_report produces a checklist, not an approval");
  }

  // ---- build report (always last) ----
  {
    Stage rp;
    rp.id = "report";
    rp.plugin = "writer.am.report";
    rp.input.emplace("mesh", mesh_ref());
    rp.input.emplace("field", pipeline::Value::stage_ref("buildtime"));
    rp.input.emplace("path", str(output_dir + "/build-report.md"));
    rp.input.emplace("title", str(part_name + " — " + process + " build report"));
    rp.input.emplace("process", str(process));
    rp.input.emplace("material", str(std::string(mat->id)));
    rp.input.emplace("density", num(mat->density));
    rp.input.emplace("material_cost_per_kg", num(mat->cost_per_kg));
    rp.input.emplace("machine_rate_per_hour", num(machine_rate_per_hour));
    rp.input.emplace("machine_power_overhead", num(machine_power_overhead));
    stages.push_back(std::move(rp));
  }

  // ---- guard: never emit an id the host cannot dispatch ----
  for (const auto& s : stages) {
    if (!is_known_capability(s.plugin)) {
      return ToolResult{pipeline::Value::null_value(),
                        "planner produced an unknown capability id",
                        ToolError{"INTERNAL",
                                  "propose_am_setup emitted stage '" + s.id + "' with unknown "
                                      "capability '" + s.plugin + "'",
                                  "this is a bug in propose_am_setup.cpp — the emitted id set is "
                                  "frozen to the 18 manufacturing capabilities"}};
    }
  }

  // ---- assemble the result ----
  std::vector<pipeline::Value> stage_values;
  std::vector<pipeline::Value> capability_values;
  stage_values.reserve(stages.size());
  capability_values.reserve(stages.size());
  for (const auto& s : stages) {
    stage_values.push_back(stage_to_value(s));
    capability_values.push_back(str(s.plugin));
  }

  std::map<std::string, pipeline::Value> pipeline_map;
  pipeline_map.emplace("version", num(1.0));
  pipeline_map.emplace("stages", pipeline::Value::list(std::move(stage_values)));

  std::map<std::string, pipeline::Value> material_map;
  material_map.emplace("id", str(std::string(mat->id)));
  material_map.emplace("family", str(std::string(mat->family)));
  material_map.emplace("density", num(mat->density));
  material_map.emplace("thermal_conductivity", num(mat->thermal_conductivity));
  material_map.emplace("specific_heat", num(mat->specific_heat));
  material_map.emplace("melt_temperature", num(mat->melt_temperature));
  material_map.emplace("glass_transition_temperature", num(mat->glass_transition));
  material_map.emplace("youngs_modulus", num(mat->youngs_modulus));
  material_map.emplace("poisson_ratio", num(mat->poisson_ratio));
  material_map.emplace("cte", num(mat->cte));
  material_map.emplace("yield_strength", num(mat->yield_strength));
  material_map.emplace("absorptivity", num(mat->absorptivity));
  material_map.emplace("cost_per_kg", num(mat->cost_per_kg));
  auto material_value = pipeline::Value::map(std::move(material_map));

  std::map<std::string, pipeline::Value> machine_map;
  machine_map.emplace("laser_power", num(laser_power));
  machine_map.emplace("scan_speed", num(scan_speed));
  machine_map.emplace("hatch_spacing", num(hatch_spacing));
  machine_map.emplace("layer_height", num(layer_height));
  machine_map.emplace("process_layer_height", num(process_layer_height));
  machine_map.emplace("recoat_time", num(recoat_time));
  machine_map.emplace("print_speed", num(print_speed));
  machine_map.emplace("road_width", num(road_width));
  machine_map.emplace("deposition_rate", num(deposition_rate));
  machine_map.emplace("machine_power_overhead", num(machine_power_overhead));
  machine_map.emplace("machine_rate_per_hour", num(machine_rate_per_hour));
  machine_map.emplace("build_volume", vec3(build_volume));
  auto machine_value = pipeline::Value::map(std::move(machine_map));

  // Stage the resolved setup so estimate_build_cost / set_build_orientation
  // can read it back without the model re-typing every number.
  if (ctx.session_state != nullptr) {
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map)
      *ctx.session_state = pipeline::Value::map({});
    std::map<std::string, pipeline::Value> manufacturing;
    manufacturing.emplace("process", str(process));
    manufacturing.emplace("material", material_value);
    manufacturing.emplace("machine", machine_value);
    manufacturing.emplace("build_direction", vec3(build_direction));
    manufacturing.emplace("overhang_threshold_deg", num(overhang_threshold_deg));
    std::map<std::string, pipeline::Value> session_map;
    for (const auto& [k, v] : ctx.session_state->as_map()) {
      if (k == "manufacturing")
        continue;
      session_map.emplace(k, v);
    }
    session_map.emplace("manufacturing", pipeline::Value::map(std::move(manufacturing)));
    *ctx.session_state = pipeline::Value::map(std::move(session_map));
  }

  std::vector<pipeline::Value> warning_values;
  warning_values.reserve(warnings.size());
  for (const auto& w : warnings)
    warning_values.push_back(str(w));

  std::map<std::string, pipeline::Value> out;
  out.emplace("process", str(process));
  out.emplace("material", material_value);
  out.emplace("machine", machine_value);
  out.emplace("build_direction", vec3(build_direction));
  out.emplace("pipeline", pipeline::Value::map(std::move(pipeline_map)));
  out.emplace("stage_count", num(static_cast<double>(stages.size())));
  out.emplace("capabilities", pipeline::Value::list(std::move(capability_values)));
  out.emplace("marine", pipeline::Value::boolean(marine));
  out.emplace("warnings", pipeline::Value::list(std::move(warning_values)));
  out.emplace("notes",
              str("Numbers are indicative machine-class / literature values, not qualified "
                  "process parameters. Feed `pipeline` to propose_pipeline to persist it."));

  std::string summary = "proposed " + std::to_string(stages.size()) + "-stage " + process
                        + " pipeline for " + std::string(mat->id)
                        + (marine ? " (+ marine advisory stages)" : "") + ", "
                        + std::to_string(warnings.size()) + " caveat(s)";
  return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
}

}  // namespace

Tool make_propose_am_setup_tool() {
  Tool t;
  t.name = "propose_am_setup";
  t.description =
      "Propose a complete, runnable additive-manufacturing pipeline for a "
      "{process, material, machine} triple: layered build mesh, process thermal history, "
      "distortion / residual stress (metals) or interlayer bond strength (polymers), "
      "overhang + printability + build-time stages, machine output (G-code / CLI slices) "
      "and a build report. Set include_marine or design_depth to append the hydrostatic / "
      "collapse-margin / corrosion / qualification-dossier stages. Read-mostly: it stages "
      "the resolved setup under session `manufacturing` and dispatches nothing.";
  t.category = "Pipeline";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{process?: 'lpbf' | 'fff' | 'ded_waam' | 'sls',      # default 'lpbf'\n"
      " material?: string,                                  # 316L | Ti6Al4V | IN625 | AlSi10Mg |\n"
      "                                                     # NAB | 2507 | PA12-CF | PEKK\n"
      " machine?: {laser_power?, scan_speed?, hatch_spacing?, layer_height?,\n"
      "            process_layer_height?, recoat_time?, print_speed?, road_width?,\n"
      "            travel_speed?, filament_diameter?, deposition_rate?,\n"
      "            machine_power_overhead?, machine_rate_per_hour?, interlayer_time?,\n"
      "            layer_time?, build_volume?: [x, y, z]},   # SI units throughout\n"
      " build_direction?: [x, y, z],                        # default [0, 0, 1]\n"
      " lattice_spec?: string,                              # path -> reader.lattice mesh source\n"
      " geometry_path?: string,                             # warns: no CAD reader in this set\n"
      " include_marine?: bool, design_depth?: number,       # depth (m) implies marine\n"
      " application?: 'hull' | 'propulsion' | 'piping' | 'structural' | 'non_structural',\n"
      " part_name?: string, output_dir?: string             # default 'results'\n"
      "}";
  t.output_schema_doc =
      "{process, material: {...}, machine: {...}, build_direction: [x,y,z],\n"
      " pipeline: {version: 1, stages: [{id, plugin, input}, ...]},   # dispatch-ready\n"
      " stage_count: number, capabilities: [string], marine: bool,\n"
      " warnings: [string],                                          # honesty surface\n"
      " notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    // The handler is the tool contract's boundary: nothing escapes it.
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "propose_am_setup failed internally",
                        ToolError{"INTERNAL", std::string("propose_am_setup: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "propose_am_setup failed internally",
                        ToolError{"INTERNAL", "propose_am_setup: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
