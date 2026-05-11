// SPDX-License-Identifier: Apache-2.0
//
// souxmar — the host CLI.
//
// Subcommands (Sprint 3 push 3):
//   souxmar run <pipeline.yaml>   Parse, validate, and execute a pipeline.
//   souxmar plugin list           Discover plugins and print their capabilities.
//   souxmar version               Print version + ABI info.
//
// The CLI is intentionally thin: every interesting decision lives in the
// libraries (souxmar-pipeline, souxmar-plugin, souxmar-core). The CLI's
// job is argument parsing, plugin-search-path assembly, and pretty-printing.
//
// Exit codes follow sysexits.h conventions:
//   0  success
//   64 usage error
//   65 input data error (parse / validate failure)
//   70 internal error (plugin load / dispatch failure)

#include <fmt/core.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "souxmar/ai/audit_log.h"
#include "souxmar/ai/budget_config.h"   // Sprint 6 push 6
#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/index.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"
#include "souxmar/version.h"

namespace fs = std::filesystem;

namespace {

constexpr int kExitOk        = 0;
constexpr int kExitUsage     = 64;
constexpr int kExitInputData = 65;
constexpr int kExitInternal  = 70;

void print_usage() {
  fmt::print(stderr,
      "souxmar {} — open-source CAE pipeline runner\n"
      "\n"
      "Usage:\n"
      "  souxmar run <pipeline.yaml> [--no-cache] [--cache-dir <dir>] [--plugin-path <dir>]...\n"
      "  souxmar plugin list [--plugin-path <dir>]...\n"
      "  souxmar plugin search [<query>] [--capability <prefix>] [--index <path>]\n"
      "  souxmar plugin validate-index [--index <path>]\n"
      "  souxmar agent list\n"
      "  souxmar agent invoke <tool> [--input <yaml>] [--input-file <path>] [--yes]\n"
      "                              [--audit-log <path>] [--budget-config <path>]\n"
      "                              [--plugin-path <dir>]...\n"
      "  souxmar version\n"
      "  souxmar help\n"
      "\n"
      "Plugin search path comes from (in priority order):\n"
      "  1. --plugin-path flags (repeatable)\n"
      "  2. $SOUXMAR_PLUGIN_PATH (colon-separated on POSIX, semicolon on Windows)\n"
      "  3. Per-user platform default\n"
      "\n"
      "Cache directory: --cache-dir, then $SOUXMAR_CACHE_DIR, then platform default\n"
      "  (~/Library/Caches/souxmar on macOS, $XDG_CACHE_HOME/souxmar on Linux,\n"
      "   %LOCALAPPDATA%\\souxmar\\cache on Windows).\n",
      souxmar::version_string());
}

// Pop one argument from `args`, asserting the next token is `--flag`'s value.
// Returns std::nullopt on missing / empty value.
std::optional<std::string> pop_value(std::vector<std::string>& args,
                                     std::size_t&              i,
                                     std::string_view          flag) {
  if (i + 1 >= args.size() || args[i + 1].empty()) {
    fmt::print(stderr, "error: {} requires a value\n", flag);
    return std::nullopt;
  }
  ++i;
  return args[i];
}

// Build a discovery report with the user's --plugin-path overrides folded in
// before the platform defaults. Returns the report; caller decides what to
// do with rejections.
souxmar::plugin::DiscoveryReport
discover_with_overrides(const std::vector<fs::path>& extra_paths) {
  souxmar::plugin::DiscoveryOptions opts;
  opts.include_env_path       = true;
  opts.include_user_prefix    = true;
  opts.include_install_prefix = false;  // CLI not installed yet — Sprint 4.
  opts.include_cwd            = false;

  auto search_paths = souxmar::plugin::default_search_paths(opts);
  // Prepend the explicit overrides so they win priority.
  search_paths.insert(search_paths.begin(), extra_paths.begin(), extra_paths.end());
  return souxmar::plugin::discover_plugins(search_paths);
}

// ---- Subcommands ---------------------------------------------------------

int cmd_version() {
  fmt::print("souxmar {} (ABI v{})\n",
             souxmar::version_string(),
             souxmar::abi_version());
  return kExitOk;
}

int cmd_plugin_list(const std::vector<fs::path>& extra_paths) {
  const auto report = discover_with_overrides(extra_paths);

  if (report.loaded.empty() && report.rejected.empty()) {
    fmt::print(stderr,
        "no plugins found. Set $SOUXMAR_PLUGIN_PATH or pass --plugin-path.\n");
    return kExitOk;
  }

  for (const auto& d : report.loaded) {
    fmt::print("{} ({})\n", d.manifest.id, d.manifest.version);
    fmt::print("  manifest:    {}\n", d.manifest_path.string());
    fmt::print("  binary:      {}\n", d.binary_path.string());
    fmt::print("  abi:         {}\n", d.manifest.abi);
    if (!d.manifest.description.empty()) {
      fmt::print("  description: {}\n", d.manifest.description);
    }
    fmt::print("  capabilities:\n");
    for (const auto& cap : d.manifest.capabilities) {
      fmt::print("    - {}\n", cap);
    }
    if (!d.manifest.tags.empty()) {
      fmt::print("  tags:        ");
      for (std::size_t i = 0; i < d.manifest.tags.size(); ++i) {
        fmt::print("{}{}", d.manifest.tags[i],
                   i + 1 == d.manifest.tags.size() ? "" : ", ");
      }
      fmt::print("\n");
    }
    fmt::print("\n");
  }

  if (!report.rejected.empty()) {
    fmt::print(stderr, "{} plugin(s) rejected:\n", report.rejected.size());
    for (const auto& r : report.rejected) {
      // Surface the structured code so log parsers can group rejections
      // by class without grepping the free-form message.
      if (r.manifest_code.has_value()) {
        fmt::print(stderr, "  - {}: [{}/{}] {}\n",
                   r.candidate_path.string(),
                   souxmar::plugin::to_string(r.code),
                   souxmar::plugin::to_string(*r.manifest_code),
                   r.reason);
      } else {
        fmt::print(stderr, "  - {}: [{}] {}\n",
                   r.candidate_path.string(),
                   souxmar::plugin::to_string(r.code),
                   r.reason);
      }
    }
  }
  return kExitOk;
}

// Locate the canonical plugin index. Priority order:
//   1. --index <path> (caller-supplied, absolute or relative to cwd).
//   2. $SOUXMAR_PLUGIN_INDEX environment variable.
//   3. ./docs/plugin-index.toml relative to cwd (works in a checkout).
//   4. <executable parent>/../share/souxmar/plugin-index.toml (Sprint 10
//      install-side hardening; not used in dev).
// Returns the first existing path; an empty fs::path if none found.
fs::path resolve_index_path(const fs::path& override_path) {
  if (!override_path.empty()) return override_path;
  if (const char* env = std::getenv("SOUXMAR_PLUGIN_INDEX"); env && *env) {
    return fs::path{env};
  }
  fs::path cwd_local = fs::current_path() / "docs" / "plugin-index.toml";
  if (fs::exists(cwd_local)) return cwd_local;
  return {};
}

int cmd_plugin_search(const std::string&  query,
                      const std::string&  capability_prefix,
                      const fs::path&     index_override) {
  const fs::path index_path = resolve_index_path(index_override);
  if (index_path.empty() || !fs::exists(index_path)) {
    fmt::print(stderr,
        "error: plugin index not found. Set $SOUXMAR_PLUGIN_INDEX or pass "
        "--index <path>, or run from a souxmar checkout containing "
        "docs/plugin-index.toml.\n");
    return kExitUsage;
  }
  auto result = souxmar::plugin::load_index_file(index_path);
  if (auto* err = std::get_if<souxmar::plugin::IndexParseError>(&result)) {
    fmt::print(stderr, "error: failed to parse index: {}\n", err->message);
    return kExitInternal;
  }
  const auto& entries = std::get<std::vector<souxmar::plugin::IndexEntry>>(result);
  const auto matches  = souxmar::plugin::search_index(entries, query, capability_prefix);
  if (matches.empty()) {
    if (!query.empty() || !capability_prefix.empty()) {
      fmt::print(stderr, "no plugins matched (query='{}', capability='{}')\n",
                 query, capability_prefix);
    } else {
      fmt::print(stderr, "no plugins listed in {}\n", index_path.string());
    }
    return kExitOk;
  }
  for (const auto& e : matches) {
    fmt::print("{} — {}\n", e.id, e.name);
    if (!e.description.empty()) {
      fmt::print("  description:  {}\n", e.description);
    }
    fmt::print("  capabilities:");
    for (const auto& c : e.capabilities) fmt::print(" {}", c);
    fmt::print("\n");
    if (!e.license.empty())          fmt::print("  license:      {}\n", e.license);
    if (!e.author.empty())           fmt::print("  author:       {}\n", e.author);
    if (!e.source.empty())           fmt::print("  source:       {}\n", e.source);
    if (!e.souxmar_versions.empty()) fmt::print("  souxmar:      {}\n", e.souxmar_versions);
    fmt::print("  conformance:  {}", souxmar::plugin::to_string(e.conformance));
    if (!e.conformance_date.empty()) fmt::print(" ({})", e.conformance_date);
    fmt::print("\n");
    fmt::print("  status:       {}{}\n",
               souxmar::plugin::to_string(e.status),
               e.paid ? "  [paid]" : "");
    fmt::print("\n");
  }
  fmt::print("{} match(es) in {}\n", matches.size(), index_path.string());
  return kExitOk;
}

// Sprint 10 push 3 — structural validation of the plugin index. Used
// by CI on every PR touching `docs/plugin-index.toml` and by authors
// running locally before opening a PR. Exit codes: 0 if every check
// passes or only warnings fired; kExitInputData (10) on any
// error-severity issue. The split lets the CI workflow's "checks"
// status reflect "warnings are reviewable; errors block".
int cmd_plugin_validate(const fs::path& index_override) {
  const fs::path index_path = resolve_index_path(index_override);
  if (index_path.empty() || !fs::exists(index_path)) {
    fmt::print(stderr,
        "error: plugin index not found. Set $SOUXMAR_PLUGIN_INDEX or pass "
        "--index <path>, or run from a souxmar checkout containing "
        "docs/plugin-index.toml.\n");
    return kExitUsage;
  }
  auto result = souxmar::plugin::load_index_file(index_path);
  if (auto* err = std::get_if<souxmar::plugin::IndexParseError>(&result)) {
    fmt::print(stderr, "error: failed to parse index: {}\n", err->message);
    return kExitInputData;
  }
  const auto& entries = std::get<std::vector<souxmar::plugin::IndexEntry>>(result);
  const auto issues   = souxmar::plugin::validate_index(entries);

  std::size_t errors = 0, warnings = 0;
  for (const auto& iss : issues) {
    using S = souxmar::plugin::IndexIssueSeverity;
    auto& out = (iss.severity == S::Error) ? errors : warnings;
    ++out;
    // Format: "<severity>: entry #<n> (id=<id>) <field>: <message>"
    // — names the field + entry-id so a reviewer can grep the PR diff
    // for the offender without consulting line numbers. Errors go to
    // stderr so CI captures them in the failure log; warnings go to
    // stdout so PR-comment renderers can pick them up next to the
    // diff without escalating to the failure stream.
    const auto& entry = entries[iss.entry_index];
    auto* stream = (iss.severity == S::Error) ? stderr : stdout;
    fmt::print(stream,
        "{}: entry #{} (id={}){}{}: {}\n",
        souxmar::plugin::to_string(iss.severity),
        iss.entry_index,
        entry.id,
        iss.field.empty() ? "" : " ",
        iss.field,
        iss.message);
  }

  fmt::print("\nValidated {} entries: {} error(s), {} warning(s) in {}\n",
             entries.size(), errors, warnings, index_path.string());
  return errors == 0 ? kExitOk : kExitInputData;
}

// Print a single stage line in `souxmar run` output.
void print_stage_line(const souxmar::pipeline::StageRunResult& sr) {
  using Status = souxmar::pipeline::StageRunResult::Status;
  std::string_view tag;
  switch (sr.status) {
    case Status::Cached:   tag = "CACHED  "; break;
    case Status::Executed: tag = "OK      "; break;
    case Status::Failed:   tag = "FAILED  "; break;
    case Status::Skipped:  tag = "SKIPPED "; break;
  }
  fmt::print("  [{}] {}  hash={}\n", tag, sr.stage_id, sr.content_hash.hex());
  if (sr.status == Status::Failed && sr.error) {
    fmt::print(stderr, "      └─ {}\n", sr.error->message);
  }
}

int cmd_run(const fs::path&              pipeline_path,
            bool                         use_cache,
            const fs::path&              cache_dir_override,
            const std::vector<fs::path>& extra_paths) {
  if (!fs::exists(pipeline_path)) {
    fmt::print(stderr, "error: pipeline file not found: {}\n", pipeline_path.string());
    return kExitUsage;
  }

  // 1. Parse YAML up front so an obvious typo fails fast (before plugin load).
  auto parse_result = souxmar::pipeline::parse_pipeline_file(pipeline_path);
  if (auto* err = std::get_if<souxmar::pipeline::ParseError>(&parse_result)) {
    fmt::print(stderr, "parse error in {}: {}", pipeline_path.string(), err->message);
    if (err->line)   fmt::print(stderr, " (line {})", *err->line);
    if (err->column) fmt::print(stderr, ", column {}", *err->column);
    fmt::print(stderr, "\n");
    return kExitInputData;
  }
  const auto pipeline = std::get<souxmar::pipeline::Pipeline>(std::move(parse_result));

  // 2. Discover + load every available plugin. We load all of them so any
  //    pipeline that names a registered capability resolves; the registry
  //    dispatcher will surface a clear error if a capability is missing.
  souxmar::plugin::Registry     registry;
  souxmar::plugin::PluginLoader loader(registry, std::string{souxmar::version_string()});

  std::vector<souxmar::plugin::LoadedPlugin> live_plugins;
  const auto report = discover_with_overrides(extra_paths);
  live_plugins.reserve(report.loaded.size());
  for (const auto& d : report.loaded) {
    auto load_result = loader.load(d);
    if (auto* lerr = std::get_if<souxmar::plugin::LoadError>(&load_result)) {
      fmt::print(stderr, "warning: failed to load plugin {}: {}\n",
                 d.manifest.id, lerr->message);
      continue;
    }
    live_plugins.push_back(std::move(std::get<souxmar::plugin::LoadedPlugin>(load_result)));
  }
  for (const auto& r : report.rejected) {
    fmt::print(stderr, "warning: rejected {}: {}\n", r.candidate_path.string(), r.reason);
  }

  if (registry.size() == 0) {
    fmt::print(stderr,
        "error: no plugins registered any capabilities. Did you set "
        "$SOUXMAR_PLUGIN_PATH or pass --plugin-path?\n");
    return kExitInternal;
  }

  // 3. Run.
  fmt::print("Running pipeline {} ({} stages, {} capabilities available)\n",
             pipeline_path.filename().string(),
             pipeline.stages.size(),
             registry.size());

  souxmar::pipeline::RegistryDispatcher dispatcher(registry);
  souxmar::pipeline::Cache              cache;
  souxmar::pipeline::RunOptions         opts;
  opts.use_cache = use_cache;

  // Wire in the disk-backed cache so reruns of stages with serializable
  // output (writer.* today) skip re-dispatch even across processes. Failure
  // to construct the cache directory is non-fatal — we just stay in-memory.
  if (use_cache) {
    try {
      auto disk = std::make_shared<souxmar::pipeline::DiskCache>(
          souxmar::pipeline::DiskCache::default_dir(cache_dir_override));
      souxmar::pipeline::DiskBacking backing;
      backing.cache       = std::move(disk);
      backing.serialize   = &souxmar::pipeline::serialize_stage_output;
      backing.deserialize = &souxmar::pipeline::deserialize_stage_output;
      opts.disk_backing   = std::move(backing);
    } catch (const std::exception& e) {
      fmt::print(stderr, "warning: disk cache disabled ({})\n", e.what());
    }
  }

  const auto run = souxmar::pipeline::run_pipeline(pipeline, dispatcher, cache, opts);

  if (run.status == souxmar::pipeline::RunResult::Status::ValidationFailed) {
    fmt::print(stderr, "validation failed:\n");
    for (const auto& msg : run.validation_errors) {
      fmt::print(stderr, "  - {}\n", msg);
    }
    return kExitInputData;
  }

  for (const auto& sr : run.stage_results) print_stage_line(sr);

  if (run.status != souxmar::pipeline::RunResult::Status::Success) {
    fmt::print(stderr, "\npipeline failed.\n");
    return kExitInternal;
  }
  fmt::print("\npipeline ok ({} stages)\n", run.stage_results.size());
  return kExitOk;
}

// ---- Agent subcommands -------------------------------------------------

std::string_view confirmation_name(souxmar::ai::Confirmation c) {
  switch (c) {
    case souxmar::ai::Confirmation::Auto:          return "auto";
    case souxmar::ai::Confirmation::ConfirmOnce:   return "confirm-once";
    case souxmar::ai::Confirmation::ConfirmAlways: return "confirm-always";
  }
  return "?";
}

int cmd_agent_list() {
  const auto registry = souxmar::ai::default_v1_tools();
  fmt::print("souxmar agent tools (v1, {} total):\n\n", registry.size());
  for (const auto& name : registry.list()) {
    const auto* t = registry.find(name);
    fmt::print("  {} [{}]  ({})\n",
               t->name, t->category, confirmation_name(t->confirmation));
    if (!t->description.empty()) {
      fmt::print("    {}\n", t->description);
    }
    fmt::print("\n");
  }
  return kExitOk;
}

int cmd_agent_invoke(const std::string&            tool_name,
                     const std::string&            input_yaml,
                     bool                          auto_yes,
                     bool                          use_cache,
                     const fs::path&               cache_dir_override,
                     const fs::path&               audit_log_path,
                     const fs::path&               budget_config_path,
                     const std::vector<fs::path>&  extra_paths) {
  // 1. Parse the inputs first so an obvious typo fails before we touch
  //    plugins.
  souxmar::pipeline::Value inputs;
  if (!input_yaml.empty()) {
    try {
      inputs = souxmar::pipeline::parse_value_yaml(input_yaml);
    } catch (const std::exception& e) {
      fmt::print(stderr, "error: --input YAML did not parse: {}\n", e.what());
      return kExitInputData;
    }
  }

  // 2. Discover + load every plugin under the search path. mesh / solve
  //    need a populated registry; other tools tolerate an empty one.
  souxmar::plugin::Registry     registry;
  souxmar::plugin::PluginLoader loader(registry, std::string{souxmar::version_string()});

  std::vector<souxmar::plugin::LoadedPlugin> live_plugins;
  const auto report = discover_with_overrides(extra_paths);
  live_plugins.reserve(report.loaded.size());
  for (const auto& d : report.loaded) {
    auto load_result = loader.load(d);
    if (auto* lerr = std::get_if<souxmar::plugin::LoadError>(&load_result)) {
      fmt::print(stderr, "warning: failed to load plugin {}: {}\n",
                 d.manifest.id, lerr->message);
      continue;
    }
    live_plugins.push_back(std::move(std::get<souxmar::plugin::LoadedPlugin>(load_result)));
  }

  // 3. Build dispatch context.
  souxmar::pipeline::RegistryDispatcher dispatcher(registry);
  souxmar::pipeline::Cache              cache;
  souxmar::pipeline::Value session_state =
      souxmar::pipeline::Value::map({});

  souxmar::ai::ToolContext ctx;
  ctx.registry      = &registry;
  ctx.dispatcher    = &dispatcher;
  ctx.cache         = &cache;
  ctx.session_state = &session_state;
  (void)cache_dir_override;  // disk_cache only relevant to `run` today
  (void)use_cache;

  // Audit log. By default we write to the platform-relative
  // `.souxmar/chat/audit.log` (or whatever $SOUXMAR_AUDIT_LOG points
  // at); --audit-log overrides both. We construct the AuditLog
  // best-effort: a permission failure surfaces a warning but does not
  // block the tool from running.
  std::unique_ptr<souxmar::ai::AuditLog> audit;
  try {
    audit = std::make_unique<souxmar::ai::AuditLog>(
        souxmar::ai::AuditLog::default_path(audit_log_path));
    ctx.audit_log = audit.get();
  } catch (const std::exception& e) {
    fmt::print(stderr, "warning: audit log disabled ({})\n", e.what());
  }

  // Sprint 6 push 6 — per-project budget config. `--budget-config` is
  // explicit; otherwise we auto-load `.souxmar/budget.toml` from CWD if
  // present. A parse error logs a warning and runs unbudgeted.
  souxmar::ai::SessionBudget budget;
  bool budget_loaded = false;
  fs::path effective_budget_path = budget_config_path;
  if (effective_budget_path.empty()) {
    const auto auto_path = souxmar::ai::default_budget_config_path();
    if (fs::exists(auto_path)) effective_budget_path = auto_path;
  }
  if (!effective_budget_path.empty()) {
    auto r = souxmar::ai::parse_budget_config_file(effective_budget_path);
    if (auto* err = std::get_if<souxmar::ai::BudgetConfigError>(&r)) {
      fmt::print(stderr, "warning: budget config '{}' not loaded: {}\n",
                 effective_budget_path.string(), err->message);
    } else {
      const auto& cfg = std::get<souxmar::ai::BudgetConfig>(r);
      cfg.apply_to(budget);
      budget_loaded = true;
    }
  }
  if (budget_loaded) {
    ctx.budget = &budget;
    fmt::print(stderr,
        "budget: max_input={} max_output={} max_total={} ({})\n",
        budget.max_input_tokens, budget.max_output_tokens,
        budget.max_total_tokens, effective_budget_path.string());
  }

  // 4. Confirmation policy: --yes maps every tool to Auto for this run.
  //    Without it, a tool needing confirmation will hit the no-prompter
  //    path and fail with NOT_CONFIRMED — explicit and recoverable.
  souxmar::ai::ConfirmationPolicy policy;
  if (auto_yes) {
    for (const auto& name : souxmar::ai::default_v1_tools().list()) {
      policy.overrides[name] = souxmar::ai::Confirmation::Auto;
    }
  }

  // 5. Dispatch + print.
  const auto registry_v1 = souxmar::ai::default_v1_tools();
  const auto result = souxmar::ai::dispatch_tool(registry_v1, tool_name,
                                                 inputs, ctx, policy);
  if (result.error) {
    fmt::print(stderr, "{} [{}]\n", result.error->message, result.error->code);
    if (!result.error->suggestion.empty()) {
      fmt::print(stderr, "  → {}\n", result.error->suggestion);
    }
    return kExitInternal;
  }

  fmt::print("{}\n", result.summary);
  if (result.data.kind() != souxmar::pipeline::Value::Kind::Null) {
    fmt::print("---\n{}\n", souxmar::pipeline::emit_value_yaml(result.data));
  }
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
    print_usage();
    return args.empty() ? kExitUsage : kExitOk;
  }

  const std::string& sub = args[0];

  // Common: collect --plugin-path flags from anywhere in the tail.
  std::vector<fs::path>    extra_paths;
  fs::path                 cache_dir_override;
  fs::path                 audit_log_path;
  fs::path                 budget_config_path;
  fs::path                 index_path_override;     // --index <path>
  std::string              capability_prefix;       // --capability <prefix>
  bool                     use_cache = true;
  bool                     auto_yes  = false;
  std::string              input_yaml;
  std::vector<std::string> positionals;

  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--plugin-path") {
      auto v = pop_value(args, i, "--plugin-path");
      if (!v) return kExitUsage;
      extra_paths.emplace_back(*v);
    } else if (args[i] == "--no-cache") {
      use_cache = false;
    } else if (args[i] == "--cache-dir") {
      auto v = pop_value(args, i, "--cache-dir");
      if (!v) return kExitUsage;
      cache_dir_override = *v;
    } else if (args[i] == "--input") {
      auto v = pop_value(args, i, "--input");
      if (!v) return kExitUsage;
      input_yaml = *v;
    } else if (args[i] == "--input-file") {
      auto v = pop_value(args, i, "--input-file");
      if (!v) return kExitUsage;
      try {
        std::ifstream src(*v);
        if (!src.is_open()) {
          fmt::print(stderr, "error: cannot open --input-file '{}'\n", *v);
          return kExitInputData;
        }
        std::ostringstream buf;
        buf << src.rdbuf();
        input_yaml = buf.str();
      } catch (const std::exception& e) {
        fmt::print(stderr, "error: reading --input-file failed: {}\n", e.what());
        return kExitInputData;
      }
    } else if (args[i] == "--yes" || args[i] == "-y") {
      auto_yes = true;
    } else if (args[i] == "--audit-log") {
      auto v = pop_value(args, i, "--audit-log");
      if (!v) return kExitUsage;
      audit_log_path = *v;
    } else if (args[i] == "--budget-config") {
      auto v = pop_value(args, i, "--budget-config");
      if (!v) return kExitUsage;
      budget_config_path = *v;
    } else if (args[i] == "--index") {
      auto v = pop_value(args, i, "--index");
      if (!v) return kExitUsage;
      index_path_override = *v;
    } else if (args[i] == "--capability") {
      auto v = pop_value(args, i, "--capability");
      if (!v) return kExitUsage;
      capability_prefix = *v;
    } else if (!args[i].empty() && args[i].front() == '-') {
      fmt::print(stderr, "error: unknown flag '{}'\n", args[i]);
      print_usage();
      return kExitUsage;
    } else {
      positionals.push_back(args[i]);
    }
  }

  if (sub == "version" || sub == "--version") {
    return cmd_version();
  }
  if (sub == "plugin") {
    if (positionals.empty()) {
      fmt::print(stderr,
          "error: `souxmar plugin` requires a sub-action (list | search)\n");
      return kExitUsage;
    }
    if (positionals[0] == "list") {
      return cmd_plugin_list(extra_paths);
    }
    if (positionals[0] == "search") {
      // Optional positional: the query string. Multiple positionals
      // (rare) are joined with spaces so `souxmar plugin search foo bar`
      // searches for the literal "foo bar" against the index — matches
      // how shells naturally tokenise a multi-word query.
      std::string query;
      for (std::size_t i = 1; i < positionals.size(); ++i) {
        if (i > 1) query += ' ';
        query += positionals[i];
      }
      return cmd_plugin_search(query, capability_prefix, index_path_override);
    }
    if (positionals[0] == "validate-index") {
      return cmd_plugin_validate(index_path_override);
    }
    fmt::print(stderr, "error: unknown plugin action '{}'\n", positionals[0]);
    return kExitUsage;
  }
  if (sub == "run") {
    if (positionals.empty()) {
      fmt::print(stderr, "error: `souxmar run` requires a pipeline file\n");
      print_usage();
      return kExitUsage;
    }
    return cmd_run(fs::path(positionals[0]), use_cache, cache_dir_override, extra_paths);
  }
  if (sub == "agent") {
    if (positionals.empty()) {
      fmt::print(stderr, "error: `souxmar agent` requires a sub-action (list | invoke)\n");
      return kExitUsage;
    }
    if (positionals[0] == "list") {
      return cmd_agent_list();
    }
    if (positionals[0] == "invoke") {
      if (positionals.size() < 2) {
        fmt::print(stderr, "error: `souxmar agent invoke` requires a tool name\n");
        return kExitUsage;
      }
      return cmd_agent_invoke(positionals[1], input_yaml, auto_yes,
                              use_cache, cache_dir_override,
                              audit_log_path, budget_config_path,
                              extra_paths);
    }
    fmt::print(stderr, "error: unknown agent action '{}'\n", positionals[0]);
    return kExitUsage;
  }

  fmt::print(stderr, "error: unknown subcommand '{}'\n", sub);
  print_usage();
  return kExitUsage;
}
