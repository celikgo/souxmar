// SPDX-License-Identifier: Apache-2.0
//
// Per-project session-budget config — Sprint 6 push 6.
//
// The agent runtime reads `.souxmar/budget.toml` (or a path the user
// names with `--budget-config`) to populate a SessionBudget's max_*
// caps before the first tool dispatch. This is the "cost meter"
// surface called out in the Sprint 6 plan: a project can pin how many
// tokens its agent sessions are allowed to spend, and the threshold
// callback fires at 50% / 80% / 100% of any axis.
//
// Format (all fields optional; missing → 0 = unlimited on that axis):
//
//   [budget]
//   max_input_tokens  = 200000
//   max_output_tokens =  50000
//   max_total_tokens  = 250000
//
// `0` is the documented "unlimited" sentinel — explicit, matches the
// SessionBudget contract. Negative values are rejected.
//
// We deliberately keep the schema small: the production cost meter
// gets additional knobs (per-provider rate caps, per-tool budgets) in
// Sprint 14 when the managed-AI proxy lands. The current shape is
// what the open-core agent needs to make budget enforcement testable
// today.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include "souxmar/ai/audit_log.h"

namespace souxmar::ai {

// Parsed [budget] section. Apply to a `SessionBudget` via apply_to().
struct BudgetConfig {
  std::size_t max_input_tokens  = 0;
  std::size_t max_output_tokens = 0;
  std::size_t max_total_tokens  = 0;

  // Set the SessionBudget's max_* caps from this config. Leaves the
  // counters and the threshold callback untouched.
  void apply_to(SessionBudget& budget) const noexcept {
    budget.max_input_tokens  = max_input_tokens;
    budget.max_output_tokens = max_output_tokens;
    budget.max_total_tokens  = max_total_tokens;
  }
};

struct BudgetConfigError {
  std::string                  message;
  std::optional<std::size_t>   line;
  std::string                  field;     // dotted path "budget.max_input_tokens"
};

using BudgetConfigResult = std::variant<BudgetConfig, BudgetConfigError>;

// Parse from a TOML string.
[[nodiscard]] BudgetConfigResult parse_budget_config(std::string_view toml_source);

// Parse from a TOML file. FileIo failures (missing file, permission
// denied) come back as BudgetConfigError with `message` describing the
// underlying error.
[[nodiscard]] BudgetConfigResult
parse_budget_config_file(const std::filesystem::path& path);

// Resolve the per-project default location: `<project_root>/.souxmar/budget.toml`
// (or `<cwd>/.souxmar/budget.toml` if `project_root` is empty).
[[nodiscard]] std::filesystem::path
default_budget_config_path(const std::filesystem::path& project_root = {});

}  // namespace souxmar::ai
