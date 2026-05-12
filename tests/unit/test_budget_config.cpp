// SPDX-License-Identifier: Apache-2.0
//
// Sprint 6 push 6 — pins the .souxmar/budget.toml schema. The shape
// here is small on purpose; the production cost meter gets per-provider
// + per-tool knobs in Sprint 14 alongside the managed-AI proxy.

#include "souxmar/ai/audit_log.h"
#include "souxmar/ai/budget_config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <variant>

using namespace souxmar::ai;

namespace {

const auto* kValid = R"toml(
[budget]
max_input_tokens  = 200000
max_output_tokens =  50000
max_total_tokens  = 250000
)toml";

}

TEST(BudgetConfig, ValidParse) {
  auto r = parse_budget_config(kValid);
  ASSERT_TRUE(std::holds_alternative<BudgetConfig>(r)) << std::get<BudgetConfigError>(r).message;
  const auto& cfg = std::get<BudgetConfig>(r);
  EXPECT_EQ(cfg.max_input_tokens, 200000u);
  EXPECT_EQ(cfg.max_output_tokens, 50000u);
  EXPECT_EQ(cfg.max_total_tokens, 250000u);
}

TEST(BudgetConfig, MissingFieldsDefaultToUnlimited) {
  // Empty file is valid — every cap defaults to 0 (unlimited).
  auto r = parse_budget_config("");
  ASSERT_TRUE(std::holds_alternative<BudgetConfig>(r));
  const auto& cfg = std::get<BudgetConfig>(r);
  EXPECT_EQ(cfg.max_input_tokens, 0u);
  EXPECT_EQ(cfg.max_output_tokens, 0u);
  EXPECT_EQ(cfg.max_total_tokens, 0u);
}

TEST(BudgetConfig, NegativeRejected) {
  auto r = parse_budget_config(R"toml(
[budget]
max_total_tokens = -1
)toml");
  ASSERT_TRUE(std::holds_alternative<BudgetConfigError>(r));
  EXPECT_EQ(std::get<BudgetConfigError>(r).field, "budget.max_total_tokens");
}

TEST(BudgetConfig, WrongTypeRejected) {
  auto r = parse_budget_config(R"toml(
[budget]
max_input_tokens = "huge"
)toml");
  ASSERT_TRUE(std::holds_alternative<BudgetConfigError>(r));
  EXPECT_EQ(std::get<BudgetConfigError>(r).field, "budget.max_input_tokens");
}

TEST(BudgetConfig, MalformedTomlReportsLine) {
  auto r = parse_budget_config("[budget\nmax = ");
  ASSERT_TRUE(std::holds_alternative<BudgetConfigError>(r));
  EXPECT_TRUE(std::get<BudgetConfigError>(r).line.has_value());
}

TEST(BudgetConfig, ApplyToSetsCapsLeavesCountersAlone) {
  SessionBudget budget;
  budget.consumed_input = 10;
  budget.consumed_output = 20;

  BudgetConfig cfg;
  cfg.max_input_tokens = 500;
  cfg.max_output_tokens = 600;
  cfg.max_total_tokens = 1000;
  cfg.apply_to(budget);

  EXPECT_EQ(budget.max_input_tokens, 500u);
  EXPECT_EQ(budget.max_output_tokens, 600u);
  EXPECT_EQ(budget.max_total_tokens, 1000u);
  EXPECT_EQ(budget.consumed_input, 10u);
  EXPECT_EQ(budget.consumed_output, 20u);
}

TEST(BudgetConfig, DefaultPathRespectsProjectRoot) {
  const auto root = std::filesystem::path("/tmp/some-project");
  const auto p = default_budget_config_path(root);
  EXPECT_EQ(p, root / ".souxmar" / "budget.toml");
}

TEST(BudgetConfig, FileRoundTrip) {
  // Stage a real file in TempDir and round-trip through the file API.
  const auto tmp = std::filesystem::temp_directory_path() / "souxmar-budget-config-test";
  std::filesystem::create_directories(tmp);
  const auto path = tmp / "budget.toml";
  std::ofstream(path) << kValid;

  auto r = parse_budget_config_file(path);
  ASSERT_TRUE(std::holds_alternative<BudgetConfig>(r));
  EXPECT_EQ(std::get<BudgetConfig>(r).max_total_tokens, 250000u);

  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
}
