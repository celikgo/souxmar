// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the agent tool framework + the 5 v1 tools. Mock
// dispatcher / mock registry keep the tests in-process and fast.

#include "souxmar/ai/tool.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"

namespace ai = souxmar::ai;
namespace pl = souxmar::pipeline;

namespace {

// A dispatcher that returns a hand-built StageOutput on demand. The
// `mesh` and `solve` tools route through this rather than real plugins.
class FakeDispatcher : public pl::IDispatcher {
 public:
  std::shared_ptr<souxmar::core::Mesh>  preset_mesh;
  std::shared_ptr<souxmar::core::Field> preset_field;
  std::string                           preset_error;
  std::vector<std::string>              calls;

  pl::DispatchResult dispatch(const pl::DispatchContext& ctx) override {
    calls.emplace_back(ctx.capability_id);
    if (!preset_error.empty()) return pl::DispatchError{preset_error};

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
  t.handler = [](const pl::Value&, ai::ToolContext&) {
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

  ASSERT_FALSE(ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
  ASSERT_FALSE(ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
  ASSERT_FALSE(ai::dispatch_tool(r, "once", pl::Value::null_value(), ctx, policy).error.has_value());
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

TEST(AiDefaultTools, RegistryContainsAllFiveV1Tools) {
  auto r = ai::default_v1_tools();
  const auto list = r.list();
  EXPECT_EQ(list.size(), 5u);
  EXPECT_NE(r.find("read_geometry_summary"), nullptr);
  EXPECT_NE(r.find("mesh"),                  nullptr);
  EXPECT_NE(r.find("set_bc"),                nullptr);
  EXPECT_NE(r.find("solve"),                 nullptr);
  EXPECT_NE(r.find("screenshot_viewport"),   nullptr);
}

TEST(AiTools_ReadGeometrySummary, ReadsFromSessionStateWhenInputAbsent) {
  auto r = ai::default_v1_tools();
  pl::Value session = pl::Value::map({{
      "geometry", pl::Value::map({
          {"num_vertices", pl::Value::number(8)},
          {"num_edges",    pl::Value::number(12)},
          {"num_faces",    pl::Value::number(6)},
          {"num_solids",   pl::Value::number(1)},
      })}});
  ai::ToolContext ctx;
  ctx.session_state = &session;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "read_geometry_summary", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  ASSERT_EQ(out.data.kind(), pl::Value::Kind::Map);
  EXPECT_EQ(out.data.find("num_vertices")->as_number(), 8.0);
  EXPECT_EQ(out.data.find("num_solids")->as_number(),   1.0);
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
      {"tag",   pl::Value::string("inlet")},
      {"type",  pl::Value::string("dirichlet")},
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
      {"tag",   pl::Value::string("inlet")},
      {"type",  pl::Value::string("greasy")},
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
  static souxmar_mesher_vtable_t fake_vt{SOUXMAR_ABI_VERSION_MAJOR,
      [](const souxmar_geometry_t*, const souxmar_mesher_options_t*,
         souxmar_mesh_t**, void*) -> souxmar_status_t {
        return souxmar_status_ok();
      },
      nullptr};
  auto reg_res = registry.add_mesher(std::string{"mesher.fake"}, "fake-plugin",
                                     &fake_vt, nullptr);
  ASSERT_TRUE(std::holds_alternative<std::monostate>(reg_res));

  ai::ToolContext ctx;
  ctx.registry   = &registry;
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
  ctx.registry   = &registry;
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
  ctx.registry   = &registry;
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

}  // namespace
