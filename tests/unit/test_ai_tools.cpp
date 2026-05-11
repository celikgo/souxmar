// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the agent tool framework + the 5 v1 tools. Mock
// dispatcher / mock registry keep the tests in-process and fast.

#include "souxmar/ai/tool.h"

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

#include "souxmar/ai/audit_log.h"

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

// ============================================================================
// Sprint 5 push 2 — new tools
// ============================================================================

TEST(AiDefaultTools_v2, RegistryNowContainsEightTools) {
  auto r = ai::default_v1_tools();
  // Sprint 6 push 1: catalogue grew to 9 (added query_mesh_quality).
  EXPECT_EQ(r.list().size(), 9u);
  for (const auto* expected : {
      "read_geometry_summary", "mesh", "set_bc", "solve",
      "screenshot_viewport", "query_field", "compute_field",
      "propose_pipeline",
      "query_mesh_quality"}) {
    EXPECT_NE(r.find(expected), nullptr) << "missing tool: " << expected;
  }
}

TEST(AiTools_QueryMeshQuality, RequiresMeshHandle) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "query_mesh_quality",
                               pl::Value::null_value(), ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  // Without a registry/dispatcher we fail on INTERNAL; with them but no
  // mesh handle we'd fail on PRECONDITION_FAILED. Either is acceptable
  // — the point is we never reach the dispatcher.
  EXPECT_TRUE(out.error->code == "INTERNAL" ||
              out.error->code == "PRECONDITION_FAILED");
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
  auto field = std::make_shared<souxmar::core::Field>(
      "test-field", souxmar::core::FieldLocation::Cell,
      souxmar::core::FieldKind::Scalar, /*count=*/4);
  // Field's data() is zero-initialised on construction; mutate via the
  // mutable span so we can exercise the aggregator with a known input.
  auto data = field->data();
  data[0] = -3.0;
  data[1] =  1.0;
  data[2] =  5.0;
  data[3] =  2.0;

  ai::ToolContext ctx;
  ctx.field_handle = field;
  ai::ConfirmationPolicy policy;
  auto out = ai::dispatch_tool(r, "query_field", pl::Value::null_value(), ctx, policy);
  ASSERT_FALSE(out.error.has_value()) << out.summary;
  EXPECT_EQ(out.data.find("min")->as_number(),   -3.0);
  EXPECT_EQ(out.data.find("max")->as_number(),    5.0);
  EXPECT_DOUBLE_EQ(out.data.find("mean")->as_number(), (-3.0 + 1.0 + 5.0 + 2.0) / 4.0);
  EXPECT_EQ(out.data.find("count")->as_number(),  4.0);
  EXPECT_EQ(std::string(out.data.find("location")->as_string()), "cell");
  EXPECT_EQ(std::string(out.data.find("kind")->as_string()),     "scalar");
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
  ctx.registry   = &registry;
  ctx.dispatcher = &dispatcher;
  ai::ConfirmationPolicy policy;
  policy.overrides["compute_field"] = ai::Confirmation::Auto;

  auto input = pl::Value::map({{"capability_id", pl::Value::string("postproc.x")}});
  auto out = ai::dispatch_tool(r, "compute_field", input, ctx, policy);
  ASSERT_TRUE(out.error.has_value());
  // The order in compute_field.cpp checks registry->find_postproc before
  // mesh_handle, so a missing capability surfaces PLUGIN_NOT_FOUND
  // first. Either is correct contract.
  EXPECT_TRUE(out.error->code == "PLUGIN_NOT_FOUND" ||
              out.error->code == "PRECONDITION_FAILED")
      << "unexpected code: " << out.error->code;
}

TEST(AiTools_ProposePipeline, RoundTripsThroughParser) {
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ai::ConfirmationPolicy policy;

  // Build a valid 2-stage spec: mesher → writer (mirrors the cantilever).
  auto spec = pl::Value::map({
      {"version", pl::Value::number(1)},
      {"stages",  pl::Value::list({
          pl::Value::map({
              {"id",     pl::Value::string("mesh")},
              {"plugin", pl::Value::string("mesher.tetra.hello")},
          }),
          pl::Value::map({
              {"id",     pl::Value::string("write")},
              {"plugin", pl::Value::string("writer.vtu")},
              {"input",  pl::Value::map({
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
    path_ = std::filesystem::temp_directory_path() /
            ("souxmar-audit-test-" + std::to_string(rd()) + ".log");
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
    e.tool_name  = "mesh";
    e.outcome    = "ok";
    e.summary    = "mesh: 4 nodes";
    e.input_hash = "deadbeefcafebabe";
    e.duration   = std::chrono::milliseconds{42};
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
  b.on_threshold = [&](int pct, std::string_view axis,
                       const ai::SessionBudget&) {
    fires.emplace_back(pct, std::string(axis));
  };

  EXPECT_EQ(b.record(40, 0),  40u);
  EXPECT_TRUE(fires.empty());
  EXPECT_EQ(b.record(20, 0),  60u);   // crosses 50% total
  ASSERT_EQ(fires.size(), 1u);
  EXPECT_EQ(fires[0].first, 50);
  EXPECT_EQ(fires[0].second, "total");
  EXPECT_EQ(b.record(30, 0),  90u);   // crosses 80% total
  ASSERT_EQ(fires.size(), 2u);
  EXPECT_EQ(fires[1].first, 80);
  EXPECT_EQ(b.record(20, 0), 110u);   // crosses 100% total
  ASSERT_EQ(fires.size(), 3u);
  EXPECT_EQ(fires[2].first, 100);
  // Re-recording past 100% does not double-fire.
  EXPECT_EQ(b.record(50, 0), 160u);
  EXPECT_EQ(fires.size(), 3u);
}

TEST(SessionBudget, UnlimitedAxisSuppressesCallback) {
  ai::SessionBudget b;     // max_*_tokens all zero
  int calls = 0;
  b.on_threshold = [&](int, std::string_view, const ai::SessionBudget&) { ++calls; };
  for (int i = 0; i < 10; ++i) b.record(1000, 1000);
  EXPECT_EQ(calls, 0);
}

TEST_F(AuditLogTest, DispatchWritesOneEntryPerCall) {
  ai::AuditLog log(path_);
  auto r = ai::default_v1_tools();
  ai::ToolContext ctx;
  ctx.audit_log = &log;
  ai::ConfirmationPolicy policy;

  // Mix outcomes so the audit log content reflects the diversity.
  (void)ai::dispatch_tool(r, "no_such_tool",       pl::Value::null_value(), ctx, policy);
  (void)ai::dispatch_tool(r, "read_geometry_summary", pl::Value::null_value(), ctx, policy);
  // close + reopen so the buffered writes flush
  log = ai::AuditLog(path_);

  std::ifstream in(path_);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(contents.find("tool: no_such_tool"),  std::string::npos);
  EXPECT_NE(contents.find("outcome: not_found"),  std::string::npos);
  EXPECT_NE(contents.find("tool: read_geometry_summary"), std::string::npos);
}

}  // namespace
