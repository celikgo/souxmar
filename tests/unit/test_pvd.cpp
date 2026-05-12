// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/pvd.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace souxmar::core::pvd;

namespace {

constexpr std::string_view kBasic = R"(<?xml version="1.0"?>
<VTKFile type="Collection" version="0.1">
  <Collection>
    <DataSet timestep="0.000" file="step_0000.vtu"/>
    <DataSet timestep="0.001" file="step_0001.vtu"/>
    <DataSet timestep="0.002" file="step_0002.vtu"/>
  </Collection>
</VTKFile>
)";

}  // namespace

TEST(Pvd, BasicThreeFrames) {
  const auto r = parse(kBasic);
  EXPECT_EQ(r.error, "");
  ASSERT_EQ(r.entries.size(), 3u);
  EXPECT_DOUBLE_EQ(r.entries[0].timestep, 0.000);
  EXPECT_DOUBLE_EQ(r.entries[1].timestep, 0.001);
  EXPECT_DOUBLE_EQ(r.entries[2].timestep, 0.002);
  EXPECT_EQ(r.entries[0].file, "step_0000.vtu");
  EXPECT_EQ(r.entries[1].file, "step_0001.vtu");
  EXPECT_EQ(r.entries[2].file, "step_0002.vtu");
}

TEST(Pvd, AttributeOrderIsIgnored) {
  // file= before timestep=
  const std::string_view xml = R"(
    <DataSet file="a.vtu" timestep="1.5"/>
    <DataSet timestep="2.5" file="b.vtu"/>
  )";
  const auto r = parse(xml);
  EXPECT_EQ(r.error, "");
  ASSERT_EQ(r.entries.size(), 2u);
  EXPECT_DOUBLE_EQ(r.entries[0].timestep, 1.5);
  EXPECT_EQ(r.entries[0].file, "a.vtu");
  EXPECT_DOUBLE_EQ(r.entries[1].timestep, 2.5);
  EXPECT_EQ(r.entries[1].file, "b.vtu");
}

TEST(Pvd, SingleQuotesAccepted) {
  const std::string_view xml = R"(<DataSet timestep='3.14' file='pi.vtu'/>)";
  const auto r = parse(xml);
  EXPECT_EQ(r.error, "");
  ASSERT_EQ(r.entries.size(), 1u);
  EXPECT_DOUBLE_EQ(r.entries[0].timestep, 3.14);
  EXPECT_EQ(r.entries[0].file, "pi.vtu");
}

TEST(Pvd, NonUniformTimesteps) {
  const std::string_view xml = R"(
    <DataSet timestep="0.0"   file="a.vtu"/>
    <DataSet timestep="0.01"  file="b.vtu"/>
    <DataSet timestep="0.025" file="c.vtu"/>
    <DataSet timestep="0.05"  file="d.vtu"/>
  )";
  const auto r = parse(xml);
  ASSERT_EQ(r.entries.size(), 4u);
  EXPECT_DOUBLE_EQ(r.entries[0].timestep, 0.0);
  EXPECT_DOUBLE_EQ(r.entries[1].timestep, 0.01);
  EXPECT_DOUBLE_EQ(r.entries[2].timestep, 0.025);
  EXPECT_DOUBLE_EQ(r.entries[3].timestep, 0.05);
}

TEST(Pvd, EmptyInputYieldsEmptyEntriesNoError) {
  const auto r = parse("");
  EXPECT_EQ(r.error, "");
  EXPECT_TRUE(r.entries.empty());
}

TEST(Pvd, TagWithoutRequiredAttributesIsSkipped) {
  const std::string_view xml = R"(
    <DataSet timestep="1.0"/>
    <DataSet file="orphan.vtu"/>
    <DataSet timestep="2.0" file="ok.vtu"/>
  )";
  const auto r = parse(xml);
  EXPECT_EQ(r.error, "");
  ASSERT_EQ(r.entries.size(), 1u);
  EXPECT_DOUBLE_EQ(r.entries[0].timestep, 2.0);
  EXPECT_EQ(r.entries[0].file, "ok.vtu");
}

TEST(Pvd, MalformedTimestepIsTypedError) {
  const std::string_view xml = R"(<DataSet timestep="not-a-number" file="x.vtu"/>)";
  const auto r = parse(xml);
  EXPECT_NE(r.error.find("not-a-number"), std::string::npos);
}

TEST(Pvd, AttributePrefixMatchIsRejected) {
  // "footimestep" should NOT be misread as "timestep".
  const std::string_view xml = R"(<DataSet footimestep="9" file="x.vtu"/>)";
  const auto r = parse(xml);
  EXPECT_EQ(r.error, "");
  EXPECT_TRUE(r.entries.empty());
}

TEST(Pvd, DataSetXIsNotMatchedAsDataSet) {
  // "<DataSetX " or "<DataSetCollection" should not match "<DataSet".
  const std::string_view xml = R"(<DataSetX timestep="1" file="x.vtu"/>)";
  const auto r = parse(xml);
  EXPECT_EQ(r.error, "");
  EXPECT_TRUE(r.entries.empty());
}
