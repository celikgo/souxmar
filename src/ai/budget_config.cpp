// SPDX-License-Identifier: Apache-2.0

#include "souxmar/ai/budget_config.h"

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include <fstream>
#include <sstream>

namespace souxmar::ai {

namespace {

BudgetConfigError make_error(std::string message,
                             std::optional<std::size_t> line = std::nullopt,
                             std::string field = {}) {
  return BudgetConfigError{std::move(message), line, std::move(field)};
}

bool read_size(const toml::table& tbl,
               std::string_view path,
               std::size_t& out,
               BudgetConfigError& err) {
  auto node = tbl.at_path(path);
  if (!node)
    return true;  // optional; leave default
  auto v = node.value<std::int64_t>();
  if (!v) {
    err = make_error(fmt::format("'{}' must be an integer", path), std::nullopt, std::string{path});
    return false;
  }
  if (*v < 0) {
    err = make_error(fmt::format("'{}' must be non-negative (0 = unlimited)", path),
                     std::nullopt,
                     std::string{path});
    return false;
  }
  out = static_cast<std::size_t>(*v);
  return true;
}

}  // namespace

BudgetConfigResult parse_budget_config(std::string_view toml_source) {
  toml::table tbl;
  try {
    tbl = toml::parse(toml_source);
  } catch (const toml::parse_error& e) {
    const auto& src = e.source();
    return make_error(fmt::format("TOML parse error: {}", e.description()),
                      static_cast<std::size_t>(src.begin.line));
  }

  BudgetConfig cfg;
  BudgetConfigError err;
  if (!read_size(tbl, "budget.max_input_tokens", cfg.max_input_tokens, err))
    return err;
  if (!read_size(tbl, "budget.max_output_tokens", cfg.max_output_tokens, err))
    return err;
  if (!read_size(tbl, "budget.max_total_tokens", cfg.max_total_tokens, err))
    return err;
  return cfg;
}

BudgetConfigResult parse_budget_config_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_error(fmt::format("cannot open '{}'", path.string()));
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return parse_budget_config(buf.str());
}

std::filesystem::path default_budget_config_path(const std::filesystem::path& project_root) {
  const auto base = project_root.empty() ? std::filesystem::current_path() : project_root;
  return base / ".souxmar" / "budget.toml";
}

}  // namespace souxmar::ai
