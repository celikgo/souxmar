// SPDX-License-Identifier: Apache-2.0
//
// Tool: estimate_build_cost
//
// Tool 22 of the ADR-0010 additive ratchet. Answers "what does this cost
// to print?" from the staged manufacturing setup plus the session mesh's
// bounding box — no plugin dispatch, no file, no network.
//
// What it computes:
//   part_volume  = bbox_volume * fill_fraction
//   mass         = density * part_volume
//   height       = extent of the bounding-box corners projected on the
//                  unit build direction
//   layers       = ceil(height / process_layer_height)
//   deposition time, per process family:
//     lpbf / sls  t_scan  = part_volume / (hatch_spacing
//                                          * process_layer_height
//                                          * scan_speed)
//                 t_build = t_scan + layers * recoat_time
//     fff         t_scan  = part_volume / (road_width
//                                          * process_layer_height
//                                          * print_speed)
//                 t_build = t_scan + layers * recoat_time
//     ded_waam    t_build = mass / (deposition_rate / 3600)
//   energy       = beam_power * t_scan + machine_power_overhead * t_build
//   machine_cost = (t_build / 3600) * machine_rate_per_hour
//   material_cost= mass * material_cost_per_kg
//   total        = machine_cost + material_cost
//
// The volumetric-rate form (area x layer x speed) is the standard
// powder-bed / extrusion build-rate estimate used in every published AM
// cost model (Baumers et al. 2016, "The cost of additive manufacturing";
// Lindemann et al. 2012) — this is the same arithmetic, spelled out.
//
// What this is NOT:
//   * Not a slicer. `fill_fraction` (default 0.35) is a documented
//     stand-in for the part's real volume fraction of its bounding box;
//     supply the measured value, or run `solver.am.buildtime` for the
//     layer-resolved answer that bins actual cell volume.
//   * No supports, no skirt/brim, no purge, no recycled-powder credit,
//     no build-plate amortisation, no labour, no post-processing, no
//     scrap or first-time-right yield.
//   * Energy is reported for reference and is NOT added to the total:
//     service-bureau hourly rates already include it. Adding both would
//     double-count.
//   * Currency is whatever unit the rates were given in. The defaults
//     are indicative service-bureau numbers, not a quote.
//
// Confirmation::Auto (the contract's `Confirmation::None` maps onto
// tool.h's no-prompt tier). Justification: arithmetic over session state.
// It reads ToolContext.mesh_handle and session_state, mutates neither,
// and dispatches nothing.

#include "souxmar/ai/tool.h"
#include "souxmar/core/mesh.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace souxmar::ai {

namespace {

// Per-process defaults. Same numbers as the manufacturing capability
// contract's solver.am.buildtime defaults and propose_am_setup's machine
// presets; duplicated here (separate translation unit, no shared header
// in the tool surface) so the tool works standalone.
struct ProcessDefaults {
  double scan_speed;             // m/s
  double hatch_spacing;          // m
  double process_layer_height;   // m
  double recoat_time;            // s per machine layer
  double print_speed;            // m/s
  double road_width;             // m
  double deposition_rate;        // kg/h
  double beam_power;             // W — laser / arc power while depositing
  double machine_power_overhead; // W
  double machine_rate_per_hour;  // currency units / h
};

// lpbf: 200 W / 0.8 m/s / 110 um hatch / 30 um layer / 8 s recoat.
constexpr ProcessDefaults kLpbf{0.8, 1.1e-4, 3.0e-5, 8.0, 0.0, 0.0, 0.0, 200.0, 2500.0, 45.0};
// fff: 0.4 mm road, 0.2 mm layer, 50 mm/s.
constexpr ProcessDefaults kFff{0.0, 0.0, 2.0e-4, 0.0, 0.05, 4.0e-4, 0.0, 0.0, 350.0, 12.0};
// ded_waam: 3 kg/h at ~3 kW arc power.
constexpr ProcessDefaults kDed{0.0, 0.0, 2.0e-3, 0.0, 0.0, 4.0e-3, 3.0, 3000.0, 6000.0, 90.0};
// sls: 60 W CO2, 5 m/s galvo, 250 um hatch, 100 um layer, 9 s recoat.
constexpr ProcessDefaults kSls{5.0, 2.5e-4, 1.0e-4, 9.0, 0.0, 2.5e-4, 0.0, 60.0, 3000.0, 25.0};

const ProcessDefaults& defaults_for(std::string_view process) {
  if (process == "fff")
    return kFff;
  if (process == "ded_waam")
    return kDed;
  if (process == "sls")
    return kSls;
  return kLpbf;
}

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

const pipeline::Value* number_at(const pipeline::Value* map, const char* key) {
  if (map == nullptr || map->kind() != pipeline::Value::Kind::Map)
    return nullptr;
  const auto* v = map->find(key);
  return (v != nullptr && v->kind() == pipeline::Value::Kind::Number) ? v : nullptr;
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{
      pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map && inputs.kind() != pipeline::Value::Kind::Null) {
    return invalid("input must be a map",
                   "estimate_build_cost input must be a map (or null for all defaults)",
                   "{process: 'lpbf', fill_fraction: 0.4}");
  }

  // ---- the staged setup, if propose_am_setup ran ----
  const pipeline::Value* mfg =
      ctx.session_state != nullptr ? ctx.session_state->find("manufacturing") : nullptr;
  if (mfg != nullptr && mfg->kind() != pipeline::Value::Kind::Map)
    mfg = nullptr;
  const pipeline::Value* mfg_machine = mfg != nullptr ? mfg->find("machine") : nullptr;
  const pipeline::Value* mfg_material = mfg != nullptr ? mfg->find("material") : nullptr;

  // ---- process ----
  std::string process = "lpbf";
  const pipeline::Value* process_in = find(inputs, "process");
  if (process_in != nullptr && process_in->kind() != pipeline::Value::Kind::String) {
    return invalid("process must be a string",
                   "`process` must be one of lpbf / fff / ded_waam / sls",
                   "omit it to reuse session manufacturing.process");
  }
  if (process_in != nullptr) {
    process = std::string(process_in->as_string());
  } else if (const auto* mp = mfg != nullptr ? mfg->find("process") : nullptr;
             mp != nullptr && mp->kind() == pipeline::Value::Kind::String) {
    process = std::string(mp->as_string());
  }
  if (process != "lpbf" && process != "fff" && process != "ded_waam" && process != "sls") {
    return invalid("unsupported process",
                   "`process` must be one of lpbf / fff / ded_waam / sls",
                   "got: '" + process + "'");
  }
  const ProcessDefaults& dflt = defaults_for(process);

  // Resolution order: explicit input -> session manufacturing.machine ->
  // session manufacturing.material -> process default.
  const auto resolve = [&](const char* key, double fallback) -> double {
    if (const auto* v = number_at(&inputs, key))
      return v->as_number();
    if (const auto* v = number_at(mfg_machine, key))
      return v->as_number();
    if (const auto* v = number_at(mfg_material, key))
      return v->as_number();
    return fallback;
  };

  // 7990 kg/m^3 (316L) and 90 / kg are the contract's solver.am.buildtime
  // defaults; the material row staged by propose_am_setup overrides them.
  double density = resolve("density", 7990.0);
  double material_cost_per_kg = resolve("material_cost_per_kg", 90.0);
  if (const auto* v = number_at(mfg_material, "cost_per_kg");
      v != nullptr && number_at(&inputs, "material_cost_per_kg") == nullptr) {
    material_cost_per_kg = v->as_number();  // the material row spells it `cost_per_kg`
  }
  const double scan_speed = resolve("scan_speed", dflt.scan_speed);
  const double hatch_spacing = resolve("hatch_spacing", dflt.hatch_spacing);
  const double process_layer_height = resolve("process_layer_height", dflt.process_layer_height);
  const double recoat_time = resolve("recoat_time", dflt.recoat_time);
  const double print_speed = resolve("print_speed", dflt.print_speed);
  const double road_width = resolve("road_width", dflt.road_width);
  const double deposition_rate = resolve("deposition_rate", dflt.deposition_rate);
  const double beam_power = resolve("laser_power", dflt.beam_power);
  const double machine_power_overhead =
      resolve("machine_power_overhead", dflt.machine_power_overhead);
  const double machine_rate_per_hour = resolve("machine_rate_per_hour", dflt.machine_rate_per_hour);

  double fill_fraction = 0.35;  // documented stand-in; see the header
  if (const auto* v = number_at(&inputs, "fill_fraction")) {
    fill_fraction = v->as_number();
    if (!(fill_fraction > 0.0) || fill_fraction > 1.0) {
      return invalid("fill_fraction out of range",
                     "`fill_fraction` must be in (0, 1]",
                     "it is the part's volume fraction of its bounding box");
    }
  }
  if (!(density > 0.0)) {
    return invalid("density must be > 0",
                   "`density` must be a positive mass density in kg/m^3",
                   "316L is 7990; AlSi10Mg 2670; PA12 1050");
  }
  if (!(process_layer_height > 0.0)) {
    return invalid("process_layer_height must be > 0",
                   "`process_layer_height` must be a positive machine layer thickness in metres",
                   "LPBF 3e-5, SLS 1e-4, FFF 2e-4, WAAM 2e-3");
  }

  // ---- build direction ----
  double bx = 0.0;
  double by = 0.0;
  double bz = 1.0;
  const pipeline::Value* bd = find(inputs, "build_direction");
  if (bd == nullptr && mfg != nullptr)
    bd = mfg->find("build_direction");
  if (bd != nullptr) {
    if (bd->kind() != pipeline::Value::Kind::List || bd->as_list().size() != 3) {
      return invalid("build_direction must be 3 numbers",
                     "`build_direction` must be a list of three numbers",
                     "default is [0, 0, 1]");
    }
    const auto l = bd->as_list();
    for (const auto& comp : l) {
      if (comp.kind() != pipeline::Value::Kind::Number) {
        return invalid("build_direction must be 3 numbers",
                       "every entry of `build_direction` must be a number",
                       "default is [0, 0, 1]");
      }
    }
    bx = l[0].as_number();
    by = l[1].as_number();
    bz = l[2].as_number();
    const double len2 = bx * bx + by * by + bz * bz;
    if (!(len2 > 0.0)) {
      return invalid("build_direction must be non-zero",
                     "`build_direction` has zero length",
                     "use [0, 0, 1] for a Z-up build");
    }
    const double inv = 1.0 / std::sqrt(len2);
    bx *= inv;
    by *= inv;
    bz *= inv;
  }

  // ---- geometry ----
  if (!ctx.mesh_handle) {
    return ToolResult{
        pipeline::Value::null_value(),
        "no mesh available — call `mesh` first",
        ToolError{"PRECONDITION_FAILED",
                  "estimate_build_cost needs the part's bounding box; "
                  "ToolContext.mesh_handle is null",
                  "invoke the `mesh` tool (e.g. capability mesher.am.layered) first, or run "
                  "solver.am.buildtime through `solve` for the layer-resolved estimate"}};
  }
  const auto box = ctx.mesh_handle->bounding_box();
  const double dx = box[3] - box[0];
  const double dy = box[4] - box[1];
  const double dz = box[5] - box[2];
  const double bbox_volume = dx * dy * dz;
  if (!(bbox_volume > 0.0)) {
    return ToolResult{
        pipeline::Value::null_value(),
        "staged mesh has a degenerate bounding box",
        ToolError{"PRECONDITION_FAILED",
                  "the staged mesh's bounding box has zero volume, so there is no part to cost",
                  "mesh a real part (a surface-only or planar mesh has no build volume)"}};
  }
  // Build height = extent of the 8 box corners projected on b. For an
  // axis-aligned direction this reduces to the matching box extent.
  const double height =
      std::abs(dx * bx) + std::abs(dy * by) + std::abs(dz * bz);
  if (!(height > 0.0)) {
    return invalid("build height is zero",
                   "the bounding box has zero extent along `build_direction`",
                   "pick a build direction that is not degenerate with the part");
  }

  const double part_volume = bbox_volume * fill_fraction;
  const double mass = density * part_volume;
  const double layers = std::ceil(height / process_layer_height);

  // ---- time ----
  double scan_time = 0.0;
  double build_time = 0.0;
  if (process == "ded_waam") {
    if (!(deposition_rate > 0.0)) {
      return invalid("deposition_rate must be > 0",
                     "`deposition_rate` (kg/h) must be positive for a ded_waam estimate",
                     "3.0 kg/h is a typical arc-DED figure");
    }
    build_time = mass / (deposition_rate / 3600.0);
    scan_time = build_time;  // the arc is on for the whole deposition
  } else if (process == "fff") {
    if (!(road_width > 0.0) || !(print_speed > 0.0)) {
      return invalid("road_width / print_speed must be > 0",
                     "an FFF estimate needs positive `road_width` and `print_speed`",
                     "0.4 mm road at 50 mm/s: {road_width: 4e-4, print_speed: 0.05}");
    }
    scan_time = part_volume / (road_width * process_layer_height * print_speed);
    build_time = scan_time + layers * recoat_time;
  } else {  // lpbf / sls
    if (!(hatch_spacing > 0.0) || !(scan_speed > 0.0)) {
      return invalid("hatch_spacing / scan_speed must be > 0",
                     "a powder-bed estimate needs positive `hatch_spacing` and `scan_speed`",
                     "LPBF 316L nominal: {hatch_spacing: 1.1e-4, scan_speed: 0.8}");
    }
    scan_time = part_volume / (hatch_spacing * process_layer_height * scan_speed);
    build_time = scan_time + layers * recoat_time;
  }

  const double energy_j = beam_power * scan_time + machine_power_overhead * build_time;
  const double energy_kwh = energy_j / 3.6e6;
  const double build_hours = build_time / 3600.0;
  const double machine_cost = build_hours * machine_rate_per_hour;
  const double material_cost = mass * material_cost_per_kg;
  const double total_cost = machine_cost + material_cost;

  std::string material_label = "unspecified";
  if (const auto* mat_id =
          mfg_material != nullptr && mfg_material->kind() == pipeline::Value::Kind::Map
              ? mfg_material->find("id")
              : nullptr;
      mat_id != nullptr && mat_id->kind() == pipeline::Value::Kind::String) {
    material_label = std::string(mat_id->as_string());
  }
  if (const auto* mat_in = find(inputs, "material");
      mat_in != nullptr && mat_in->kind() == pipeline::Value::Kind::String) {
    material_label = std::string(mat_in->as_string());
  }

  std::map<std::string, pipeline::Value> params;
  params.emplace("density", pipeline::Value::number(density));
  params.emplace("material_cost_per_kg", pipeline::Value::number(material_cost_per_kg));
  params.emplace("process_layer_height", pipeline::Value::number(process_layer_height));
  params.emplace("scan_speed", pipeline::Value::number(scan_speed));
  params.emplace("hatch_spacing", pipeline::Value::number(hatch_spacing));
  params.emplace("print_speed", pipeline::Value::number(print_speed));
  params.emplace("road_width", pipeline::Value::number(road_width));
  params.emplace("deposition_rate", pipeline::Value::number(deposition_rate));
  params.emplace("recoat_time", pipeline::Value::number(recoat_time));
  params.emplace("laser_power", pipeline::Value::number(beam_power));
  params.emplace("machine_power_overhead", pipeline::Value::number(machine_power_overhead));
  params.emplace("machine_rate_per_hour", pipeline::Value::number(machine_rate_per_hour));
  params.emplace("build_direction",
                 pipeline::Value::list({pipeline::Value::number(bx),
                                        pipeline::Value::number(by),
                                        pipeline::Value::number(bz)}));

  std::map<std::string, pipeline::Value> out;
  out.emplace("process", pipeline::Value::string(process));
  out.emplace("material", pipeline::Value::string(material_label));
  out.emplace("setup_source",
              pipeline::Value::string(mfg != nullptr ? "session_manufacturing" : "defaults"));
  out.emplace("bounding_box_volume_m3", pipeline::Value::number(bbox_volume));
  out.emplace("fill_fraction", pipeline::Value::number(fill_fraction));
  out.emplace("part_volume_m3", pipeline::Value::number(part_volume));
  out.emplace("mass_kg", pipeline::Value::number(mass));
  out.emplace("build_height_m", pipeline::Value::number(height));
  out.emplace("num_machine_layers", pipeline::Value::number(layers));
  out.emplace("deposition_time_s", pipeline::Value::number(scan_time));
  out.emplace("build_time_s", pipeline::Value::number(build_time));
  out.emplace("build_time_hours", pipeline::Value::number(build_hours));
  out.emplace("energy_j", pipeline::Value::number(energy_j));
  out.emplace("energy_kwh", pipeline::Value::number(energy_kwh));
  out.emplace("machine_cost", pipeline::Value::number(machine_cost));
  out.emplace("material_cost", pipeline::Value::number(material_cost));
  out.emplace("total_cost", pipeline::Value::number(total_cost));
  out.emplace("parameters", pipeline::Value::map(std::move(params)));
  out.emplace("notes",
              pipeline::Value::string(
                  "Bounding-box estimate with fill_fraction as a stand-in for the real part "
                  "volume; no supports, labour, post-processing or yield. Energy is reported "
                  "for reference and is not added to total_cost (the hourly rate covers it). "
                  "Run solver.am.buildtime for the layer-resolved figure."));

  char buf[416];
  std::snprintf(buf,
                sizeof(buf),
                "build cost (%s, %s): %.4g kg, %.0f layers, %.2f h, %.4g kWh -> "
                "machine %.2f + material %.2f = %.2f",
                process.c_str(),
                material_label.c_str(),
                mass,
                layers,
                build_hours,
                energy_kwh,
                machine_cost,
                material_cost,
                total_cost);

  return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
}

}  // namespace

Tool make_estimate_build_cost_tool() {
  Tool t;
  t.name = "estimate_build_cost";
  t.description =
      "Estimate mass, build time, energy, machine cost, material cost and total for the "
      "staged part: reads session `manufacturing` (as staged by propose_am_setup) plus the "
      "session mesh's bounding box. Every parameter can be overridden per call. "
      "Bounding-box arithmetic, not a slicer — run solver.am.buildtime via `solve` for the "
      "layer-resolved figure.";
  t.category = "Mesh";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{process?: 'lpbf' | 'fff' | 'ded_waam' | 'sls',   # default: session setup, else lpbf\n"
      " fill_fraction?: number,                          # (0, 1], default 0.35\n"
      " density?: number,                                # kg/m^3\n"
      " material_cost_per_kg?: number,\n"
      " process_layer_height?: number,                   # m, machine layer\n"
      " scan_speed?, hatch_spacing?,                     # powder bed\n"
      " print_speed?, road_width?,                       # FFF\n"
      " deposition_rate?,                                # kg/h, DED\n"
      " recoat_time?, laser_power?, machine_power_overhead?, machine_rate_per_hour?,\n"
      " build_direction?: [x, y, z], material?: string   # label only\n"
      "}";
  t.output_schema_doc =
      "{process, material, setup_source: 'session_manufacturing' | 'defaults',\n"
      " bounding_box_volume_m3, fill_fraction, part_volume_m3, mass_kg,\n"
      " build_height_m, num_machine_layers, deposition_time_s, build_time_s,\n"
      " build_time_hours, energy_j, energy_kwh,\n"
      " machine_cost, material_cost, total_cost,      # total = machine + material\n"
      " parameters: {...resolved inputs...}, notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "estimate_build_cost failed internally",
                        ToolError{"INTERNAL", std::string("estimate_build_cost: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "estimate_build_cost failed internally",
                        ToolError{"INTERNAL", "estimate_build_cost: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
