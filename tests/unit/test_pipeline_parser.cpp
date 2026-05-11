// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/parser.h"

#include <gtest/gtest.h>

using namespace souxmar::pipeline;

namespace {

constexpr std::string_view kCantilever = R"yaml(
version: 1
stages:
  - id: import
    plugin: reader.step
    input:
      path: beam.step

  - id: mesh
    plugin: mesher.tetra.native
    input:
      geometry: { from: import }
      target_size: 5.0e-3

  - id: solve
    plugin: solver.elasticity.linear
    input:
      mesh: { from: mesh }
      material:
        youngs_modulus: 210e9
        poissons_ratio: 0.3
      bcs:
        - { tag: clamped_face, type: dirichlet, value: [0, 0, 0] }
        - { tag: tip_face,     type: neumann,   value: [0, -1000, 0] }

  - id: export
    plugin: writer.vtu
    input:
      mesh:   { from: mesh }
      fields: { from: solve }
      path:   results/cantilever.vtu
)yaml";

TEST(Parser, CantileverPipelineParses) {
  auto r = parse_pipeline(kCantilever);
  ASSERT_TRUE(std::holds_alternative<Pipeline>(r))
      << "got error: " << std::get<ParseError>(r).message;
  const auto& p = std::get<Pipeline>(r);
  EXPECT_EQ(p.version, 1);
  ASSERT_EQ(p.stages.size(), 4u);
  EXPECT_EQ(p.stages[0].id,     "import");
  EXPECT_EQ(p.stages[0].plugin, "reader.step");
  EXPECT_EQ(p.stages[1].id,     "mesh");
  EXPECT_EQ(p.stages[3].id,     "export");
}

TEST(Parser, StageRefRecognised) {
  auto r = parse_pipeline(kCantilever);
  ASSERT_TRUE(std::holds_alternative<Pipeline>(r));
  const auto& mesh_input = std::get<Pipeline>(r).stages[1].input;
  ASSERT_NE(mesh_input.find("geometry"), nullptr);
  ASSERT_EQ(mesh_input.find("geometry")->kind(), Value::Kind::Stage);
  EXPECT_EQ(mesh_input.find("geometry")->as_stage().stage_id, "import");
}

TEST(Parser, NumericInputsAreNumbers) {
  auto r = parse_pipeline(kCantilever);
  ASSERT_TRUE(std::holds_alternative<Pipeline>(r));
  const auto& mesh_input = std::get<Pipeline>(r).stages[1].input;
  ASSERT_NE(mesh_input.find("target_size"), nullptr);
  EXPECT_EQ(mesh_input.find("target_size")->kind(), Value::Kind::Number);
  EXPECT_DOUBLE_EQ(mesh_input.find("target_size")->as_number(), 5.0e-3);
}

TEST(Parser, ListsParse) {
  auto r = parse_pipeline(kCantilever);
  ASSERT_TRUE(std::holds_alternative<Pipeline>(r));
  const auto& solve_input = std::get<Pipeline>(r).stages[2].input;
  const auto* bcs = solve_input.find("bcs");
  ASSERT_NE(bcs, nullptr);
  ASSERT_EQ(bcs->kind(), Value::Kind::List);
  EXPECT_EQ(bcs->as_list().size(), 2u);
}

TEST(Parser, MissingVersionRejected) {
  auto r = parse_pipeline("stages: [{id: a, plugin: x}]");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("version"), std::string::npos);
}

TEST(Parser, MissingStagesRejected) {
  auto r = parse_pipeline("version: 1");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("stages"), std::string::npos);
}

TEST(Parser, EmptyStagesRejected) {
  auto r = parse_pipeline("version: 1\nstages: []\n");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("at least one"), std::string::npos);
}

TEST(Parser, DuplicateStageIdRejected) {
  auto r = parse_pipeline(R"yaml(
version: 1
stages:
  - {id: a, plugin: x}
  - {id: a, plugin: y}
)yaml");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("duplicate"), std::string::npos);
}

TEST(Parser, MissingStageIdRejected) {
  auto r = parse_pipeline("version: 1\nstages: [{plugin: x}]\n");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("`id`"), std::string::npos);
}

TEST(Parser, MissingStagePluginRejected) {
  auto r = parse_pipeline("version: 1\nstages: [{id: a}]\n");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("`plugin`"), std::string::npos);
}

TEST(Parser, NonV1RejectedExplicitly) {
  auto r = parse_pipeline("version: 9\nstages: [{id: a, plugin: x}]\n");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("v1"), std::string::npos);
}

TEST(Parser, UnknownInputTypePreservesAsString) {
  // A scalar that isn't bool / number is kept as string. "1.2.3" looks
  // numeric to a permissive parser; we want it to stay a string.
  auto r = parse_pipeline(R"yaml(
version: 1
stages:
  - id: a
    plugin: x
    input:
      version_string: "1.2.3"
)yaml");
  ASSERT_TRUE(std::holds_alternative<Pipeline>(r));
  const auto& input = std::get<Pipeline>(r).stages[0].input;
  ASSERT_NE(input.find("version_string"), nullptr);
  EXPECT_EQ(input.find("version_string")->kind(), Value::Kind::String);
  EXPECT_EQ(input.find("version_string")->as_string(), "1.2.3");
}

TEST(Parser, MalformedYamlReportsLine) {
  auto r = parse_pipeline("version: 1\nstages:\n  - {");
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_TRUE(std::get<ParseError>(r).line.has_value());
}

}  // namespace
