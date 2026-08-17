// SPDX-License-Identifier: Apache-2.0
//
// Tool: set_build_orientation
//
// Tool 21 of the ADR-0010 additive ratchet. Scores candidate build
// orientations — the six axis-aligned directions plus anything the caller
// supplies — and stages the winner in session state.
//
// What it computes (mesh present):
//   For each candidate unit build direction b, over the mesh's outer
//   shell (souxmar::core::SurfaceStream, the same triangulated boundary
//   the viewport draws), with A_i the area and n_i the outward unit
//   normal of triangle i:
//     support_area(b)  = sum of A_i over faces with n_i.b < 0 and
//                        tilt_i = acos(|n_i.b|) < overhang_threshold_deg
//     downskin_area(b) = sum of A_i over faces with n_i.b < 0
//     footprint(b)     = 0.5 * sum of A_i * |n_i.b|
//     height(b)        = max_j(x_j.b) - min_j(x_j.b) over mesh nodes
//   The three cost terms are min-max normalised across the candidate set
//   and combined as
//     cost = (w_s*support_area/total_area + w_h*height/height_max
//             + w_f*footprint/footprint_max) / (w_s + w_h + w_f)
//     score = 1 - cost      (higher is better)
//   Default weights 0.60 / 0.25 / 0.15: support area drives
//   post-processing labour and is the dominant DfAM cost driver; height
//   drives the layer count and therefore machine time; the projected
//   footprint is a proxy for per-layer recoat / scan area. Rank by
//   descending score with the candidate index as the tiebreaker, so the
//   ranking is a total order and reproducible byte-for-byte.
//
// What this is NOT:
//   * `footprint` is a shadow area. For a closed convex shell it is the
//     exact silhouette; for a re-entrant part it over-counts overlapping
//     shadows. It is not a support-contact area and not a plate-adhesion
//     area.
//   * The support-area term counts *downskin* area below the tilt
//     threshold. It does not trace support columns to the plate, so it
//     ignores support volume, self-supporting cones, and any support
//     strategy the slicer applies.
//   * `solver.am.overhang` is the mesh-resolved, per-cell version of the
//     same geometry test. This tool exists to answer "which way up?"
//     cheaply, from session state alone, without a plugin.
//   * With no mesh staged the tool degrades to a pure advisory ranking
//     over the candidate list (documented order, all scores zero) and
//     says so in the summary — it never guesses geometry.
//   * The areas are accumulated in SurfaceStream's boundary-face order,
//     which is an std::unordered_map walk, so the summed areas can differ
//     in the last ULP between standard libraries. The *ranking* is
//     immunised against that (scores are compared as integers at 1e-9
//     resolution, index breaking ties); the reported area values are not.
//
// Confirmation::ConfirmOnce, matching the contract and the BC tier: this
// mutates session state (a `boundary_conditions` entry of type
// "build_orientation", plus `manufacturing.build_direction`) which every
// downstream AM stage reads, so the first call per session surfaces a
// chip. It is local and reversible, so subsequent calls are silent —
// exactly the `set_bc` / `set_material` bargain.

#include "souxmar/ai/tool.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/surface_stream.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

// Cap on caller-supplied candidates. Scoring is O(triangles x candidates)
// and an agent looping over a fine sphere sampling is a denial-of-service
// on its own session, not a useful query.
constexpr std::size_t kMaxSuppliedCandidates = 24;

constexpr double kPi = 3.14159265358979323846;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

double dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct Candidate {
  Vec3 dir;              // unit
  std::string label;     // "+Z" / "candidate_0"
  bool supplied = false; // came from the caller, not the axis set
  // Raw geometric measures (0 when no mesh / no surface).
  double support_area = 0.0;
  double downskin_area = 0.0;
  double footprint = 0.0;
  double height = 0.0;
  double score = 0.0;
  std::size_t index = 0;  // insertion order — the ranking tiebreaker
};

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{
      pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

pipeline::Value vec3_value(const Vec3& v) {
  return pipeline::Value::list({pipeline::Value::number(v.x),
                                pipeline::Value::number(v.y),
                                pipeline::Value::number(v.z)});
}

pipeline::Value candidate_value(const Candidate& c, std::size_t rank, bool have_geometry) {
  std::map<std::string, pipeline::Value> m;
  m.emplace("rank", pipeline::Value::number(static_cast<double>(rank)));
  m.emplace("label", pipeline::Value::string(c.label));
  m.emplace("direction", vec3_value(c.dir));
  m.emplace("source", pipeline::Value::string(c.supplied ? "supplied" : "axis"));
  m.emplace("score", pipeline::Value::number(c.score));
  if (have_geometry) {
    m.emplace("support_area_m2", pipeline::Value::number(c.support_area));
    m.emplace("downskin_area_m2", pipeline::Value::number(c.downskin_area));
    m.emplace("projected_footprint_m2", pipeline::Value::number(c.footprint));
    m.emplace("height_m", pipeline::Value::number(c.height));
  }
  return pipeline::Value::map(std::move(m));
}

// Merge one key into a Map Value, preserving every other key. Value is a
// persistent tree, so "mutation" is a rebuild.
pipeline::Value with_key(const pipeline::Value& map_value,
                         const std::string& key,
                         pipeline::Value value) {
  std::map<std::string, pipeline::Value> out;
  if (map_value.kind() == pipeline::Value::Kind::Map) {
    for (const auto& [k, v] : map_value.as_map()) {
      if (k == key)
        continue;
      out.emplace(k, v);
    }
  }
  out.emplace(key, std::move(value));
  return pipeline::Value::map(std::move(out));
}

ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map && inputs.kind() != pipeline::Value::Kind::Null) {
    return invalid("input must be a map",
                   "set_build_orientation input must be a map (or null for all defaults)",
                   "{candidates: [[0,0,1], [1,0,0]], overhang_threshold_deg: 45}");
  }

  double threshold_deg = 45.0;  // contract 3.9 default
  if (const auto* v = find(inputs, "overhang_threshold_deg")) {
    if (v->kind() != pipeline::Value::Kind::Number) {
      return invalid("overhang_threshold_deg must be a number",
                     "`overhang_threshold_deg` must be a number when set",
                     "45 is the usual LPBF self-supporting limit");
    }
    threshold_deg = v->as_number();
    if (!(threshold_deg > 0.0) || !(threshold_deg < 90.0)) {
      return invalid("overhang_threshold_deg out of range",
                     "`overhang_threshold_deg` must be in (0, 90) exclusive",
                     "a face at 0 deg is parallel to the plate; at 90 deg it is vertical");
    }
  }

  double w_support = 0.60;
  double w_height = 0.25;
  double w_footprint = 0.15;
  if (const auto* w = find(inputs, "weights")) {
    if (w->kind() != pipeline::Value::Kind::Map) {
      return invalid("weights must be a map",
                     "`weights` must be a map with optional support / height / footprint entries",
                     "{weights: {support: 1.0, height: 0.0, footprint: 0.0}}");
    }
    const auto read_weight = [&](const char* key, double& target) -> bool {
      const auto* e = w->find(key);
      if (e == nullptr)
        return true;
      if (e->kind() != pipeline::Value::Kind::Number || e->as_number() < 0.0)
        return false;
      target = e->as_number();
      return true;
    };
    if (!read_weight("support", w_support) || !read_weight("height", w_height)
        || !read_weight("footprint", w_footprint)) {
      return invalid("weights must be non-negative numbers",
                     "every entry of `weights` must be a number >= 0",
                     "{weights: {support: 0.6, height: 0.25, footprint: 0.15}}");
    }
    if (w_support + w_height + w_footprint <= 0.0) {
      return invalid("weights sum to zero",
                     "at least one entry of `weights` must be > 0",
                     "the weights are normalised by their sum, so all-zero has no meaning");
    }
  }
  const double w_sum = w_support + w_height + w_footprint;

  // ---- candidate set: supplied first (caller intent is information),
  // then the six axis-aligned directions in a fixed order.
  std::vector<Candidate> candidates;
  const auto push_candidate = [&](const Vec3& dir, std::string label, bool supplied) {
    for (const auto& existing : candidates) {
      if (std::abs(existing.dir.x - dir.x) < 1e-9 && std::abs(existing.dir.y - dir.y) < 1e-9
          && std::abs(existing.dir.z - dir.z) < 1e-9) {
        return;  // duplicate direction — keep the first label
      }
    }
    Candidate c;
    c.dir = dir;
    c.label = std::move(label);
    c.supplied = supplied;
    c.index = candidates.size();
    candidates.push_back(std::move(c));
  };

  if (const auto* cands = find(inputs, "candidates")) {
    if (cands->kind() != pipeline::Value::Kind::List) {
      return invalid("candidates must be a list",
                     "`candidates` must be a list of 3-number direction vectors when set",
                     "{candidates: [[0,0,1], [0.707, 0, 0.707]]}");
    }
    const auto list = cands->as_list();
    if (list.size() > kMaxSuppliedCandidates) {
      return invalid("too many candidates",
                     "`candidates` is limited to 24 entries",
                     "score a coarse set first, then refine around the winner");
    }
    std::size_t n = 0;
    for (const auto& entry : list) {
      if (entry.kind() != pipeline::Value::Kind::List || entry.as_list().size() != 3) {
        return invalid("candidate must be 3 numbers",
                       "every entry of `candidates` must be a list of three numbers",
                       "{candidates: [[0,0,1]]}");
      }
      const auto comps = entry.as_list();
      Vec3 d;
      double len2 = 0.0;
      for (std::size_t i = 0; i < 3; ++i) {
        if (comps[i].kind() != pipeline::Value::Kind::Number) {
          return invalid("candidate must be 3 numbers",
                         "every component of a candidate direction must be a number",
                         "{candidates: [[0,0,1]]}");
        }
        const double val = comps[i].as_number();
        if (i == 0)
          d.x = val;
        else if (i == 1)
          d.y = val;
        else
          d.z = val;
        len2 += val * val;
      }
      if (len2 <= 0.0) {
        return invalid("candidate has zero length",
                       "a candidate direction of zero length has no orientation",
                       "use [0, 0, 1] for a Z-up build");
      }
      const double inv = 1.0 / std::sqrt(len2);
      push_candidate(Vec3{d.x * inv, d.y * inv, d.z * inv}, "candidate_" + std::to_string(n), true);
      ++n;
    }
  }
  push_candidate(Vec3{0.0, 0.0, 1.0}, "+Z", false);
  push_candidate(Vec3{0.0, 0.0, -1.0}, "-Z", false);
  push_candidate(Vec3{1.0, 0.0, 0.0}, "+X", false);
  push_candidate(Vec3{-1.0, 0.0, 0.0}, "-X", false);
  push_candidate(Vec3{0.0, 1.0, 0.0}, "+Y", false);
  push_candidate(Vec3{0.0, -1.0, 0.0}, "-Y", false);

  // ---- geometry, if the session has any ----
  const bool mesh_available = static_cast<bool>(ctx.mesh_handle);
  bool surface_available = false;
  double total_area = 0.0;
  std::size_t triangle_count = 0;
  std::size_t num_nodes = 0;

  if (mesh_available) {
    const auto& mesh = *ctx.mesh_handle;

    // Height from the node cloud (doubles — better than the renderer's
    // float positions, and defined even for edge-only meshes).
    const auto nodes = mesh.nodes_flat();
    num_nodes = nodes.size() / 3;
    for (auto& c : candidates) {
      double lo = 0.0;
      double hi = 0.0;
      for (std::size_t n = 0; n < num_nodes; ++n) {
        const double p = nodes[n * 3 + 0] * c.dir.x + nodes[n * 3 + 1] * c.dir.y
                         + nodes[n * 3 + 2] * c.dir.z;
        if (n == 0) {
          lo = hi = p;
        } else {
          lo = p < lo ? p : lo;
          hi = p > hi ? p : hi;
        }
      }
      c.height = hi - lo;
    }

    const core::SurfaceStream shell(mesh);
    const auto pos = shell.positions();
    const auto idx = shell.indices();
    triangle_count = shell.triangle_count();
    // tilt from the plate is acos(|n.b|), so "tilt < threshold" is
    // "|n.b| > cos(threshold)". A flat ceiling (|n.b| = 1, tilt 0 deg)
    // therefore always needs support; a near-vertical wall never does.
    const double cos_threshold = std::cos(threshold_deg * kPi / 180.0);

    for (std::size_t tri = 0; tri < triangle_count; ++tri) {
      const std::size_t i0 = static_cast<std::size_t>(idx[tri * 3 + 0]) * 3;
      const std::size_t i1 = static_cast<std::size_t>(idx[tri * 3 + 1]) * 3;
      const std::size_t i2 = static_cast<std::size_t>(idx[tri * 3 + 2]) * 3;
      if (i0 + 2 >= pos.size() || i1 + 2 >= pos.size() || i2 + 2 >= pos.size())
        continue;
      // SurfaceStream stores float positions; promote explicitly.
      const Vec3 a{static_cast<double>(pos[i0 + 0]),
                   static_cast<double>(pos[i0 + 1]),
                   static_cast<double>(pos[i0 + 2])};
      const Vec3 b{static_cast<double>(pos[i1 + 0]),
                   static_cast<double>(pos[i1 + 1]),
                   static_cast<double>(pos[i1 + 2])};
      const Vec3 c{static_cast<double>(pos[i2 + 0]),
                   static_cast<double>(pos[i2 + 1]),
                   static_cast<double>(pos[i2 + 2])};
      const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
      const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
      const Vec3 cross{e1.y * e2.z - e1.z * e2.y,
                       e1.z * e2.x - e1.x * e2.z,
                       e1.x * e2.y - e1.y * e2.x};
      const double twice_area = std::sqrt(dot(cross, cross));
      if (!(twice_area > 0.0))
        continue;  // degenerate triangle
      const double area = 0.5 * twice_area;
      const Vec3 normal{cross.x / twice_area, cross.y / twice_area, cross.z / twice_area};
      total_area += area;
      for (auto& cand : candidates) {
        const double d = dot(normal, cand.dir);
        const double ad = std::abs(d);
        cand.footprint += 0.5 * area * ad;
        if (d < 0.0) {
          cand.downskin_area += area;
          if (ad > cos_threshold)
            cand.support_area += area;
        }
      }
    }
    surface_available = total_area > 0.0 && triangle_count > 0;
  }

  // ---- score ----
  // "Geometry was evaluated" needs an actual node cloud: an empty mesh
  // handle would otherwise score every candidate identically and report
  // it as if it had been measured.
  const bool have_geometry = mesh_available && num_nodes > 0;
  if (have_geometry) {
    double height_max = 0.0;
    double footprint_max = 0.0;
    for (const auto& c : candidates) {
      height_max = c.height > height_max ? c.height : height_max;
      footprint_max = c.footprint > footprint_max ? c.footprint : footprint_max;
    }
    for (auto& c : candidates) {
      const double support_term =
          surface_available && total_area > 0.0 ? c.support_area / total_area : 0.0;
      const double height_term = height_max > 0.0 ? c.height / height_max : 0.0;
      const double footprint_term = footprint_max > 0.0 ? c.footprint / footprint_max : 0.0;
      const double cost =
          (w_support * support_term + w_height * height_term + w_footprint * footprint_term)
          / w_sum;
      c.score = 1.0 - cost;
    }
  }

  // Deterministic total order: descending quantised score, then
  // insertion index. The quantisation matters — the areas are summed in
  // SurfaceStream's boundary-face order, which is an unordered_map walk
  // and therefore may differ between standard libraries; two orientations
  // that are mathematically tied can land a last-ULP apart. Comparing
  // integers at 1e-9 resolution keeps the ranking byte-identical across
  // platforms while the reported score stays exact.
  std::vector<const Candidate*> ranked;
  ranked.reserve(candidates.size());
  for (const auto& c : candidates)
    ranked.push_back(&c);
  const auto rank_key = [](const Candidate& c) -> long long {
    return std::llround(c.score * 1.0e9);
  };
  for (std::size_t i = 1; i < ranked.size(); ++i) {  // insertion sort: stable, tiny n, no
    std::size_t j = i;                              // dependency on std::sort's tiebreak
    while (j > 0
           && (rank_key(*ranked[j]) > rank_key(*ranked[j - 1])
               || (rank_key(*ranked[j]) == rank_key(*ranked[j - 1])
                   && ranked[j]->index < ranked[j - 1]->index))) {
      const auto* tmp = ranked[j - 1];
      ranked[j - 1] = ranked[j];
      ranked[j] = tmp;
      --j;
    }
  }

  std::vector<pipeline::Value> ranking;
  ranking.reserve(ranked.size());
  for (std::size_t r = 0; r < ranked.size(); ++r)
    ranking.push_back(candidate_value(*ranked[r], r, have_geometry));
  const Candidate& winner = *ranked.front();

  // ---- stage the winner ----
  bool staged = false;
  if (ctx.session_state != nullptr) {
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map)
      *ctx.session_state = pipeline::Value::map({});

    std::string tag = "build_plate";
    if (const auto* tv = find(inputs, "tag");
        tv != nullptr && tv->kind() == pipeline::Value::Kind::String) {
      tag = std::string(tv->as_string());
    }

    std::map<std::string, pipeline::Value> bc;
    bc.emplace("type", pipeline::Value::string("build_orientation"));
    bc.emplace("tag", pipeline::Value::string(tag));
    bc.emplace("direction", vec3_value(winner.dir));
    bc.emplace("label", pipeline::Value::string(winner.label));
    bc.emplace("score", pipeline::Value::number(winner.score));
    bc.emplace("overhang_threshold_deg", pipeline::Value::number(threshold_deg));
    bc.emplace("advisory_only", pipeline::Value::boolean(!have_geometry));
    if (have_geometry) {
      bc.emplace("support_area_m2", pipeline::Value::number(winner.support_area));
      bc.emplace("height_m", pipeline::Value::number(winner.height));
    }

    std::vector<pipeline::Value> bcs;
    if (const auto* existing = ctx.session_state->find("boundary_conditions");
        existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
      for (const auto& item : existing->as_list())
        bcs.push_back(item);
    }
    bcs.push_back(pipeline::Value::map(std::move(bc)));

    auto session = with_key(
        *ctx.session_state, "boundary_conditions", pipeline::Value::list(std::move(bcs)));

    // Mirror into `manufacturing.build_direction` so the AM tools that
    // read the setup bag (estimate_build_cost, propose_am_setup output)
    // agree with the staged BC without re-deriving it.
    const auto* existing_mfg = session.find("manufacturing");
    auto mfg = existing_mfg != nullptr && existing_mfg->kind() == pipeline::Value::Kind::Map
                   ? *existing_mfg
                   : pipeline::Value::map({});
    mfg = with_key(mfg, "build_direction", vec3_value(winner.dir));
    mfg = with_key(mfg, "overhang_threshold_deg", pipeline::Value::number(threshold_deg));
    session = with_key(session, "manufacturing", std::move(mfg));

    *ctx.session_state = std::move(session);
    staged = true;
  }

  std::map<std::string, pipeline::Value> weights_out;
  weights_out.emplace("support", pipeline::Value::number(w_support));
  weights_out.emplace("height", pipeline::Value::number(w_height));
  weights_out.emplace("footprint", pipeline::Value::number(w_footprint));

  std::map<std::string, pipeline::Value> out;
  out.emplace("winner", candidate_value(winner, 0, have_geometry));
  out.emplace("build_direction", vec3_value(winner.dir));
  out.emplace("ranking", pipeline::Value::list(std::move(ranking)));
  out.emplace("candidate_count", pipeline::Value::number(static_cast<double>(candidates.size())));
  out.emplace("overhang_threshold_deg", pipeline::Value::number(threshold_deg));
  out.emplace("weights", pipeline::Value::map(std::move(weights_out)));
  out.emplace("mesh_available", pipeline::Value::boolean(mesh_available));
  out.emplace("surface_available", pipeline::Value::boolean(surface_available));
  out.emplace("advisory_only", pipeline::Value::boolean(!have_geometry));
  out.emplace("triangles_evaluated", pipeline::Value::number(static_cast<double>(triangle_count)));
  out.emplace("total_surface_area_m2", pipeline::Value::number(total_area));
  out.emplace("staged", pipeline::Value::boolean(staged));
  out.emplace(
      "notes",
      pipeline::Value::string(
          have_geometry
              ? "Support area counts downskin faces below the tilt threshold; it is not a "
                "support-volume estimate. Footprint is a shadow area and over-counts "
                "re-entrant geometry. Run solver.am.overhang for the mesh-resolved answer."
              : "No mesh geometry staged: this is an advisory ranking over the candidate list "
                "in the "
                "documented order (supplied candidates first, then +Z, -Z, +X, -X, +Y, -Y). "
                "No geometry was evaluated. Call `mesh` first for a scored ranking."));

  char buf[416];
  if (have_geometry) {
    std::snprintf(buf,
                  sizeof(buf),
                  "build orientation %s [%.3f, %.3f, %.3f] staged: score %.3f of %zu candidates "
                  "(support %.4g m2 of %.4g m2, height %.4g m, %zu triangles)",
                  winner.label.c_str(),
                  winner.dir.x,
                  winner.dir.y,
                  winner.dir.z,
                  winner.score,
                  candidates.size(),
                  winner.support_area,
                  total_area,
                  winner.height,
                  triangle_count);
  } else {
    std::snprintf(buf,
                  sizeof(buf),
                  "build orientation %s [%.3f, %.3f, %.3f] staged: advisory only — no mesh "
                  "geometry on the session, so support area / height / footprint were NOT "
                  "evaluated across the %zu candidates",
                  winner.label.c_str(),
                  winner.dir.x,
                  winner.dir.y,
                  winner.dir.z,
                  candidates.size());
  }

  return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
}

}  // namespace

Tool make_set_build_orientation_tool() {
  Tool t;
  t.name = "set_build_orientation";
  t.description =
      "Score candidate build orientations (the six axis-aligned directions plus any you "
      "supply) on projected support area, build height and projected footprint, then stage "
      "the winner in session state as a `build_orientation` entry plus "
      "`manufacturing.build_direction`. Uses the staged mesh when there is one; with no mesh "
      "it returns an advisory ranking over the candidate list and says so.";
  t.category = "BC";
  t.confirmation = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{candidates?: [[x, y, z], ...],        # up to 24; normalised internally\n"
      " overhang_threshold_deg?: number,      # (0, 90), default 45\n"
      " weights?: {support?: number,          # default 0.60\n"
      "            height?: number,           # default 0.25\n"
      "            footprint?: number},       # default 0.15\n"
      " tag?: string                          # BC tag, default 'build_plate'\n"
      "}";
  t.output_schema_doc =
      "{winner: {label, direction, score, support_area_m2, downskin_area_m2,\n"
      "          projected_footprint_m2, height_m, source},\n"
      " build_direction: [x, y, z], ranking: [ ...same shape, best first... ],\n"
      " candidate_count, overhang_threshold_deg, weights: {...},\n"
      " mesh_available: bool, surface_available: bool, advisory_only: bool,\n"
      " triangles_evaluated, total_surface_area_m2, staged: bool, notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "set_build_orientation failed internally",
                        ToolError{"INTERNAL", std::string("set_build_orientation: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "set_build_orientation failed internally",
                        ToolError{"INTERNAL", "set_build_orientation: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
