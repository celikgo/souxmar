// SPDX-License-Identifier: Apache-2.0
//
// Sprint 5 push 1 gate test — runs the v1 plugin conformance suite
// against all three in-tree example plugins (hello-mesher, hello-writer,
// vtu-writer). When this is green, the souxmar-c headers are eligible
// for ABI v1 frozen-candidate marking (see ADR-0004).
//
// Each plugin's report is asserted to have:
//   * all_passed() == true,
//   * every check in {C001 … C010} present and Pass,
// so a regression in any check fails this test immediately.

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "souxmar/plugin/conformance.h"
#include "souxmar/plugin/discovery.h"

#include "test_config.h"

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

fs::path plugins_root() {
  // SOUXMAR_TEST_HELLO_MESHER_DIR resolves to the per-plugin build dir;
  // discovery wants the parent so it walks every subdirectory.
  return fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
}

const plugin::DiscoveredPlugin*
find_plugin(const plugin::DiscoveryReport& r, std::string_view id) {
  for (const auto& p : r.loaded) {
    if (p.manifest.id == id) return &p;
  }
  return nullptr;
}

// Pretty-prints a failed report so debugging is easy when the gate breaks.
std::string format_report(const plugin::ConformanceReport& r) {
  std::string out = "plugin: " + r.plugin_id + "\n";
  for (const auto& cr : r.results) {
    out += "  " + cr.check_id + " " +
           std::string(plugin::to_string(cr.outcome)) +
           (cr.detail.empty() ? "" : "  -- " + cr.detail) + "\n";
  }
  return out;
}

void expect_all_v1_checks_pass(const plugin::ConformanceReport& report) {
  // First the headline assertion. If this fires, dump the full report so
  // CI logs point straight at the broken check.
  EXPECT_TRUE(report.all_passed()) << format_report(report);

  // Then per-check presence + Pass — guards against a future refactor
  // that drops a check by accident.
  const std::set<std::string> expected{
      "C001","C002","C003","C004","C005","C006","C007","C008","C009","C010"};
  std::set<std::string> seen;
  for (const auto& cr : report.results) {
    seen.insert(cr.check_id);
    EXPECT_EQ(cr.outcome, plugin::ConformanceOutcome::Pass)
        << "check " << cr.check_id << " (" << cr.detail << ") for plugin "
        << report.plugin_id;
  }
  EXPECT_EQ(seen, expected)
      << "v1 suite must run exactly C001..C010 — saw " << seen.size() << " checks";
}

class ConformanceGateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty())
        << "no plugins discovered under " << plugins_root();
  }
  plugin::DiscoveryReport discovery_;
};

TEST_F(ConformanceGateTest, AllChecksCatalogueMatchesExpectedV1Set) {
  // Sanity: the catalogue is what we documented in the header.
  const auto checks = plugin::all_checks();
  ASSERT_EQ(checks.size(), 10u);
  for (std::size_t i = 0; i < checks.size(); ++i) {
    const auto expected_id = "C00" + std::to_string(i + 1);
    EXPECT_EQ(checks[i].id, i == 9 ? "C010" : expected_id);
  }
}

TEST_F(ConformanceGateTest, HelloMesherPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.hello-mesher");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

TEST_F(ConformanceGateTest, HelloWriterPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.hello-writer");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

TEST_F(ConformanceGateTest, VtuWriterPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.vtu-writer");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

// Sprint 5 push 3 added two new in-tree plugins. The gate test asserts
// they also pass every v1 check — same bar as the original three.

TEST_F(ConformanceGateTest, HeatSolverPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.heat-solver");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

TEST_F(ConformanceGateTest, ScalarMagnitudePostprocPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.scalar-magnitude");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

// Sprint 6 push 1 adds the in-tree mesh-quality postproc plugin.
// The conformance gate must stay green for it, just like the other five.
TEST_F(ConformanceGateTest, MeshQualityPostprocPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.mesh-quality");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

// Sprint 6 push 4 — the first reader.* plugin lands. ABI minor v1.1 is
// the additive change; the conformance suite is unchanged and must
// still pass for every in-tree plugin.
TEST_F(ConformanceGateTest, StlReaderPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.stl-reader");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

// Sprint 6 push 5 — second tetrahedral mesher. The suite covers any
// mesher.* plugin shape, not just hello-mesher's, so the bar is exactly
// the same.
TEST_F(ConformanceGateTest, GridMesherPassesAllChecks) {
  const auto* p = find_plugin(discovery_, "dev.souxmar.examples.grid-mesher");
  ASSERT_NE(p, nullptr);
  expect_all_v1_checks_pass(plugin::run_conformance(*p));
}

// Negative: confirm that a deliberately-mismatched manifest (declared ABI
// 99) trips C001 + Skips the rest. Builds on the discovered hello-mesher
// to keep the test self-contained — we fabricate a DiscoveredPlugin in
// memory rather than perturbing an on-disk manifest.
TEST_F(ConformanceGateTest, MismatchedAbiTripsC001AndSkipsDownstream) {
  const auto* base = find_plugin(discovery_, "dev.souxmar.examples.hello-mesher");
  ASSERT_NE(base, nullptr);

  plugin::DiscoveredPlugin bad = *base;
  bad.manifest.abi = 99;  // host expects 1

  const auto r = plugin::run_conformance(bad);
  ASSERT_FALSE(r.all_passed());
  ASSERT_EQ(r.results.size(), 10u);
  EXPECT_EQ(r.results[0].check_id, "C001");
  EXPECT_EQ(r.results[0].outcome,  plugin::ConformanceOutcome::Fail);
  // Every subsequent check must be Skip with C001 attribution.
  for (std::size_t i = 1; i < r.results.size(); ++i) {
    EXPECT_EQ(r.results[i].outcome, plugin::ConformanceOutcome::Skip)
        << r.results[i].check_id << " should skip after C001 failed";
    EXPECT_NE(r.results[i].detail.find("C001"), std::string::npos);
  }
}

}  // namespace
