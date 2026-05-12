// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/dag.h"
#include "souxmar/pipeline/parser.h"

#include <gtest/gtest.h>

using namespace souxmar::pipeline;

namespace {

Pipeline parse_or_die(std::string_view src) {
  auto r = parse_pipeline(src);
  if (auto* e = std::get_if<ParseError>(&r)) {
    ADD_FAILURE() << "parse failed: " << e->message;
    return {};
  }
  return std::get<Pipeline>(std::move(r));
}

TEST(Dag, SimpleLineHasOriginalOrder) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: r}
  - {id: b, plugin: m, input: {x: {from: a}}}
  - {id: c, plugin: w, input: {y: {from: b}}}
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<std::size_t>>(r));
  const auto& order = std::get<std::vector<std::size_t>>(r);
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 0u);
  EXPECT_EQ(order[1], 1u);
  EXPECT_EQ(order[2], 2u);
}

TEST(Dag, DiamondTopologicalSortStable) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: r}
  - {id: b, plugin: x, input: {x: {from: a}}}
  - {id: c, plugin: x, input: {x: {from: a}}}
  - {id: d, plugin: w, input: {l: {from: b}, r: {from: c}}}
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<std::size_t>>(r));
  const auto& order = std::get<std::vector<std::size_t>>(r);
  ASSERT_EQ(order.size(), 4u);
  EXPECT_EQ(order[0], 0u);  // a
  EXPECT_EQ(order[3], 3u);  // d last
  // b and c are interchangeable; we sort by index so we expect b then c.
  EXPECT_EQ(order[1], 1u);
  EXPECT_EQ(order[2], 2u);
}

TEST(Dag, SelfReferenceRejected) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {self: {from: a}}}
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<DagError>>(r));
  EXPECT_NE(std::get<std::vector<DagError>>(r)[0].message.find("references itself"),
            std::string::npos);
}

TEST(Dag, DanglingReferenceRejected) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x, input: {y: {from: nonexistent}}}
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<DagError>>(r));
  EXPECT_NE(std::get<std::vector<DagError>>(r)[0].message.find("unknown stage"), std::string::npos);
}

TEST(Dag, CycleDetected) {
  // YAML order doesn't dictate dependency: b depends on c, c depends on b.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: x}
  - {id: b, plugin: x, input: {z: {from: c}}}
  - {id: c, plugin: x, input: {z: {from: b}}}
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<DagError>>(r));
  EXPECT_NE(std::get<std::vector<DagError>>(r)[0].message.find("cycle"), std::string::npos);
}

TEST(Dag, ReferenceFromNestedListAndMap) {
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: r}
  - id: b
    plugin: x
    input:
      bcs:
        - { mesh: { from: a } }
)yaml");
  auto r = validate(p);
  ASSERT_TRUE(std::holds_alternative<std::vector<std::size_t>>(r));
}

TEST(CollectStageRefs, WalksLeafs) {
  auto v = Value::map({
      {"x", Value::stage_ref("foo")},
      {"y", Value::list({Value::stage_ref("bar"), Value::number(1.0)})},
      {"z", Value::number(2.0)},
  });
  auto refs = collect_stage_refs(v);
  ASSERT_EQ(refs.size(), 2u);
  // Order isn't guaranteed (std::map iteration), but the SET is.
  std::sort(refs.begin(), refs.end());
  EXPECT_EQ(refs[0], "bar");
  EXPECT_EQ(refs[1], "foo");
}

}  // namespace
