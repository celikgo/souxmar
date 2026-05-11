// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/runner.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "souxmar/pipeline/parser.h"

using namespace souxmar::pipeline;

namespace {

// Mock dispatcher records every call and returns a payload encoding the
// stage's capability id, so tests can verify both call ordering and that
// upstream outputs were correctly threaded.
class MockDispatcher : public IDispatcher {
 public:
  struct Call {
    std::string                                  capability_id;
    std::vector<std::string>                     upstream_ids;
  };

  std::vector<Call> calls;

  // If a capability id appears in `fail_for`, that dispatch returns an error.
  std::vector<std::string> fail_for;

  // Plugin version returned per capability id.
  std::map<std::string, std::string> versions;

  std::string plugin_version(std::string_view cap) override {
    auto it = versions.find(std::string(cap));
    return it != versions.end() ? it->second : "";
  }

  DispatchResult dispatch(const DispatchContext& ctx) override {
    Call c;
    c.capability_id = std::string(ctx.capability_id);
    for (const auto& [id, _] : ctx.upstream_outputs) c.upstream_ids.push_back(id);
    calls.push_back(c);

    for (const auto& f : fail_for) {
      if (f == ctx.capability_id) {
        return DispatchError{"deliberate test failure for " + f};
      }
    }
    return std::make_shared<std::string>(c.capability_id);
  }
};

Pipeline parse_or_die(std::string_view src) {
  auto r = parse_pipeline(src);
  if (auto* e = std::get_if<ParseError>(&r)) {
    ADD_FAILURE() << "parse failed: " << e->message;
    return {};
  }
  return std::get<Pipeline>(std::move(r));
}

TEST(Runner, ValidationFailureReportedWithoutDispatch) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {y: {from: missing}}}
)yaml");
  MockDispatcher d;
  Cache c;
  auto r = run_pipeline(p, d, c);
  EXPECT_EQ(r.status, RunResult::Status::ValidationFailed);
  EXPECT_FALSE(r.validation_errors.empty());
  EXPECT_TRUE(d.calls.empty());
}

TEST(Runner, SuccessfulRunCallsAllStagesInTopologicalOrder) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: import, plugin: reader.step, input: {path: x}}
  - {id: mesh,   plugin: mesher.x,    input: {g: {from: import}}}
  - {id: solve,  plugin: solver.x,    input: {m: {from: mesh}}}
)yaml");
  MockDispatcher d;
  Cache c;
  auto r = run_pipeline(p, d, c);
  EXPECT_EQ(r.status, RunResult::Status::Success);
  ASSERT_EQ(d.calls.size(), 3u);
  EXPECT_EQ(d.calls[0].capability_id, "reader.step");
  EXPECT_EQ(d.calls[1].capability_id, "mesher.x");
  EXPECT_EQ(d.calls[2].capability_id, "solver.x");
  // Each downstream call sees all upstream outputs in its context.
  EXPECT_EQ(d.calls[1].upstream_ids.size(), 1u);  // import
  EXPECT_EQ(d.calls[2].upstream_ids.size(), 2u);  // import + mesh
}

TEST(Runner, OutputsAvailableByStageId) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 1}}
  - {id: b, plugin: y, input: {a: {from: a}}}
)yaml");
  MockDispatcher d;
  Cache c;
  auto r = run_pipeline(p, d, c);
  EXPECT_EQ(r.status, RunResult::Status::Success);
  ASSERT_EQ(r.outputs.size(), 2u);
  ASSERT_NE(r.outputs.find("a"), r.outputs.end());
  EXPECT_EQ(*static_cast<std::string*>(r.outputs["a"].get()), "x");
}

TEST(Runner, FailureStopsByDefaultAndSkipsDownstream) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x}
  - {id: b, plugin: y, input: {a: {from: a}}}
  - {id: c, plugin: z, input: {b: {from: b}}}
)yaml");
  MockDispatcher d;
  d.fail_for = {"y"};
  Cache c;
  auto r = run_pipeline(p, d, c);
  EXPECT_EQ(r.status, RunResult::Status::StageFailed);
  ASSERT_EQ(r.stage_results.size(), 3u);
  EXPECT_EQ(r.stage_results[0].status, StageRunResult::Status::Executed);
  EXPECT_EQ(r.stage_results[1].status, StageRunResult::Status::Failed);
  EXPECT_EQ(r.stage_results[2].status, StageRunResult::Status::Skipped);
  ASSERT_EQ(d.calls.size(), 2u);  // c was not dispatched
}

TEST(Runner, CacheHitOnSecondRunSkipsDispatch) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 1}}
  - {id: b, plugin: y, input: {q: {from: a}}}
)yaml");
  MockDispatcher d;
  Cache c;
  auto r1 = run_pipeline(p, d, c);
  EXPECT_EQ(r1.status, RunResult::Status::Success);
  EXPECT_EQ(d.calls.size(), 2u);

  // Second run with the same pipeline + cache should be 100% cached.
  d.calls.clear();
  auto r2 = run_pipeline(p, d, c);
  EXPECT_EQ(r2.status, RunResult::Status::Success);
  EXPECT_EQ(d.calls.size(), 0u) << "expected zero dispatch on full cache hit";
  for (const auto& sr : r2.stage_results) {
    EXPECT_EQ(sr.status, StageRunResult::Status::Cached);
  }
}

TEST(Runner, ChangingInputsInvalidatesCache) {
  auto p1 = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 1}}
)yaml");
  auto p2 = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 2}}
)yaml");
  MockDispatcher d;
  Cache c;
  run_pipeline(p1, d, c);
  EXPECT_EQ(d.calls.size(), 1u);
  run_pipeline(p2, d, c);
  EXPECT_EQ(d.calls.size(), 2u);  // re-dispatched because input differs
}

TEST(Runner, PluginVersionFoldsIntoCacheKey) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 1}}
)yaml");
  MockDispatcher d;
  Cache c;
  d.versions["x"] = "1.0";
  run_pipeline(p, d, c);
  d.versions["x"] = "2.0";
  run_pipeline(p, d, c);
  EXPECT_EQ(d.calls.size(), 2u);  // version change invalidated the cache
}

TEST(Runner, UpstreamChangeInvalidatesDownstreamCache) {
  auto p1 = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 1}}
  - {id: b, plugin: y, input: {a: {from: a}}}
)yaml");
  auto p2 = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {p: 2}}
  - {id: b, plugin: y, input: {a: {from: a}}}
)yaml");
  MockDispatcher d;
  Cache c;
  run_pipeline(p1, d, c);
  EXPECT_EQ(d.calls.size(), 2u);
  run_pipeline(p2, d, c);
  EXPECT_EQ(d.calls.size(), 4u);  // both a and b re-run
}

TEST(Runner, UseCacheFalseSkipsCacheLookup) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x}
)yaml");
  MockDispatcher d;
  Cache c;
  RunOptions opts;
  opts.use_cache = false;
  run_pipeline(p, d, c, opts);
  run_pipeline(p, d, c, opts);
  EXPECT_EQ(d.calls.size(), 2u);  // no caching in either run
  EXPECT_EQ(c.size(),       0u);  // cache untouched
}

}  // namespace
