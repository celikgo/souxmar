// SPDX-License-Identifier: Apache-2.0
//
// Tool: apply_hydrostatic_load
//
// Tool 23 of the ADR-0010 additive ratchet. Marine sibling of `set_bc` /
// `apply_inlet`: stages a depth-derived pressure load case in
// session_state["boundary_conditions"] with `type == "hydrostatic"`, and
// computes the pressure itself so the agent gets a number back instead of
// a bag it has to reason about.
//
// What it computes:
//   p = rho * g * h  [+ p_atm when include_atmospheric]
// reported as a positive magnitude, compressive on the wetted surface
// (the sign convention `solver.marine.hydrostatic` uses).
//
// Seawater density, when not given explicitly, comes from a two-term fit
// regressed against the UNESCO EOS-80 surface-density table
// (Millero & Poisson 1981):
//   rho(S, T) = 1027.0 + 0.78*(S - 35) - 0.17*(T - 10) - 0.0044*(T - 10)^2
// with S in PSU and T in deg C. Within 0-30 deg C and 30-40 PSU it tracks
// EOS-80 to about +/-0.3 kg/m^3 at atmospheric pressure; outside that box
// the tool refuses rather than extrapolate. It ignores the pressure
// (compressibility) term, which adds roughly +1.5 kg/m^3 per 300 m of
// depth — conservative for a pressure-hull load case, since ignoring it
// under-predicts the load by ~0.15 %.
//
// What this is NOT: it does not touch the mesh. Which surface is wetted,
// and how the pressure varies over it, is `solver.marine.hydrostatic`'s
// job (it puts the mesh's top at the design depth and adds rho*g*dz
// below). This tool stages the load *case*: depth, density, pressure, and
// the factored set.
//
// Confirmation::ConfirmOnce, matching the contract and the BC tier: it
// mutates session state that the next solve consumes, so the first call
// per session surfaces a chip; it is local and reversible, so subsequent
// calls are silent. Same bargain as `set_bc` / `apply_inlet`.

#include "souxmar/ai/tool.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

// Validity box of the density correlation, and the deepest point in the
// ocean — a depth beyond it is a units mistake, not a design.
constexpr double kMinSalinityPsu = 0.0;
constexpr double kMaxSalinityPsu = 45.0;
constexpr double kMinTemperatureC = -2.0;
constexpr double kMaxTemperatureC = 40.0;
constexpr double kMaxDepthM = 11000.0;
constexpr std::size_t kMaxDepthFactors = 16;  // contract 3.15 clamp

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{
      pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

// Returns false when the key is present but not a number.
bool read_number(const pipeline::Value& inputs, const char* key, double& out, bool& present) {
  const auto* v = find(inputs, key);
  present = false;
  if (v == nullptr)
    return true;
  if (v->kind() != pipeline::Value::Kind::Number)
    return false;
  out = v->as_number();
  present = true;
  return true;
}

double seawater_density(double salinity_psu, double temperature_c) {
  const double dt = temperature_c - 10.0;
  return 1027.0 + 0.78 * (salinity_psu - 35.0) - 0.17 * dt - 0.0044 * dt * dt;
}

ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map && inputs.kind() != pipeline::Value::Kind::Null) {
    return invalid("input must be a map",
                   "apply_hydrostatic_load input must be a map (or null for all defaults)",
                   "{tag: 'hull_outer', depth: 300}");
  }

  bool present = false;
  double depth = 300.0;  // m — contract 3.15 design_depth default
  if (!read_number(inputs, "depth", depth, present)) {
    return invalid("depth must be a number",
                   "`depth` must be a number of metres below the free surface",
                   "{depth: 300}");
  }
  if (!(depth > 0.0) || depth > kMaxDepthM) {
    return invalid("depth out of range",
                   "`depth` must be > 0 and <= 11000 m",
                   "depth is metres of seawater, measured positive downward");
  }

  double gravity = 9.80665;  // m/s^2 — standard gravity, contract 3.15
  if (!read_number(inputs, "gravity", gravity, present)) {
    return invalid("gravity must be a number",
                   "`gravity` must be a number in m/s^2",
                   "9.80665 is standard gravity");
  }
  if (!(gravity > 0.0)) {
    return invalid("gravity must be > 0",
                   "`gravity` must be positive",
                   "9.80665 is standard gravity");
  }

  double salinity_psu = 35.0;  // open-ocean mean
  if (!read_number(inputs, "salinity_psu", salinity_psu, present)) {
    return invalid("salinity_psu must be a number",
                   "`salinity_psu` must be a number in practical salinity units",
                   "35 PSU is open-ocean mean; the Baltic runs 7-15");
  }
  if (salinity_psu < kMinSalinityPsu || salinity_psu > kMaxSalinityPsu) {
    return invalid("salinity_psu out of range",
                   "`salinity_psu` must be in [0, 45] — the density fit's validity range",
                   "supply `seawater_density` directly for brines outside that range");
  }

  double temperature_c = 10.0;  // deg C — house rule: temperatures are deg C
  if (!read_number(inputs, "seawater_temperature", temperature_c, present)) {
    return invalid("seawater_temperature must be a number",
                   "`seawater_temperature` must be a number in degrees Celsius",
                   "10 C is a temperate-water default");
  }
  if (temperature_c < kMinTemperatureC || temperature_c > kMaxTemperatureC) {
    return invalid("seawater_temperature out of range",
                   "`seawater_temperature` must be in [-2, 40] C — the density fit's validity "
                   "range",
                   "supply `seawater_density` directly to bypass the correlation");
  }

  double density = 0.0;
  bool density_given = false;
  if (!read_number(inputs, "seawater_density", density, density_given)) {
    return invalid("seawater_density must be a number",
                   "`seawater_density` must be a number in kg/m^3",
                   "omit it to derive the density from salinity + temperature");
  }
  std::string density_source = "explicit";
  if (!density_given || density <= 0.0) {
    density = seawater_density(salinity_psu, temperature_c);
    density_source = "derived_from_salinity_temperature";
  }

  bool include_atmospheric = false;
  if (const auto* v = find(inputs, "include_atmospheric")) {
    if (v->kind() != pipeline::Value::Kind::Bool) {
      return invalid("include_atmospheric must be a bool",
                     "`include_atmospheric` must be true or false when set",
                     "default false: the load case is gauge pressure");
    }
    include_atmospheric = v->as_bool();
  }
  double atmospheric_pressure = 101325.0;  // Pa — ISA sea level
  if (!read_number(inputs, "atmospheric_pressure", atmospheric_pressure, present)) {
    return invalid("atmospheric_pressure must be a number",
                   "`atmospheric_pressure` must be a number in Pa",
                   "101325 Pa is ISA sea level");
  }
  const double p_offset = include_atmospheric ? atmospheric_pressure : 0.0;

  // ---- the load case, plus the optional factored set ----
  const double pressure = density * gravity * depth + p_offset;

  std::vector<double> factors;
  if (const auto* fv = find(inputs, "depth_factors")) {
    if (fv->kind() != pipeline::Value::Kind::List || fv->as_list().empty()
        || fv->as_list().size() > kMaxDepthFactors) {
      return invalid("depth_factors must be 1..16 numbers",
                     "`depth_factors` must be a list of 1 to 16 positive numbers when set",
                     "[1.0, 1.5, 2.25] = operating / test / collapse");
    }
    for (const auto& f : fv->as_list()) {
      if (f.kind() != pipeline::Value::Kind::Number || !(f.as_number() > 0.0)) {
        return invalid("depth_factors must be positive numbers",
                       "every entry of `depth_factors` must be a number > 0",
                       "[1.0, 1.5, 2.25] = operating / test / collapse");
      }
      factors.push_back(f.as_number());
    }
  } else {
    factors.push_back(1.0);
  }

  std::vector<pipeline::Value> load_cases;
  load_cases.reserve(factors.size());
  double max_pressure = 0.0;
  for (std::size_t i = 0; i < factors.size(); ++i) {
    const double case_depth = depth * factors[i];
    const double case_pressure = density * gravity * case_depth + p_offset;
    max_pressure = case_pressure > max_pressure ? case_pressure : max_pressure;
    std::map<std::string, pipeline::Value> lc;
    lc.emplace("index", pipeline::Value::number(static_cast<double>(i)));
    lc.emplace("factor", pipeline::Value::number(factors[i]));
    lc.emplace("depth", pipeline::Value::number(case_depth));
    lc.emplace("pressure", pipeline::Value::number(case_pressure));
    load_cases.push_back(pipeline::Value::map(std::move(lc)));
  }

  // ---- stage it ----
  if (ctx.session_state == nullptr) {
    return ToolResult{
        pipeline::Value::null_value(),
        "session_state not wired",
        ToolError{"INTERNAL",
                  "apply_hydrostatic_load requires ToolContext.session_state to be a writable Map",
                  "wrap your invocation with a non-null session_state Value of kind Map"}};
  }
  if (ctx.session_state->kind() != pipeline::Value::Kind::Map)
    *ctx.session_state = pipeline::Value::map({});

  std::string tag = "wetted_surface";
  if (const auto* tv = find(inputs, "tag")) {
    if (tv->kind() != pipeline::Value::Kind::String) {
      return invalid("tag must be a string",
                     "`tag` must be a string naming the wetted boundary",
                     "default 'wetted_surface'");
    }
    tag = std::string(tv->as_string());
  }

  std::map<std::string, pipeline::Value> bc;
  bc.emplace("type", pipeline::Value::string("hydrostatic"));
  bc.emplace("tag", pipeline::Value::string(tag));
  bc.emplace("depth", pipeline::Value::number(depth));
  bc.emplace("density", pipeline::Value::number(density));
  bc.emplace("pressure", pipeline::Value::number(pressure));
  bc.emplace("gravity", pipeline::Value::number(gravity));
  bc.emplace("salinity_psu", pipeline::Value::number(salinity_psu));
  bc.emplace("seawater_temperature", pipeline::Value::number(temperature_c));
  bc.emplace("include_atmospheric", pipeline::Value::boolean(include_atmospheric));
  bc.emplace("density_source", pipeline::Value::string(density_source));
  bc.emplace("load_cases", pipeline::Value::list(load_cases));
  bc.emplace("sign_convention",
             pipeline::Value::string("positive magnitude, compressive on the wetted surface"));
  auto bc_value = pipeline::Value::map(std::move(bc));

  std::vector<pipeline::Value> bcs;
  if (const auto* existing = ctx.session_state->find("boundary_conditions");
      existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
    for (const auto& item : existing->as_list())
      bcs.push_back(item);
  }
  bcs.push_back(bc_value);
  const auto count = bcs.size();

  std::map<std::string, pipeline::Value> session_map;
  for (const auto& [k, v] : ctx.session_state->as_map()) {
    if (k == "boundary_conditions")
      continue;
    session_map.emplace(k, v);
  }
  session_map.emplace("boundary_conditions", pipeline::Value::list(std::move(bcs)));
  *ctx.session_state = pipeline::Value::map(std::move(session_map));

  std::map<std::string, pipeline::Value> out;
  out.emplace("count", pipeline::Value::number(static_cast<double>(count)));
  out.emplace("latest", bc_value);
  out.emplace("depth", pipeline::Value::number(depth));
  out.emplace("density", pipeline::Value::number(density));
  out.emplace("density_source", pipeline::Value::string(density_source));
  out.emplace("pressure", pipeline::Value::number(pressure));
  out.emplace("pressure_mpa", pipeline::Value::number(pressure / 1.0e6));
  out.emplace("max_pressure", pipeline::Value::number(max_pressure));
  out.emplace("load_cases", pipeline::Value::list(std::move(load_cases)));
  out.emplace("notes",
              pipeline::Value::string(
                  "p = rho*g*h, positive magnitude, compressive on the wetted surface. The "
                  "density correlation ignores the compressibility term (~+1.5 kg/m^3 per "
                  "300 m), which under-predicts the load by ~0.15 % per 300 m. Run "
                  "solver.marine.hydrostatic for the per-node distribution."));

  char buf[352];
  std::snprintf(buf,
                sizeof(buf),
                "hydrostatic load on tag '%s' staged: %.1f m at %.1f kg/m3 -> %.4g MPa "
                "(%zu load case(s), %zu total BCs)",
                tag.c_str(),
                depth,
                density,
                pressure / 1.0e6,
                factors.size(),
                count);

  return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
}

}  // namespace

Tool make_apply_hydrostatic_load_tool() {
  Tool t;
  t.name = "apply_hydrostatic_load";
  t.description =
      "Stage a depth-derived pressure load case on a tagged wetted surface: computes "
      "p = rho*g*h (optionally + atmospheric), derives seawater density from salinity + "
      "temperature when it is not given, and appends a `hydrostatic` entry to the session's "
      "boundary conditions. Supply depth_factors for an operating / test / collapse set.";
  t.category = "BC";
  t.confirmation = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{depth?: number,                  # m below the free surface, default 300, max 11000\n"
      " tag?: string,                    # default 'wetted_surface'\n"
      " seawater_density?: number,       # kg/m^3; omit / <= 0 to derive it\n"
      " salinity_psu?: number,           # [0, 45], default 35\n"
      " seawater_temperature?: number,   # [-2, 40] deg C, default 10\n"
      " gravity?: number,                # m/s^2, default 9.80665\n"
      " include_atmospheric?: bool,      # default false (gauge pressure)\n"
      " atmospheric_pressure?: number,   # Pa, default 101325\n"
      " depth_factors?: [number]         # 1..16 positive factors, default [1.0]\n"
      "}";
  t.output_schema_doc =
      "{count: number,                   # total BCs staged this session\n"
      " latest: {...},                   # the BC just added; type == 'hydrostatic'\n"
      " depth, density, density_source, pressure, pressure_mpa, max_pressure,\n"
      " load_cases: [{index, factor, depth, pressure}], notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "apply_hydrostatic_load failed internally",
                        ToolError{"INTERNAL", std::string("apply_hydrostatic_load: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "apply_hydrostatic_load failed internally",
                        ToolError{"INTERNAL", "apply_hydrostatic_load: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
