// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the agent tool framework + the 5 v1 tools. Mock
// dispatcher / mock registry keep the tests in-process and fast.

#include "souxmar/ai/audit_log.h"
#include "souxmar/ai/tool.h"
#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ai = souxmar::ai;
namespace pl = souxmar::pipeline;

namespace {

// A dispatcher that returns a hand-built StageOutput on demand. The
// `mesh` and `solve` tools route through this rather than real plugins.
class FakeDispatcher : public pl::IDispatcher {
 public:
  std::shared_ptr<souxmar::core::Mesh> preset_mesh;
  std::shared_ptr<souxmar::core::Field> preset_field;
  std::string preset_error;
  std::vector<std::string> calls;

  pl::DispatchResult dispatch(const pl::DispatchContext& ctx) override {
    calls.emplace_back(ctx.capability_id);
    if (!preset_error.empty())
      return pl::DispatchError{preset_error};

    auto so = std::make_shared<pl::StageOutput>();
    if (preset_mesh) {
      so->kind = pl::StageOutput::Kind::Mesh;
      so->mesh = preset_mesh;
    } else if (preset_field) {
      so->kind = pl::StageOutput::Kind::Field;
      so->field = preset_field;
    }
    return std::static_pointer_cast<void>(so);
  }
};

// ============================================================================
// Framework
// ============================================================================

TEST(AiToolRegistry, AddAndLookup) {
  ai::ToolRegistry r;
  EXPECT_EQ(r.size(), 0u);

  ai::Tool t;
  t.name = "ping";
  t.handler = [](const pl::Value&, ai::ToolContext&) -> ai::ToolResult {
    return {pl::Value::string("pong"), "ok", std::nullopt};
  };
  r.add(std::move(t));
  EXPECT_EQ(r.size(), 1u);
  ASSERT_NE(r.find("ping"), nullptr);
  EXPECT_EQ(r.find("ping")->name, "ping");
  EXPECT_EQ(r.find("nope"), nullptr);
  EXPECT_EQ(r.list(), std::vector<std::string>{"ping"});
}

TEST(AiToolDispatch, NotFoundReturnsStructuredError) {
  ai::ToolRegistry r;
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "missing", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "NOT_FOUND");
}

TEST(AiToolDispatch, ConfirmationAutoRunsWithoutPrompter) {
  ai::ToolRegistry r;
  bool ran = false;
  ai::Tool t;
  t.name = "auto-tool";
  t.confirmation = ai::Confirmation::Auto;
  t.handler = [&](const pl::Value&, ai::ToolContext&) {
    ran = true;
    return ai::ToolResult{pl::Value::null_value(), "did the thing", std::nullopt};
  };
  r.add(std::move(t));

  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "auto-tool", pl::Value::null_value(), ctx, policy);
  EXPECT_TRUE(ran);
  EXPECT_FALSE(out.error.has_value());
}

TEST(AiToolDispatch, ConfirmAlwaysBlockedWithoutPrompter) {
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "destructive";
  t.confirmation = ai::Confirmation::ConfirmAlways;
  bool ran = false;
  t.handler = [&](const pl::Value&, ai::ToolContext&) {
    ran = true;
    return ai::ToolResult{};
  };
  r.add(std::move(t));

  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "destructive", pl::Value::null_value(), ctx, policy);
  EXPECT_FALSE(ran);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "NOT_CONFIRMED");
}

TEST(AiToolDispatch, ConfirmAlwaysPromptedEveryCall) {
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "destructive";
  t.confirmation = ai::Confirmation::ConfirmAlways;
  int run_count = 0;
  t.handler = [&](const pl::Value&, ai::ToolContext&) {
    ++run_count;
    return ai::ToolResult{};
  };
  r.add(std::move(t));

  int prompt_count = 0;
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [&](const ai::Tool&, const pl::Value&) {
    ++prompt_count;
    return true;
  };

  for (int i = 0; i < 3; ++i) {
    auto out = ai::dispatch_tool(r, "destructive", pl::Value::null_value(), ctx, policy);
    EXPECT_FALSE(out.error.has_value());
  }
  EXPECT_EQ(prompt_count, 3);
  EXPECT_EQ(run_count, 3);
}

TEST(AiToolDispatch, ConfirmOncePromptsOnce) {
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "once";
  t.confirmation = ai::Confirmation::ConfirmOnce;
  t.handler = [](const pl::Value&, ai::ToolContext&) { return ai::ToolResult{}; };
  r.add(std::move(t));

  int prompt_count = 0;
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [&](const ai::Tool&, const pl::Value&) {
    ++prompt_count;
    return true;
  };

  // NB: no `(void)` cast on these — ASSERT_FALSE needs a bool, and a
  // void operand does not compile.
  ASSERT_FALSE(
      ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
  ASSERT_FALSE(
      ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
  ASSERT_FALSE(
      ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
  EXPECT_EQ(prompt_count, 1) << "ConfirmOnce should prompt only on the first call";
  EXPECT_TRUE(policy.confirmed_once.contains("once"));
}

TEST(AiToolDispatch, PromptDenialReturnsDenied) {
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "denied";
  t.confirmation = ai::Confirmation::ConfirmAlways;
  bool ran = false;
  t.handler = [&](const pl::Value&, ai::ToolContext&) {
    ran = true;
    return ai::ToolResult{};
  };
  r.add(std::move(t));

  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return false; };
  auto out = ai::dispatch_tool(r, "denied", pl::Value::null_value(), ctx, policy);
  EXPECT_FALSE(ran);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "DENIED");
}

TEST(AiToolDispatch, OverridePolicyTakesPrecedence) {
  // A tool declared ConfirmAlways may be overridden to Auto (e.g. --yes).
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "destructive";
  t.confirmation = ai::Confirmation::ConfirmAlways;
  bool ran = false;
  t.handler = [&](const pl::Value&, ai::ToolContext&) {
    ran = true;
    return ai::ToolResult{};
  };
  r.add(std::move(t));

  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.overrides["destructive"] = ai::Confirmation::Auto;
  auto out = ai::dispatch_tool(r, "destructive", pl::Value::null_value(), ctx, policy);
  EXPECT_TRUE(ran);
  EXPECT_FALSE(out.error.has_value());
}

TEST(AiToolDispatch, HandlerExceptionSurfacedAsInternalError) {
  ai::ToolRegistry r;
  ai::Tool t;
  t.name = "throws";
  t.handler = [](const pl::Value&, ai::ToolContext&) -> ai::ToolResult {
    throw std::runtime_error("kaboom");
  };
  r.add(std::move(t));

  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "throws", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INTERNAL");
  EXPECT_NE(out.error->message.find("kaboom"), std::string::npos);
}

// ============================================================================
// Default v1 registry — contents + per-tool behaviour
// ============================================================================

// The five tools Sprint 4 push 3 shipped must still be present and still
// be named the same — the catalogue only ever grows (ADR-0011 ratchet).
// The size assertion tracks the whole catalogue, which is 24 after the
// manufacturing block (18 + 6).
TEST(AiDefaultTools, RegistryStillContainsTheOriginalFiveTools) {
  auto r = ai::default_v1_tools();
  const auto list = r.list();
  EXPECT_EQ(list.size(), 24u);
  EXPECT_NE(r.find("read_geometry_summary"), nullptr);
  EXPECT_NE(r.find("mesh"), nullptr);
  EXPECT_NE(r.find("set_bc"), nullptr);
  EXPECT_NE(r.find("solve"), nullptr);
  EXPECT_NE(r.find("screenshot_viewport"), nullptr);
}

TEST(AiTools_ReadGeometrySummary, ReadsFromSessionStateWhenInputAbsent) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({{"geometry",
                                       pl::Value::map({
                                           {"num_vertices", pl::Value::number(8)},
                                           {"num_edges", pl::Value::number(12)},
                                           {"num_faces", pl::Value::number(6)},
                                           {"num_solids", pl::Value::number(1)},
                                       })}});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "read_geometry_summary", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  ASSERT_EQ(out.data.kind(), pl::Value::Kind::Map);
  EXPECT_EQ(out.data.find("num_vertices")->as_number(), 8.0);
  EXPECT_EQ(out.data.find("num_solids")->as_number(), 1.0);
  EXPECT_NE(out.summary.find("8 v"), std::string::npos);
}

TEST(AiTools_ReadGeometrySummary, NotAvailableWhenAbsentEverywhere) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "read_geometry_summary", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "NOT_AVAILABLE");
}

TEST(AiTools_SetBc, AppendsToSessionState) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;

  ai::ConfirmationPolicy policy;
  // Force-Auto so the test doesn't need a prompter.
  policy.overrides["set_bc"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("inlet")},
      {"type", pl::Value::string("dirichlet")},
      {"value", pl::Value::number(0.0)},
  });
  auto out = ai::dispatch_tool(r, "set_bc", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  ASSERT_EQ(session.kind(), pl::Value::Kind::Map);
  const auto* bcs = session.find("boundary_conditions");
  ASSERT_NE(bcs, nullptr);
  ASSERT_EQ(bcs->kind(), pl::Value::Kind::List);
  EXPECT_EQ(bcs->as_list().size(), 1u);

  // A second call appends.
  out = ai::dispatch_tool(r, "set_bc", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(session.find("boundary_conditions")->as_list().size(), 2u);
}

TEST(AiTools_SetBc, RejectsInvalidType) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["set_bc"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("inlet")},
      {"type", pl::Value::string("greasy")},
      {"value", pl::Value::number(0.0)},
  });
  auto out = ai::dispatch_tool(r, "set_bc", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_Mesh, DispatchSuccessUpdatesContext) {
  auto r = ai::default_v1_tools();

  // Build a tiny mesh: 1 tet (4 nodes, 1 cell).
  auto mesh = std::make_shared<souxmar::core::Mesh>();
  const auto n0 = mesh->add_node({0.0, 0.0, 0.0});
  const auto n1 = mesh->add_node({1.0, 0.0, 0.0});
  const auto n2 = mesh->add_node({0.0, 1.0, 0.0});
  const auto n3 = mesh->add_node({0.0, 0.0, 1.0});
  std::array<souxmar::core::NodeIndex, 4> nodes{n0, n1, n2, n3};
  (void)mesh->add_cell(souxmar::core::ElementType::Tet4, nodes);

  FakeDispatcher dispatcher;
  dispatcher.preset_mesh = mesh;

  // The mesh tool only consults registry->find_mesher(...) for existence;
  // we have to actually register the capability against a Registry for
  // that check to pass. Use the C++ add_mesher API + the hello-mesher's
  // vtable shape via a no-op vtable.
  souxmar::plugin::Registry registry;
  static souxmar_mesher_vtable_t fake_vt{
      SOUXMAR_ABI_VERSION_MAJOR,
      [](const souxmar_geometry_t*, const souxmar_mesher_options_t*, souxmar_mesh_t**, void*)
          -> souxmar_status_t { return souxmar_status_ok(); },
      nullptr};
  auto reg_res = registry.add_mesher(std::string{"mesher.fake"}, "fake-plugin", &fake_vt, nullptr);
  ASSERT_TRUE(std::holds_alternative<std::monostate>(reg_res));

  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ai::ConfirmationPolicy policy;

  auto input = pl::Value::map({{"capability_id", pl::Value::string("mesher.fake")}});
  auto out = ai::dispatch_tool(r, "mesh", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(dispatcher.calls, std::vector<std::string>{"mesher.fake"});
  EXPECT_EQ(ctx.mesh_handle, mesh) << "mesh handle should be stashed for solve";
  ASSERT_EQ(out.data.kind(), pl::Value::Kind::Map);
  EXPECT_EQ(out.data.find("num_nodes")->as_number(), 4.0);
  EXPECT_EQ(out.data.find("num_cells")->as_number(), 1.0);
}

TEST(AiTools_Mesh, MissingCapabilityReturnsStructuredError) {
  auto r = ai::default_v1_tools();
  souxmar::plugin::Registry registry;
  FakeDispatcher dispatcher;
  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ai::ConfirmationPolicy policy;
  auto input = pl::Value::map({{"capability_id", pl::Value::string("nope")}});
  auto out = ai::dispatch_tool(r, "mesh", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "PLUGIN_NOT_FOUND");
}

TEST(AiTools_Solve, RequiresMeshFirst) {
  auto r = ai::default_v1_tools();
  souxmar::plugin::Registry registry;
  FakeDispatcher dispatcher;
  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ai::ConfirmationPolicy policy;
  policy.overrides["solve"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({{"capability_id", pl::Value::string("solver.linear")}});
  auto out = ai::dispatch_tool(r, "solve", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "PRECONDITION_FAILED");
}

TEST(AiTools_ScreenshotViewport, ReturnsNotAvailableInHeadlessBuild) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.overrides["screenshot_viewport"] = ai::Confirmation::Auto;
  auto out = ai::dispatch_tool(r, "screenshot_viewport", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "NOT_AVAILABLE");
}

// ============================================================================
// Value ↔ YAML round-trip (CLI relies on these)
// ============================================================================

TEST(ValueYaml, ScalarRoundtrip) {
  EXPECT_EQ(pl::parse_value_yaml("42").kind(), pl::Value::Kind::Number);
  EXPECT_EQ(pl::parse_value_yaml("hello").kind(), pl::Value::Kind::String);
  EXPECT_EQ(pl::parse_value_yaml("true").kind(), pl::Value::Kind::Bool);
}

TEST(ValueYaml, MapEmitParseRoundtrip) {
  auto v = pl::Value::map({
      {"a", pl::Value::number(1.5)},
      {"b", pl::Value::string("hi there")},
      {"c", pl::Value::list({pl::Value::number(1), pl::Value::number(2)})},
  });
  const auto yaml = pl::emit_value_yaml(v);
  const auto back = pl::parse_value_yaml(yaml);
  EXPECT_EQ(v, back) << "emitted YAML:\n" << yaml;
}

TEST(ValueYaml, StageRefShorthand) {
  auto v = pl::parse_value_yaml("{from: stage42}");
  ASSERT_EQ(v.kind(), pl::Value::Kind::Stage);
  EXPECT_EQ(v.as_stage().stage_id, "stage42");
}

// ============================================================================
// Sprint 5 push 2 — new tools
// ============================================================================

TEST(AiDefaultTools_v2, RegistryContainsEveryCatalogueTool) {
  auto r = ai::default_v1_tools();
  // Sprint 8 push 5 took the catalogue 16 → 18, the shape ADR-0011
  // froze as tool-contract v1. The manufacturing block adds tools
  // 19-24 via the ADR-0010 additive ratchet: 18 + 6 = 24.
  EXPECT_EQ(r.list().size(), 24u);
  for (const auto* expected : {"read_geometry_summary",
                               "mesh",
                               "set_bc",
                               "solve",
                               "screenshot_viewport",
                               "query_field",
                               "compute_field",
                               "propose_pipeline",
                               "query_mesh_quality",
                               "set_material",
                               "list_plugins",
                               "apply_pipeline_diff",
                               "export_results",
                               "apply_inlet",
                               "apply_wall",
                               "apply_outlet",
                               "propose_cfd_setup",
                               "validate_bcs",
                               // Manufacturing block — tools 19-24.
                               "propose_am_setup",
                               "check_printability",
                               "set_build_orientation",
                               "estimate_build_cost",
                               "apply_hydrostatic_load",
                               "check_marine_integrity"}) {
    EXPECT_NE(r.find(expected), nullptr) << "missing tool: " << expected;
  }
}

// ============================================================================
// Sprint 6 push 3 — new tools (set_material / list_plugins /
// apply_pipeline_diff / export_results).
// ============================================================================

TEST(AiTools_SetMaterial, AppendsToSessionStateMaterials) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  // ConfirmOnce tools without a prompter return NOT_CONFIRMED — feed
  // an always-yes prompter so the handler runs.
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };

  std::map<std::string, pl::Value> input;
  input.emplace("tag", pl::Value::string("body"));
  input.emplace("model", pl::Value::string("linear_elastic"));
  std::map<std::string, pl::Value> props;
  props.emplace("E", pl::Value::number(210e9));
  props.emplace("nu", pl::Value::number(0.3));
  input.emplace("properties", pl::Value::map(std::move(props)));

  auto out = ai::dispatch_tool(r, "set_material", pl::Value::map(std::move(input)), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << (out.error ? out.error->message : "");
  EXPECT_EQ(out.data.find("count")->as_number(), 1.0);

  const auto* mats = session.find("materials");
  ASSERT_NE(mats, nullptr);
  ASSERT_EQ(mats->kind(), pl::Value::Kind::List);
  ASSERT_EQ(mats->as_list().size(), 1u);
  EXPECT_EQ(mats->as_list()[0].find("tag")->as_string(), "body");
  EXPECT_EQ(mats->as_list()[0].find("model")->as_string(), "linear_elastic");
}

TEST(AiTools_SetMaterial, RejectsMissingProperties) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };

  std::map<std::string, pl::Value> input;
  input.emplace("tag", pl::Value::string("body"));
  input.emplace("model", pl::Value::string("linear_elastic"));
  // No `properties` map.
  auto out = ai::dispatch_tool(r, "set_material", pl::Value::map(std::move(input)), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ListPlugins, RequiresRegistry) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;  // no registry
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "list_plugins", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INTERNAL");
}

TEST(AiTools_ListPlugins, ReturnsEmptyOnEmptyRegistry) {
  auto r = ai::default_v1_tools();
  souxmar::plugin::Registry empty;
  ai::ToolContext ctx;
  ctx.registry = &empty;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "list_plugins", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value());
  EXPECT_EQ(out.data.find("count_total")->as_number(), 0.0);
  const auto* caps = out.data.find("capabilities");
  ASSERT_NE(caps, nullptr);
  ASSERT_EQ(caps->kind(), pl::Value::Kind::List);
  EXPECT_EQ(caps->as_list().size(), 0u);
}

TEST(AiTools_ApplyPipelineDiff, AddStageAndReValidate) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };

  // Base: a 1-stage pipeline (mesher).
  std::map<std::string, pl::Value> mesh_stage;
  mesh_stage.emplace("id", pl::Value::string("mesh"));
  mesh_stage.emplace("plugin", pl::Value::string("mesher.tetra.hello"));
  std::vector<pl::Value> stages{pl::Value::map(std::move(mesh_stage))};
  std::map<std::string, pl::Value> base_map;
  base_map.emplace("version", pl::Value::number(1));
  base_map.emplace("stages", pl::Value::list(std::move(stages)));

  // Op: add a writer downstream.
  std::map<std::string, pl::Value> add_stage_map;
  add_stage_map.emplace("id", pl::Value::string("write"));
  add_stage_map.emplace("plugin", pl::Value::string("writer.vtu"));
  std::map<std::string, pl::Value> write_input;
  std::map<std::string, pl::Value> from_ref;
  from_ref.emplace("from", pl::Value::string("mesh"));
  write_input.emplace("mesh", pl::Value::map(std::move(from_ref)));
  write_input.emplace("path", pl::Value::string("/tmp/out.vtu"));
  add_stage_map.emplace("input", pl::Value::map(std::move(write_input)));

  std::map<std::string, pl::Value> add_op;
  add_op.emplace("op", pl::Value::string("add"));
  add_op.emplace("after", pl::Value::string("mesh"));
  add_op.emplace("stage", pl::Value::map(std::move(add_stage_map)));

  std::vector<pl::Value> ops{pl::Value::map(std::move(add_op))};

  std::map<std::string, pl::Value> input;
  input.emplace("base", pl::Value::map(std::move(base_map)));
  input.emplace("ops", pl::Value::list(std::move(ops)));

  auto out =
      ai::dispatch_tool(r, "apply_pipeline_diff", pl::Value::map(std::move(input)), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << (out.error ? out.error->message : "");
  EXPECT_EQ(out.data.find("parsed_stages")->as_number(), 2.0);
  EXPECT_EQ(out.data.find("ops_applied")->as_number(), 1.0);
  EXPECT_NE(out.data.find("yaml")->as_string().find("writer.vtu"), std::string_view::npos);
}

TEST(AiTools_ApplyPipelineDiff, DanglingReferenceTripsParser) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };

  // Base: mesh → write. Remove `mesh` so `write` dangles.
  std::map<std::string, pl::Value> mesh_stage;
  mesh_stage.emplace("id", pl::Value::string("mesh"));
  mesh_stage.emplace("plugin", pl::Value::string("mesher.tetra.hello"));

  std::map<std::string, pl::Value> write_stage;
  write_stage.emplace("id", pl::Value::string("write"));
  write_stage.emplace("plugin", pl::Value::string("writer.vtu"));
  std::map<std::string, pl::Value> wi;
  std::map<std::string, pl::Value> ref;
  ref.emplace("from", pl::Value::string("mesh"));
  wi.emplace("mesh", pl::Value::map(std::move(ref)));
  write_stage.emplace("input", pl::Value::map(std::move(wi)));

  std::vector<pl::Value> stages{pl::Value::map(std::move(mesh_stage)),
                                pl::Value::map(std::move(write_stage))};
  std::map<std::string, pl::Value> base_map;
  base_map.emplace("version", pl::Value::number(1));
  base_map.emplace("stages", pl::Value::list(std::move(stages)));

  std::map<std::string, pl::Value> remove_op;
  remove_op.emplace("op", pl::Value::string("remove"));
  remove_op.emplace("id", pl::Value::string("mesh"));
  std::vector<pl::Value> ops{pl::Value::map(std::move(remove_op))};

  std::map<std::string, pl::Value> input;
  input.emplace("base", pl::Value::map(std::move(base_map)));
  input.emplace("ops", pl::Value::list(std::move(ops)));

  auto out =
      ai::dispatch_tool(r, "apply_pipeline_diff", pl::Value::map(std::move(input)), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ExportResults, RequiresMeshHandle) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };

  std::map<std::string, pl::Value> input;
  input.emplace("capability_id", pl::Value::string("writer.vtu"));
  input.emplace("path", pl::Value::string("/tmp/x.vtu"));
  auto out = ai::dispatch_tool(r, "export_results", pl::Value::map(std::move(input)), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  // No registry / dispatcher attached → INTERNAL. With them and no
  // mesh handle → PRECONDITION_FAILED. Either reaches an early-error
  // path without invoking a writer.
  EXPECT_TRUE(out.error->code == "INTERNAL" || out.error->code == "PRECONDITION_FAILED"
              || out.error->code == "PLUGIN_NOT_FOUND");
}

TEST(AiTools_QueryMeshQuality, RequiresMeshHandle) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "query_mesh_quality", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  // Without a registry/dispatcher we fail on INTERNAL; with them but no
  // mesh handle we'd fail on PRECONDITION_FAILED. Either is acceptable
  // — the point is we never reach the dispatcher.
  EXPECT_TRUE(out.error->code == "INTERNAL" || out.error->code == "PRECONDITION_FAILED");
}

TEST(AiTools_QueryField, RequiresFieldHandle) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "query_field", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "PRECONDITION_FAILED");
}

TEST(AiTools_QueryField, AggregatesOverField) {
  auto r = ai::default_v1_tools();
  auto field = std::make_shared<souxmar::core::Field>("test-field",
                                                      souxmar::core::FieldLocation::Cell,
                                                      souxmar::core::FieldKind::Scalar,
                                                      /*count=*/4);
  // Field's data() is zero-initialised on construction; mutate via the
  // mutable span so we can exercise the aggregator with a known input.
  auto data = field->data();
  data[0] = -3.0;
  data[1] = 1.0;
  data[2] = 5.0;
  data[3] = 2.0;

  ai::ToolContext ctx;
  ctx.field_handle = field;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "query_field", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("min")->as_number(), -3.0);
  EXPECT_EQ(out.data.find("max")->as_number(), 5.0);
  EXPECT_DOUBLE_EQ(out.data.find("mean")->as_number(), (-3.0 + 1.0 + 5.0 + 2.0) / 4.0);
  EXPECT_EQ(out.data.find("count")->as_number(), 4.0);
  EXPECT_EQ(std::string(out.data.find("location")->as_string()), "cell");
  EXPECT_EQ(std::string(out.data.find("kind")->as_string()), "scalar");
}

TEST(AiTools_ComputeField, RejectsMissingCapabilityIdInput) {
  // Sprint 5 push 3 activated compute_field against the postproc C ABI.
  // Without a capability_id we should get a clean INVALID_ARGUMENT now
  // (was NOT_AVAILABLE while the stub shipped).
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  policy.overrides["compute_field"] = ai::Confirmation::Auto;
  auto out = ai::dispatch_tool(r, "compute_field", pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ComputeField, RequiresMeshAndFieldHandles) {
  auto r = ai::default_v1_tools();
  souxmar::plugin::Registry registry;
  FakeDispatcher dispatcher;
  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ai::ConfirmationPolicy policy;
  policy.overrides["compute_field"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({{"capability_id", pl::Value::string("postproc.x")}});
  auto out = ai::dispatch_tool(r, "compute_field", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  // The order in compute_field.cpp checks registry->find_postproc before
  // mesh_handle, so a missing capability surfaces PLUGIN_NOT_FOUND
  // first. Either is correct contract.
  EXPECT_TRUE(out.error->code == "PLUGIN_NOT_FOUND" || out.error->code == "PRECONDITION_FAILED")
      << "unexpected code: " << out.error->code;
}

TEST(AiTools_ProposePipeline, RoundTripsThroughParser) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;

  // Build a valid 2-stage spec: mesher → writer (mirrors the cantilever).
  auto spec = pl::Value::map({
      {"version", pl::Value::number(1)},
      {"stages",
       pl::Value::list({
           pl::Value::map({
               {"id", pl::Value::string("mesh")},
               {"plugin", pl::Value::string("mesher.tetra.hello")},
           }),
           pl::Value::map({
               {"id", pl::Value::string("write")},
               {"plugin", pl::Value::string("writer.vtu")},
               {"input",
                pl::Value::map({
                    {"mesh", pl::Value::stage_ref("mesh")},
                    {"path", pl::Value::string("out.vtu")},
                })},
           }),
       })},
  });
  auto out = ai::dispatch_tool(r, "propose_pipeline", spec, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("parsed_stages")->as_number(), 2.0);
  EXPECT_NE(std::string(out.data.find("yaml")->as_string()).find("mesher.tetra.hello"),
            std::string::npos);
}

TEST(AiTools_ProposePipeline, RejectsBadSpec) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  // Missing required stages list.
  auto spec = pl::Value::map({{"version", pl::Value::number(1)}});
  auto out = ai::dispatch_tool(r, "propose_pipeline", spec, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

// ============================================================================
// Audit log + session budget
// ============================================================================

class AuditLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::random_device rd;
    path_ = std::filesystem::temp_directory_path()
            / ("souxmar-audit-test-" + std::to_string(rd()) + ".log");
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  std::filesystem::path path_;
};

TEST_F(AuditLogTest, AppendWritesOneLinePerEntry) {
  {
    ai::AuditLog log(path_);
    ai::AuditLog::Entry e;
    e.tool_name = "mesh";
    e.outcome = "ok";
    e.summary = "mesh: 4 nodes";
    e.input_hash = "deadbeefcafebabe";
    e.duration = std::chrono::milliseconds{42};
    log.append(e);
    log.append(e);
  }
  std::ifstream in(path_);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  // Two entries → two newlines.
  EXPECT_EQ(std::count(contents.begin(), contents.end(), '\n'), 2);
  EXPECT_NE(contents.find("tool: mesh"), std::string::npos);
  EXPECT_NE(contents.find("outcome: ok"), std::string::npos);
  EXPECT_NE(contents.find("duration_ms: 42"), std::string::npos);
}

TEST(AuditLog, DefaultPathHonorsEnvOverride) {
  // Use putenv-style fallback that works on every platform: set via
  // setenv on POSIX, _putenv on Windows. Skip the test if the platform
  // doesn't support it cleanly.
#if defined(_WIN32)
  _putenv("SOUXMAR_AUDIT_LOG=C:/tmp/souxmar-audit-env-test.log");
#else
  setenv("SOUXMAR_AUDIT_LOG", "/tmp/souxmar-audit-env-test.log", /*overwrite=*/1);
#endif
  const auto p = ai::AuditLog::default_path();
  EXPECT_NE(p.string().find("souxmar-audit-env-test.log"), std::string::npos);
#if defined(_WIN32)
  _putenv("SOUXMAR_AUDIT_LOG=");
#else
  unsetenv("SOUXMAR_AUDIT_LOG");
#endif
}

TEST(SessionBudget, RecordCrossesThresholdsOnce) {
  ai::SessionBudget b;
  b.max_total_tokens = 100;
  std::vector<std::pair<int, std::string>> fires;
  b.on_threshold = [&](int pct, std::string_view axis, const ai::SessionBudget&) {
    fires.emplace_back(pct, std::string(axis));
  };

  EXPECT_EQ(b.record(40, 0), 40u);
  EXPECT_TRUE(fires.empty());
  EXPECT_EQ(b.record(20, 0), 60u);  // crosses 50% total
  ASSERT_EQ(fires.size(), 1u);
  EXPECT_EQ(fires[0].first, 50);
  EXPECT_EQ(fires[0].second, "total");
  EXPECT_EQ(b.record(30, 0), 90u);  // crosses 80% total
  ASSERT_EQ(fires.size(), 2u);
  EXPECT_EQ(fires[1].first, 80);
  EXPECT_EQ(b.record(20, 0), 110u);  // crosses 100% total
  ASSERT_EQ(fires.size(), 3u);
  EXPECT_EQ(fires[2].first, 100);
  // Re-recording past 100% does not double-fire.
  EXPECT_EQ(b.record(50, 0), 160u);
  EXPECT_EQ(fires.size(), 3u);
}

TEST(SessionBudget, UnlimitedAxisSuppressesCallback) {
  ai::SessionBudget b;  // max_*_tokens all zero
  int calls = 0;
  b.on_threshold = [&](int, std::string_view, const ai::SessionBudget&) { ++calls; };
  for (int i = 0; i < 10; ++i)
    b.record(1000, 1000);
  EXPECT_EQ(calls, 0);
}

TEST_F(AuditLogTest, DispatchWritesOneEntryPerCall) {
  ai::AuditLog log(path_);
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ctx.audit_log = &log;
  ai::ConfirmationPolicy policy;

  // Mix outcomes so the audit log content reflects the diversity.
  (void)ai::dispatch_tool(r, "no_such_tool", pl::Value::null_value(), ctx, policy);
  (void)ai::dispatch_tool(r, "read_geometry_summary", pl::Value::null_value(), ctx, policy);
  // close + reopen so the buffered writes flush
  log = ai::AuditLog(path_);

  std::ifstream in(path_);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(contents.find("tool: no_such_tool"), std::string::npos);
  EXPECT_NE(contents.find("outcome: not_found"), std::string::npos);
  EXPECT_NE(contents.find("tool: read_geometry_summary"), std::string::npos);
}

// ============================================================================
// Sprint 8 push 4 — CFD-aware BC tools (apply_inlet / apply_wall / apply_outlet)
// ============================================================================

TEST(AiTools_ApplyInlet, ScalarVelocityAppendsInletBc) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("in_face")},
      {"velocity", pl::Value::number(2.5)},
  });
  auto out = ai::dispatch_tool(r, "apply_inlet", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;

  const auto* bcs = session.find("boundary_conditions");
  ASSERT_NE(bcs, nullptr);
  ASSERT_EQ(bcs->kind(), pl::Value::Kind::List);
  ASSERT_EQ(bcs->as_list().size(), 1u);
  const auto& bc = bcs->as_list()[0];
  EXPECT_EQ(bc.find("type")->as_string(), "inlet");
  EXPECT_EQ(bc.find("tag")->as_string(), "in_face");
  EXPECT_EQ(bc.find("velocity")->as_number(), 2.5);
}

TEST(AiTools_ApplyInlet, VectorVelocityAppendsInletBc) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("in_face")},
      {"velocity",
       pl::Value::list({pl::Value::number(1.0), pl::Value::number(0.0), pl::Value::number(0.0)})},
      {"pressure", pl::Value::number(101325.0)},
      {"turbulence_intensity", pl::Value::number(0.05)},
  });
  auto out = ai::dispatch_tool(r, "apply_inlet", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;

  const auto& bc = session.find("boundary_conditions")->as_list()[0];
  EXPECT_EQ(bc.find("velocity")->kind(), pl::Value::Kind::List);
  EXPECT_EQ(bc.find("velocity")->as_list().size(), 3u);
  EXPECT_EQ(bc.find("pressure")->as_number(), 101325.0);
  EXPECT_EQ(bc.find("turbulence_intensity")->as_number(), 0.05);
}

TEST(AiTools_ApplyInlet, RejectsWrongShapeVelocity) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("in_face")},
      {"velocity", pl::Value::list({pl::Value::number(1.0), pl::Value::number(0.0)})},  // 2-vector
  });
  auto out = ai::dispatch_tool(r, "apply_inlet", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ApplyInlet, RejectsMissingTag) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"velocity", pl::Value::number(1.5)},
  });
  auto out = ai::dispatch_tool(r, "apply_inlet", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ApplyWall, DefaultsToNoSlip) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("pipe_wall")},
  });
  auto out = ai::dispatch_tool(r, "apply_wall", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;

  const auto& bc = session.find("boundary_conditions")->as_list()[0];
  EXPECT_EQ(bc.find("type")->as_string(), "wall");
  EXPECT_EQ(bc.find("condition")->as_string(), "no_slip");
}

TEST(AiTools_ApplyWall, AcceptsWallFunctionWithTemperatureAndRoughness) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("hot_wall")},
      {"condition", pl::Value::string("wall_function")},
      {"temperature", pl::Value::number(353.15)},
      {"roughness", pl::Value::number(2e-5)},
  });
  auto out = ai::dispatch_tool(r, "apply_wall", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;

  const auto& bc = session.find("boundary_conditions")->as_list()[0];
  EXPECT_EQ(bc.find("condition")->as_string(), "wall_function");
  EXPECT_EQ(bc.find("temperature")->as_number(), 353.15);
  EXPECT_EQ(bc.find("roughness")->as_number(), 2e-5);
}

TEST(AiTools_ApplyWall, RejectsUnknownCondition) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("wall")},
      {"condition", pl::Value::string("teflon")},
  });
  auto out = ai::dispatch_tool(r, "apply_wall", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ApplyOutlet, PressureOutletRequiresPressure) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_outlet"] = ai::Confirmation::Auto;

  // Missing pressure → rejected.
  auto missing = pl::Value::map({
      {"tag", pl::Value::string("out")},
  });
  auto out = ai::dispatch_tool(r, "apply_outlet", missing, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");

  // With pressure → accepted.
  auto ok = pl::Value::map({
      {"tag", pl::Value::string("out")},
      {"pressure", pl::Value::number(0.0)},
  });
  out = ai::dispatch_tool(r, "apply_outlet", ok, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  const auto& bc = session.find("boundary_conditions")->as_list()[0];
  EXPECT_EQ(bc.find("type")->as_string(), "outlet");
  EXPECT_EQ(bc.find("condition")->as_string(), "pressure_outlet");
  EXPECT_EQ(bc.find("pressure")->as_number(), 0.0);
}

TEST(AiTools_ApplyOutlet, OutflowDoesNotRequirePressure) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_outlet"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({
      {"tag", pl::Value::string("out")},
      {"condition", pl::Value::string("outflow")},
  });
  auto out = ai::dispatch_tool(r, "apply_outlet", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(session.find("boundary_conditions")->as_list()[0].find("condition")->as_string(),
            "outflow");
}

// ============================================================================
// Sprint 8 push 5 — CFD planner + BC validator
// ============================================================================

TEST(AiTools_ProposeCfdSetup, GenericPipeFlowProducesInletWallOutlet) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto input = pl::Value::map({
      {"goal", pl::Value::string("steady pipe flow at Re=2000")},
      {"target_velocity", pl::Value::number(2.0)},
  });
  auto out = ai::dispatch_tool(r, "propose_cfd_setup", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  ASSERT_EQ(out.data.kind(), pl::Value::Kind::Map);

  // Solver: incompressible default.
  EXPECT_EQ(out.data.find("recommended_solver")->as_string(), "solver.cfd.simple");

  // Plan shape: apply_inlet → apply_wall → apply_outlet.
  const auto* plan = out.data.find("plan");
  ASSERT_NE(plan, nullptr);
  ASSERT_EQ(plan->kind(), pl::Value::Kind::List);
  ASSERT_EQ(plan->as_list().size(), 3u);
  EXPECT_EQ(plan->as_list()[0].find("tool")->as_string(), "apply_inlet");
  EXPECT_EQ(plan->as_list()[1].find("tool")->as_string(), "apply_wall");
  EXPECT_EQ(plan->as_list()[2].find("tool")->as_string(), "apply_outlet");

  // Inlet carries the target velocity through.
  EXPECT_EQ(plan->as_list()[0].find("input")->find("velocity")->as_number(), 2.0);
}

TEST(AiTools_ProposeCfdSetup, TagListPicksFirstAndLastByName) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto input = pl::Value::map({
      {"goal", pl::Value::string("flow through a pipe bend")},
      {"tags",
       pl::Value::list({pl::Value::string("walls_pipe"),
                        pl::Value::string("inflow_face"),
                        pl::Value::string("outflow_face"),
                        pl::Value::string("walls_flange")})},
  });
  auto out = ai::dispatch_tool(r, "propose_cfd_setup", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  const auto* plan = out.data.find("plan");

  // First step should be apply_inlet on the in_flow-named tag.
  EXPECT_EQ(plan->as_list()[0].find("tool")->as_string(), "apply_inlet");
  EXPECT_EQ(plan->as_list()[0].find("input")->find("tag")->as_string(), "inflow_face");
  // Last step should be apply_outlet on the out_flow-named tag.
  const auto& last = plan->as_list().back();
  EXPECT_EQ(last.find("tool")->as_string(), "apply_outlet");
  EXPECT_EQ(last.find("input")->find("tag")->as_string(), "outflow_face");
  // The two `walls_*` tags should appear as apply_wall steps.
  std::size_t wall_count = 0;
  for (const auto& step : plan->as_list()) {
    if (step.find("tool")->as_string() == "apply_wall")
      ++wall_count;
  }
  EXPECT_EQ(wall_count, 2u);
}

TEST(AiTools_ProposeCfdSetup, CompressibleRegimeRoutesToPimple) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto input = pl::Value::map({
      {"goal", pl::Value::string("nozzle expansion")},
      {"regime", pl::Value::string("compressible")},
  });
  auto out = ai::dispatch_tool(r, "propose_cfd_setup", input, ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("recommended_solver")->as_string(), "solver.cfd.openfoam.pimple");
}

TEST(AiTools_ProposeCfdSetup, RejectsUnknownRegime) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto input = pl::Value::map({
      {"goal", pl::Value::string("anything")},
      {"regime", pl::Value::string("relativistic")},
  });
  auto out = ai::dispatch_tool(r, "propose_cfd_setup", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  EXPECT_EQ(out.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ValidateBcs, EmptySessionIsWarningOk) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;  // no session_state
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "validate_bcs", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_TRUE(out.data.find("ok")->as_bool());
  const auto* issues = out.data.find("issues");
  ASSERT_NE(issues, nullptr);
  ASSERT_EQ(issues->kind(), pl::Value::Kind::List);
  ASSERT_FALSE(issues->as_list().empty());
  EXPECT_EQ(issues->as_list()[0].find("severity")->as_string(), "warning");
}

TEST(AiTools_ValidateBcs, FullSetupIsOk) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;
  policy.overrides["apply_outlet"] = ai::Confirmation::Auto;

  (void)ai::dispatch_tool(
      r,
      "apply_inlet",
      pl::Value::map({{"tag", pl::Value::string("in")}, {"velocity", pl::Value::number(1.0)}}),
      ctx,
      policy);
  (void)ai::dispatch_tool(
      r, "apply_wall", pl::Value::map({{"tag", pl::Value::string("walls")}}), ctx, policy);
  (void)ai::dispatch_tool(
      r,
      "apply_outlet",
      pl::Value::map({{"tag", pl::Value::string("out")}, {"pressure", pl::Value::number(0.0)}}),
      ctx,
      policy);

  auto out = ai::dispatch_tool(r, "validate_bcs", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_TRUE(out.data.find("ok")->as_bool());
  EXPECT_EQ(out.data.find("counts")->find("inlet")->as_number(), 1.0);
  EXPECT_EQ(out.data.find("counts")->find("wall")->as_number(), 1.0);
  EXPECT_EQ(out.data.find("counts")->find("outlet")->as_number(), 1.0);
}

TEST(AiTools_ValidateBcs, NoInletAndNoOutletEmitWarnings) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;

  // Stage only walls.
  (void)ai::dispatch_tool(
      r, "apply_wall", pl::Value::map({{"tag", pl::Value::string("walls")}}), ctx, policy);

  auto out = ai::dispatch_tool(r, "validate_bcs", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value());
  EXPECT_TRUE(out.data.find("ok")->as_bool());  // warnings only.
  bool saw_no_inlet = false, saw_no_outlet = false;
  for (const auto& iss : out.data.find("issues")->as_list()) {
    const auto code = std::string(iss.find("code")->as_string());
    if (code == "NO_INLET")
      saw_no_inlet = true;
    if (code == "NO_OUTLET")
      saw_no_outlet = true;
  }
  EXPECT_TRUE(saw_no_inlet);
  EXPECT_TRUE(saw_no_outlet);
}

TEST(AiTools_ValidateBcs, DuplicateTagWithConflictingTypesIsError) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;
  policy.overrides["apply_outlet"] = ai::Confirmation::Auto;

  // Same tag once as inlet, then as outlet — apply_* tools do not
  // dedupe, so the staged bag has the duplicate.
  (void)ai::dispatch_tool(
      r,
      "apply_inlet",
      pl::Value::map({{"tag", pl::Value::string("port")}, {"velocity", pl::Value::number(1.0)}}),
      ctx,
      policy);
  (void)ai::dispatch_tool(
      r,
      "apply_outlet",
      pl::Value::map({{"tag", pl::Value::string("port")}, {"pressure", pl::Value::number(0.0)}}),
      ctx,
      policy);

  auto out = ai::dispatch_tool(r, "validate_bcs", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value());
  EXPECT_FALSE(out.data.find("ok")->as_bool());
  bool saw_dup = false;
  for (const auto& iss : out.data.find("issues")->as_list()) {
    if (iss.find("code")->as_string() == "DUPLICATE_TAG")
      saw_dup = true;
  }
  EXPECT_TRUE(saw_dup);
}

// Three CFD-aware tools chained together build a complete pipe-flow BC
// set on a single session — the canonical pipe-bend pattern push 6 will
// run end-to-end via the cfd-stub plugin.
TEST(AiTools_CfdBcChain, InletWallOutletCoexistOnOneSession) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_inlet"] = ai::Confirmation::Auto;
  policy.overrides["apply_wall"] = ai::Confirmation::Auto;
  policy.overrides["apply_outlet"] = ai::Confirmation::Auto;

  ASSERT_FALSE(ai::dispatch_tool(r,
                                 "apply_inlet",
                                 pl::Value::map({{"tag", pl::Value::string("in")},
                                                 {"velocity", pl::Value::number(1.0)}}),
                                 ctx,
                                 policy)
                   .error.has_value());
  ASSERT_FALSE(
      ai::dispatch_tool(
          r, "apply_wall", pl::Value::map({{"tag", pl::Value::string("walls")}}), ctx, policy)
          .error.has_value());
  ASSERT_FALSE(ai::dispatch_tool(r,
                                 "apply_outlet",
                                 pl::Value::map({{"tag", pl::Value::string("out")},
                                                 {"pressure", pl::Value::number(0.0)}}),
                                 ctx,
                                 policy)
                   .error.has_value());

  const auto& bcs = session.find("boundary_conditions")->as_list();
  ASSERT_EQ(bcs.size(), 3u);
  EXPECT_EQ(bcs[0].find("type")->as_string(), "inlet");
  EXPECT_EQ(bcs[1].find("type")->as_string(), "wall");
  EXPECT_EQ(bcs[2].find("type")->as_string(), "outlet");
}

// ============================================================================
// Manufacturing block — AM + marine tools (19-24)
// ============================================================================

// A no-op solver vtable. The FakeDispatcher above never calls through it;
// the AM tools only ask the Registry whether the capability *exists*.
souxmar_solver_vtable_t& fake_solver_vtable() {
  static souxmar_solver_vtable_t vt{
      SOUXMAR_ABI_VERSION_MAJOR,
      [](const souxmar_mesh_t*,
         const souxmar_value_t*,
         const souxmar_solver_options_t*,
         souxmar_field_t**,
         void*) -> souxmar_status_t { return souxmar_status_ok(); },
      nullptr};
  return vt;
}

// Locate a stage by plugin id inside a propose_am_setup proposal.
const pl::Value* stage_with_plugin(const pl::Value& proposal, std::string_view plugin) {
  const auto* pipeline = proposal.find("pipeline");
  if (!pipeline)
    return nullptr;
  const auto* stages = pipeline->find("stages");
  if (!stages || stages->kind() != pl::Value::Kind::List)
    return nullptr;
  for (const auto& stage : stages->as_list()) {
    const auto* p = stage.find("plugin");
    if (p && p->kind() == pl::Value::Kind::String && p->as_string() == plugin)
      return &stage;
  }
  return nullptr;
}

TEST(AiTools_ProposeAmSetup, LpbfProposalIsRunnableAndStagesTheSetup) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;  // Auto tier — no prompter needed

  auto out = ai::dispatch_tool(
      r,
      "propose_am_setup",
      pl::Value::map({{"process", pl::Value::string("lpbf")},
                      {"material", pl::Value::string("316L")},
                      {"part_name", pl::Value::string("bracket")}}),
      ctx,
      policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("process")->as_string(), "lpbf");
  EXPECT_EQ(out.data.find("material")->find("id")->as_string(), "316L");
  // 316L density feeds through from the built-in material table.
  EXPECT_DOUBLE_EQ(out.data.find("material")->find("density")->as_number(), 7990.0);
  // Metal powder-bed chain: mesh + thermal + melt pool + distortion +
  // residual stress + overhang + printability + buildtime + slices +
  // report = 10 stages.
  EXPECT_GE(out.data.find("stage_count")->as_number(), 8.0);

  // The mesh source is the layered AM mesher, and it carries the
  // simulation layer height as `target_size` (meshers get no value bag).
  const auto* mesher = stage_with_plugin(out.data, "mesher.am.layered");
  ASSERT_NE(mesher, nullptr);
  EXPECT_EQ(mesher->find("id")->as_string(), "build_mesh");
  EXPECT_DOUBLE_EQ(mesher->find("input")->find("target_size")->as_number(), 0.001);

  // Every postproc stage MUST carry `field: {from: <stage>}` or the
  // registry dispatcher rejects it.
  const auto* melt_pool = stage_with_plugin(out.data, "postproc.am.melt_pool");
  ASSERT_NE(melt_pool, nullptr);
  const auto* field_ref = melt_pool->find("input")->find("field");
  ASSERT_NE(field_ref, nullptr);
  ASSERT_EQ(field_ref->kind(), pl::Value::Kind::Stage);
  EXPECT_EQ(field_ref->as_stage().stage_id, "thermal");

  // No marine stages unless asked for.
  EXPECT_FALSE(out.data.find("marine")->as_bool());
  EXPECT_EQ(stage_with_plugin(out.data, "solver.marine.hydrostatic"), nullptr);

  // The resolved setup is staged for estimate_build_cost to consume.
  const auto* mfg = session.find("manufacturing");
  ASSERT_NE(mfg, nullptr);
  EXPECT_EQ(mfg->find("process")->as_string(), "lpbf");
  EXPECT_EQ(mfg->find("material")->find("id")->as_string(), "316L");

  // A polymer feedstock on a metal process is a hard input error, not a
  // silently-wrong pipeline.
  auto bad = ai::dispatch_tool(r,
                               "propose_am_setup",
                               pl::Value::map({{"process", pl::Value::string("lpbf")},
                                               {"material", pl::Value::string("PEKK")}}),
                               ctx,
                               policy);
  ASSERT_TRUE(bad.error.has_value());
  EXPECT_EQ(bad.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_CheckPrintability, SummarisesBlockersAndReportsMissingCapability) {
  auto r = ai::default_v1_tools();
  ai::ConfirmationPolicy policy;

  // No registry / dispatcher at all → INTERNAL.
  {
    ai::ToolContext ctx;
    auto out = ai::dispatch_tool(r, "check_printability", pl::Value::null_value(), ctx, policy);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code, "INTERNAL");
  }
  // Wired host, capability absent → PLUGIN_NOT_FOUND with a suggestion.
  {
    souxmar::plugin::Registry empty;
    FakeDispatcher dispatcher;
    ai::ToolContext ctx;
    ctx.registry = &empty;
    ctx.dispatcher = &dispatcher;
    auto out = ai::dispatch_tool(r, "check_printability", pl::Value::null_value(), ctx, policy);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code, "PLUGIN_NOT_FOUND");
    EXPECT_FALSE(out.error->suggestion.empty());
    EXPECT_TRUE(dispatcher.calls.empty()) << "must not reach the dispatcher";
  }
  // Capability present, no mesh → PRECONDITION_FAILED.
  souxmar::plugin::Registry registry;
  ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.add_solver(
      std::string{"solver.am.printability"}, "fake-am", &fake_solver_vtable(), nullptr)));
  FakeDispatcher dispatcher;
  {
    ai::ToolContext ctx;
    ctx.registry = &registry;
    ctx.dispatcher = &dispatcher;
    auto out = ai::dispatch_tool(r, "check_printability", pl::Value::null_value(), ctx, policy);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code, "PRECONDITION_FAILED");
  }

  // Happy path: three cells, scores 0.9 / 0.4 / 0.2, limiting factors
  // none / overhang / overhang.
  auto field = std::make_shared<souxmar::core::Field>("printability",
                                                      souxmar::core::FieldLocation::Cell,
                                                      souxmar::core::FieldKind::Vector,
                                                      /*count=*/3);
  auto data = field->data();
  data[0] = 0.9;
  data[1] = 0.0;
  data[2] = 0.002;
  data[3] = 0.4;
  data[4] = 1.0;
  data[5] = 0.0004;
  data[6] = 0.2;
  data[7] = 1.0;
  data[8] = 0.0003;
  dispatcher.preset_field = field;

  auto mesh = std::make_shared<souxmar::core::Mesh>();
  (void)mesh->add_node({0.0, 0.0, 0.0});
  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ctx.mesh_handle = mesh;

  auto out = ai::dispatch_tool(
      r, "check_printability", pl::Value::map({{"score_threshold", pl::Value::number(0.5)}}), ctx,
      policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("capability_id")->as_string(), "solver.am.printability");
  EXPECT_EQ(out.data.find("num_cells")->as_number(), 3.0);
  EXPECT_DOUBLE_EQ(out.data.find("score")->find("min")->as_number(), 0.2);
  EXPECT_DOUBLE_EQ(out.data.find("score")->find("max")->as_number(), 0.9);
  EXPECT_EQ(out.data.find("cells_below_threshold")->as_number(), 2.0);
  EXPECT_EQ(out.data.find("limiting_factors")->find("overhang")->as_number(), 2.0);
  EXPECT_EQ(out.data.find("dominant_limiting_factor")->as_string(), "overhang");
  EXPECT_FALSE(out.data.find("printable")->as_bool());
  EXPECT_EQ(ctx.field_handle, field) << "field handle should be stashed for reuse";
}

TEST(AiTools_SetBuildOrientation, ScoresAxesWithMeshAndDegradesToAdvisoryWithout) {
  auto r = ai::default_v1_tools();
  ai::ConfirmationPolicy policy;
  policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };  // ConfirmOnce

  // ---- no mesh: advisory ranking, still stages a winner ----
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  auto out = ai::dispatch_tool(r, "set_build_orientation", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_TRUE(out.data.find("advisory_only")->as_bool());
  EXPECT_FALSE(out.data.find("mesh_available")->as_bool());
  EXPECT_EQ(out.data.find("candidate_count")->as_number(), 6.0);
  EXPECT_EQ(out.data.find("winner")->find("label")->as_string(), "+Z");
  EXPECT_NE(out.summary.find("advisory only"), std::string::npos)
      << "the summary must admit that nothing was measured";

  const auto* bcs = session.find("boundary_conditions");
  ASSERT_NE(bcs, nullptr);
  ASSERT_EQ(bcs->as_list().size(), 1u);
  EXPECT_EQ(bcs->as_list()[0].find("type")->as_string(), "build_orientation");
  ASSERT_NE(session.find("manufacturing"), nullptr);
  EXPECT_EQ(session.find("manufacturing")->find("build_direction")->as_list()[2].as_number(), 1.0);

  // ---- with a mesh: a real scored ranking over the outer shell ----
  auto mesh = std::make_shared<souxmar::core::Mesh>();
  const auto n0 = mesh->add_node({0.0, 0.0, 0.0});
  const auto n1 = mesh->add_node({1.0, 0.0, 0.0});
  const auto n2 = mesh->add_node({0.0, 1.0, 0.0});
  const auto n3 = mesh->add_node({0.0, 0.0, 0.25});
  std::array<souxmar::core::NodeIndex, 4> nodes{n0, n1, n2, n3};
  (void)mesh->add_cell(souxmar::core::ElementType::Tet4, nodes);

  pl::Value session2 = pl::Value::map({});
  ai::ToolContext ctx2;
  ctx2.session_state = &session2;
  ctx2.mesh_handle = mesh;
  auto scored = ai::dispatch_tool(
      r,
      "set_build_orientation",
      pl::Value::map({{"candidates",
                       pl::Value::list({pl::Value::list({pl::Value::number(0.0),
                                                         pl::Value::number(0.0),
                                                         pl::Value::number(2.0)})})}}),
      ctx2,
      policy);
  ASSERT_FALSE(scored.error.has_value()) << scored.summary;
  EXPECT_FALSE(scored.data.find("advisory_only")->as_bool());
  EXPECT_TRUE(scored.data.find("surface_available")->as_bool());
  // The supplied [0,0,2] normalises to +Z, which the axis set would also
  // contribute — the duplicate is dropped, so 6 candidates remain.
  EXPECT_EQ(scored.data.find("candidate_count")->as_number(), 6.0);
  // One Tet4 has four boundary triangles; their areas sum to
  // 0.5 + 0.125 + 0.125 + 0.53033 = 1.28033 m^2.
  EXPECT_EQ(scored.data.find("triangles_evaluated")->as_number(), 4.0);
  EXPECT_NEAR(scored.data.find("total_surface_area_m2")->as_number(), 1.28033, 1e-4);

  const auto* ranking = scored.data.find("ranking");
  ASSERT_NE(ranking, nullptr);
  ASSERT_EQ(ranking->as_list().size(), 6u);
  EXPECT_GE(ranking->as_list()[0].find("score")->as_number(),
            ranking->as_list()[5].find("score")->as_number())
      << "ranking must be sorted best-first";
  // The +Z candidate is 0.25 m tall and casts a 0.5 m^2 shadow (the
  // z = 0 face) — the geometry maths, independent of the weighting.
  const pl::Value* z_up = nullptr;
  for (const auto& entry : ranking->as_list()) {
    if (entry.find("label")->as_string() == "candidate_0")
      z_up = &entry;
  }
  ASSERT_NE(z_up, nullptr);
  EXPECT_NEAR(z_up->find("height_m")->as_number(), 0.25, 1e-12);
  EXPECT_NEAR(z_up->find("projected_footprint_m2")->as_number(), 0.5, 1e-5);
  EXPECT_NEAR(z_up->find("support_area_m2")->as_number(), 0.5, 1e-5)
      << "building +Z puts the whole z=0 face on supports";
  // The winner is a support-free orientation (-X and -Y both leave only
  // the 76-degree slanted face pointing down, which is self-supporting
  // at the 45-degree threshold); the index tiebreak picks -X.
  EXPECT_NEAR(scored.data.find("winner")->find("support_area_m2")->as_number(), 0.0, 1e-9);
  EXPECT_EQ(scored.data.find("winner")->find("label")->as_string(), "-X");
}

TEST(AiTools_EstimateBuildCost, ComputesMassTimeAndCostFromTheBoundingBox) {
  auto r = ai::default_v1_tools();
  ai::ConfirmationPolicy policy;

  // No mesh → PRECONDITION_FAILED (there is no bounding box to cost).
  {
    ai::ToolContext ctx;
    auto out = ai::dispatch_tool(r, "estimate_build_cost", pl::Value::null_value(), ctx, policy);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code, "PRECONDITION_FAILED");
  }

  // 100 x 50 x 20 mm bounding box → 1e-4 m^3; the default 0.35 fill and
  // the 316L default density give 7990 * 3.5e-5 = 0.27965 kg.
  auto mesh = std::make_shared<souxmar::core::Mesh>();
  (void)mesh->add_node({0.0, 0.0, 0.0});
  (void)mesh->add_node({0.1, 0.05, 0.02});
  ai::ToolContext ctx;
  ctx.mesh_handle = mesh;

  auto out = ai::dispatch_tool(r, "estimate_build_cost", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("process")->as_string(), "lpbf");
  EXPECT_EQ(out.data.find("setup_source")->as_string(), "defaults");
  EXPECT_DOUBLE_EQ(out.data.find("bounding_box_volume_m3")->as_number(), 1.0e-4);
  EXPECT_NEAR(out.data.find("mass_kg")->as_number(), 0.27965, 1e-9);
  EXPECT_DOUBLE_EQ(out.data.find("build_height_m")->as_number(), 0.02);
  // ceil(0.02 / 3e-5) = 667 machine layers.
  EXPECT_DOUBLE_EQ(out.data.find("num_machine_layers")->as_number(), 667.0);
  // material_cost = 0.27965 kg * 90 / kg.
  EXPECT_NEAR(out.data.find("material_cost")->as_number(), 25.1685, 1e-6);
  EXPECT_GT(out.data.find("machine_cost")->as_number(), 0.0);
  EXPECT_NEAR(out.data.find("total_cost")->as_number(),
              out.data.find("machine_cost")->as_number()
                  + out.data.find("material_cost")->as_number(),
              1e-9);
  EXPECT_GT(out.data.find("energy_kwh")->as_number(), 0.0);

  // fill_fraction outside (0, 1] is rejected rather than clamped.
  auto bad = ai::dispatch_tool(r,
                               "estimate_build_cost",
                               pl::Value::map({{"fill_fraction", pl::Value::number(1.5)}}),
                               ctx,
                               policy);
  ASSERT_TRUE(bad.error.has_value());
  EXPECT_EQ(bad.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_ApplyHydrostaticLoad, StagesHydrostaticBcWithComputedPressure) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  policy.overrides["apply_hydrostatic_load"] = ai::Confirmation::Auto;

  auto out = ai::dispatch_tool(r,
                               "apply_hydrostatic_load",
                               pl::Value::map({{"tag", pl::Value::string("hull_outer")},
                                               {"depth", pl::Value::number(300.0)}}),
                               ctx,
                               policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  // rho(35 PSU, 10 C) = 1027.0 exactly at the fit's reference point.
  EXPECT_NEAR(out.data.find("density")->as_number(), 1027.0, 1e-9);
  EXPECT_EQ(out.data.find("density_source")->as_string(), "derived_from_salinity_temperature");
  // p = 1027.0 * 9.80665 * 300 = 3 021 428.865 Pa.
  EXPECT_NEAR(out.data.find("pressure")->as_number(), 3021428.865, 1e-3);
  EXPECT_NEAR(out.data.find("pressure_mpa")->as_number(), 3.021428865, 1e-9);
  EXPECT_EQ(out.data.find("count")->as_number(), 1.0);

  const auto* bcs = session.find("boundary_conditions");
  ASSERT_NE(bcs, nullptr);
  ASSERT_EQ(bcs->as_list().size(), 1u);
  const auto& bc = bcs->as_list()[0];
  EXPECT_EQ(bc.find("type")->as_string(), "hydrostatic");
  EXPECT_EQ(bc.find("tag")->as_string(), "hull_outer");
  EXPECT_DOUBLE_EQ(bc.find("depth")->as_number(), 300.0);
  EXPECT_NEAR(bc.find("density")->as_number(), 1027.0, 1e-9);
  EXPECT_NEAR(bc.find("pressure")->as_number(), 3021428.865, 1e-3);

  // Factored set: operating / test / collapse.
  auto factored = ai::dispatch_tool(
      r,
      "apply_hydrostatic_load",
      pl::Value::map({{"depth", pl::Value::number(300.0)},
                      {"depth_factors",
                       pl::Value::list({pl::Value::number(1.0), pl::Value::number(1.5),
                                        pl::Value::number(2.25)})}}),
      ctx,
      policy);
  ASSERT_FALSE(factored.error.has_value()) << factored.summary;
  ASSERT_EQ(factored.data.find("load_cases")->as_list().size(), 3u);
  EXPECT_NEAR(factored.data.find("max_pressure")->as_number(), 3021428.865 * 2.25, 1e-2);
  EXPECT_EQ(factored.data.find("count")->as_number(), 2.0) << "BCs accumulate on the session";

  // A negative depth is an input error, not a suction load.
  auto bad = ai::dispatch_tool(r,
                               "apply_hydrostatic_load",
                               pl::Value::map({{"depth", pl::Value::number(-5.0)}}),
                               ctx,
                               policy);
  ASSERT_TRUE(bad.error.has_value());
  EXPECT_EQ(bad.error->code, "INVALID_ARGUMENT");
}

TEST(AiTools_CheckMarineIntegrity, ReportsCollapseMarginAndMissingCapability) {
  auto r = ai::default_v1_tools();
  ai::ConfirmationPolicy policy;

  souxmar::plugin::Registry registry;
  ASSERT_TRUE(std::holds_alternative<std::monostate>(registry.add_solver(
      std::string{"solver.marine.hull_collapse"}, "fake-marine", &fake_solver_vtable(), nullptr)));
  FakeDispatcher dispatcher;

  // Both checks requested but only the collapse capability exists →
  // PLUGIN_NOT_FOUND naming the missing id.
  {
    ai::ToolContext ctx;
    ctx.registry = &registry;
    ctx.dispatcher = &dispatcher;
    auto out = ai::dispatch_tool(r, "check_marine_integrity", pl::Value::null_value(), ctx, policy);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code, "PLUGIN_NOT_FOUND");
    EXPECT_NE(out.error->message.find("solver.marine.corrosion"), std::string::npos);
    EXPECT_TRUE(dispatcher.calls.empty()) << "must not reach the dispatcher";
  }

  // Collapse only, with a field carrying margin 0.8 and governing mode 1
  // (membrane yield) on both cells.
  auto field = std::make_shared<souxmar::core::Field>("collapse_margin",
                                                      souxmar::core::FieldLocation::Cell,
                                                      souxmar::core::FieldKind::Vector,
                                                      /*count=*/2);
  auto data = field->data();
  for (std::size_t c = 0; c < 2; ++c) {
    data[c * 3 + 0] = 4.2e6;  // Pa
    data[c * 3 + 1] = 0.8;    // margin — below 1.0, so not adequate
    data[c * 3 + 2] = 1.0;    // membrane yield
  }
  dispatcher.preset_field = field;

  auto mesh = std::make_shared<souxmar::core::Mesh>();
  (void)mesh->add_node({0.0, 0.0, 0.0});
  ai::ToolContext ctx;
  ctx.registry = &registry;
  ctx.dispatcher = &dispatcher;
  ctx.mesh_handle = mesh;

  auto out = ai::dispatch_tool(
      r,
      "check_marine_integrity",
      pl::Value::map({{"checks", pl::Value::list({pl::Value::string("collapse")})},
                      {"design_depth", pl::Value::number(300.0)}}),
      ctx,
      policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(dispatcher.calls, std::vector<std::string>{"solver.marine.hull_collapse"});
  ASSERT_NE(out.data.find("collapse"), nullptr);
  EXPECT_EQ(out.data.find("corrosion"), nullptr) << "corrosion was not requested";
  EXPECT_NEAR(out.data.find("collapse")->find("collapse_pressure_pa")->as_number(), 4.2e6, 1.0);
  EXPECT_NEAR(out.data.find("collapse")->find("margin")->as_number(), 0.8, 1e-12);
  EXPECT_EQ(out.data.find("collapse")->find("governing_mode")->as_string(), "membrane_yield");
  EXPECT_FALSE(out.data.find("collapse")->find("adequate")->as_bool());
  EXPECT_FALSE(out.data.find("ok")->as_bool());
  EXPECT_FALSE(out.data.find("advisories")->as_list().empty())
      << "a sub-unity margin must produce an actionable advisory";
}

}  // namespace
