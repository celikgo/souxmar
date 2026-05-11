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
// Sprint 10 push 6 — `souxmar update check` / `apply` subcommands.
#include "souxmar/update/manifest.h"
#include "souxmar/update/state_machine.h"
#include "souxmar/update/update_state.h"
#include "souxmar/update/verifier.h"
#include "souxmar/version.h"

namespace fs = std::filesystem;

namespace {

constexpr int kExitOk        = 0;
constexpr int kExitUsage     = 64;
constexpr int kExitInputData = 65;
constexpr int kExitInternal  = 70;
// Sprint 10 push 6 — `souxmar update check` exit codes. The integration
// test branches on these; the rollback log (push 7) records them
// alongside the structured refusal reason. Picked from the 75–79 range
// to stay clear of sysexits.h's EX_NOPERM=77 / EX_CONFIG=78.
constexpr int kExitUpdateRefused  = 75;
constexpr int kExitSignatureBad   = 76;

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
      "  souxmar update check       --manifest <path> --signature <path>\n"
      "                             --trusted-key <id>=<hex> [--trusted-key <id>=<hex>]...\n"
      "                             [--current-version <ver>] [--state <path>]\n"
      "                             [--platform <os>/<arch>] [--as-of <rfc3339>] [--json]\n"
      "  souxmar update apply --dry-run  (same flags as `check`)\n"
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

// ---- Update subcommand --------------------------------------------------
//
// `souxmar update check` and `souxmar update apply --dry-run` share the
// same pre-flight: read the manifest file off disk, read the detached
// signature file, verify the signature against caller-supplied trusted
// keys, run the apply-gate state machine. The non-dry-run apply path
// (download + stage + swap) lands in Sprint 10 push 7 alongside the
// rollback log; until then `apply` without --dry-run prints a
// "not-yet-implemented" message and exits non-zero.
//
// Network is deliberately absent from this push — `--manifest` is a
// local path. The release pipeline produces the manifest + .sig pair;
// CI tests can synthesise both. Push 7 adds the HTTP fetcher that
// downloads `<channel>.toml` + `<channel>.toml.sig` from the CDN.

struct UpdateFlags {
  fs::path                                   manifest_path;
  fs::path                                   signature_path;
  fs::path                                   state_path_override;
  std::vector<std::pair<std::string, std::string>>  trusted_keys;  // (id, hex)
  std::string                                current_version_override;
  std::string                                platform_override;
  std::string                                as_of_override;
  bool                                       json    = false;
  bool                                       dry_run = false;
};

std::string read_file_to_string(const fs::path& p, std::string& err) {
  std::ifstream src(p, std::ios::binary);
  if (!src.is_open()) {
    err = "cannot open '" + p.string() + "'";
    return {};
  }
  std::ostringstream buf;
  buf << src.rdbuf();
  return buf.str();
}

// Strip ASCII whitespace at both ends. The signature file on disk is
// 128 hex chars typically followed by '\n'; we strip leading + trailing
// whitespace so a CRLF / extra newline doesn't reject an otherwise-
// valid sig.
std::string strip_ascii_ws(std::string_view s) {
  std::size_t b = 0;
  while (b < s.size() &&
         (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r'))
    ++b;
  std::size_t e = s.size();
  while (e > b &&
         (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r'))
    --e;
  return std::string(s.substr(b, e - b));
}

// Splits a "<id>=<hex>" CLI argument into its two halves. Empty id or
// missing '=' returns false; the caller surfaces a usage error.
bool parse_trusted_key_flag(std::string_view              raw,
                            std::pair<std::string, std::string>& out) {
  const auto eq = raw.find('=');
  if (eq == std::string_view::npos) return false;
  out.first  = std::string(raw.substr(0, eq));
  out.second = std::string(raw.substr(eq + 1));
  return !out.first.empty() && !out.second.empty();
}

// Run the verify + apply-gate pre-flight. Returns the gate's decision
// plus a SignatureStatus that the caller folds into the exit code.
// Diagnostic strings flow through `out_diagnostics` so --json mode can
// suppress them.
struct UpdatePreflightResult {
  bool                                  valid = false;  // got far enough to call the gate
  souxmar::update::SignatureStatus      sig_status =
      souxmar::update::SignatureStatus::Ok;
  std::optional<souxmar::update::UpdateDecision> decision;
};

int cmd_update_check(const UpdateFlags& f) {
  using namespace souxmar::update;

  // 1. Mandatory flags.
  if (f.manifest_path.empty() || f.signature_path.empty() ||
      f.trusted_keys.empty()) {
    fmt::print(stderr,
        "error: `souxmar update check` requires --manifest, --signature, "
        "and at least one --trusted-key <id>=<hex>\n");
    return kExitUsage;
  }

  // 2. Read the manifest + signature off disk. The verifier hashes the
  //    manifest bytes verbatim — no canonicalisation, no preprocessing.
  std::string err;
  const std::string manifest_text = read_file_to_string(f.manifest_path, err);
  if (!err.empty()) {
    fmt::print(stderr, "error: {}\n", err);
    return kExitInputData;
  }
  const std::string signature_text = read_file_to_string(f.signature_path, err);
  if (!err.empty()) {
    fmt::print(stderr, "error: {}\n", err);
    return kExitInputData;
  }
  const std::string sig_hex = strip_ascii_ws(signature_text);
  std::vector<std::uint8_t> signature_bytes;
  if (!hex_decode(sig_hex, signature_bytes)) {
    fmt::print(stderr,
        "error: signature file '{}' is not valid lowercase hex\n",
        f.signature_path.string());
    return kExitInputData;
  }

  // 3. Parse manifest.
  auto parse_result = parse_manifest_string(manifest_text);
  if (auto* perr = std::get_if<ManifestParseError>(&parse_result)) {
    fmt::print(stderr, "error: manifest parse failed: {}\n", perr->message);
    return kExitInputData;
  }
  const auto& manifest = std::get<Manifest>(parse_result);

  // 4. Structural validate. Errors hard-fail; warnings print to stderr
  //    so a reviewer can see them but the gate still runs.
  const auto issues = validate_manifest(manifest);
  bool any_error = false;
  for (const auto& i : issues) {
    if (i.severity == ManifestIssueSeverity::Error) {
      any_error = true;
      fmt::print(stderr, "manifest error: {}: {}\n", i.field, i.message);
    } else {
      fmt::print(stderr, "manifest warning: {}: {}\n", i.field, i.message);
    }
  }
  if (any_error) return kExitInputData;

  // 5. Build trust store from CLI flags.
  TrustStore trust;
  for (const auto& [id, hex] : f.trusted_keys) {
    if (!trust.add_hex(id, hex)) {
      fmt::print(stderr,
          "error: --trusted-key {}=... rejected (id empty or hex pubkey "
          "is not 32 bytes / not lowercase hex)\n", id);
      return kExitUsage;
    }
  }

  // 6. Verify.
  const std::span<const std::uint8_t> message_span{
      reinterpret_cast<const std::uint8_t*>(manifest_text.data()),
      manifest_text.size()};
  const auto sig_status = verify_manifest_signature(
      message_span, signature_bytes, manifest.signing.public_key_id, trust);
  if (sig_status != SignatureStatus::Ok) {
    if (f.json) {
      fmt::print("{{\"status\":\"signature-failed\",\"reason\":\"{}\"}}\n",
                 to_string(sig_status));
    } else {
      fmt::print(stderr,
          "signature verification failed: {} (manifest signed by "
          "public_key_id={})\n",
          to_string(sig_status), manifest.signing.public_key_id);
    }
    return kExitSignatureBad;
  }

  // 7. Build the CurrentInstall snapshot. Order of precedence for
  //    `current_version`: --current-version flag > state file >
  //    running CLI's version.
  std::string current_version =
      !f.current_version_override.empty()
          ? f.current_version_override
          : std::string(souxmar::version_string());
  std::string max_seen;
  const fs::path state_path = f.state_path_override.empty()
      ? default_update_state_path()
      : f.state_path_override;
  {
    auto loaded = load_update_state(state_path);
    if (auto* lerr = std::get_if<UpdateStateLoadError>(&loaded)) {
      fmt::print(stderr,
          "warning: update-state file '{}' could not be read ({}); "
          "treating as fresh install\n",
          state_path.string(), lerr->message);
    } else {
      const auto& s = std::get<UpdateState>(loaded);
      if (!s.current_installed_version.empty() &&
          f.current_version_override.empty()) {
        current_version = s.current_installed_version;
      }
      max_seen = s.max_version_ever_seen;
    }
  }

  HostPlatform host = detect_host_platform();
  if (!f.platform_override.empty()) {
    auto p = parse_host_platform(f.platform_override);
    if (!p) {
      fmt::print(stderr,
          "error: --platform '{}' is not <os>/<arch> "
          "(os in linux|macos|windows; arch in x86_64|aarch64)\n",
          f.platform_override);
      return kExitUsage;
    }
    host = *p;
  }

  // 8. Build the time source.
  std::unique_ptr<TimeSource> clock;
  if (!f.as_of_override.empty()) {
    auto tp = parse_rfc3339_utc(f.as_of_override);
    if (!tp) {
      fmt::print(stderr,
          "error: --as-of '{}' is not canonical RFC-3339 UTC "
          "(YYYY-MM-DDTHH:MM:SSZ)\n", f.as_of_override);
      return kExitUsage;
    }
    clock = std::make_unique<FixedTimeSource>(*tp);
  } else {
    clock = std::make_unique<SystemTimeSource>();
  }

  // 9. Run the gate.
  CurrentInstall install{current_version, max_seen, host};
  const auto decision = apply_gate(manifest, install, *clock);

  // 10. Render.
  if (auto* apply = std::get_if<UpdateApply>(&decision)) {
    if (f.json) {
      fmt::print(
          "{{\"status\":\"apply\",\"version\":\"{}\",\"url\":\"{}\","
          "\"sha256\":\"{}\",\"size\":{},\"os\":\"{}\",\"arch\":\"{}\","
          "\"mandatory\":{}}}\n",
          apply->version, apply->artifact.url, apply->artifact.sha256,
          apply->artifact.size, to_string(apply->artifact.os),
          to_string(apply->artifact.arch),
          apply->mandatory ? "true" : "false");
    } else {
      fmt::print("update available: {} -> {}\n",
                 current_version.empty() ? "(fresh install)" : current_version,
                 apply->version);
      fmt::print("  url:       {}\n", apply->artifact.url);
      fmt::print("  sha256:    {}\n", apply->artifact.sha256);
      fmt::print("  size:      {} bytes\n", apply->artifact.size);
      fmt::print("  platform:  {}/{}\n",
                 to_string(apply->artifact.os),
                 to_string(apply->artifact.arch));
      fmt::print("  mandatory: {}\n", apply->mandatory ? "yes" : "no");
    }
    return kExitOk;
  }

  const auto& refusal = std::get<UpdateRefusal>(decision);
  if (refusal.reason == RefusalReason::AlreadyOnOrAheadOfOffered) {
    if (f.json) {
      fmt::print("{{\"status\":\"up-to-date\",\"version\":\"{}\"}}\n",
                 current_version);
    } else {
      fmt::print("already up-to-date ({})\n", refusal.detail);
    }
    return kExitOk;
  }
  if (f.json) {
    fmt::print("{{\"status\":\"refused\",\"reason\":\"{}\",\"detail\":\"{}\"}}\n",
               to_string(refusal.reason), refusal.detail);
  } else {
    fmt::print(stderr, "update refused: {} ({})\n",
               to_string(refusal.reason), refusal.detail);
  }
  return kExitUpdateRefused;
}

int cmd_update_apply(const UpdateFlags& f) {
  if (!f.dry_run) {
    fmt::print(stderr,
        "error: `souxmar update apply` requires --dry-run in this "
        "build. The download + atomic-swap path lands in Sprint 10 "
        "push 7 alongside the rollback log; until then, --dry-run "
        "runs the same pre-flight as `update check`.\n");
    return kExitUsage;
  }
  // The dry-run path is identical to check today. Once push 7 lands,
  // this branch grows the download + stage + swap + audit log calls;
  // the dry-run flag keeps the current behaviour as the verifiable
  // "what *would* happen?" diagnostic.
  return cmd_update_check(f);
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
  // Sprint 10 push 6 — `souxmar update` flags. Hoisted into the shared
  // parse loop so the order of flags doesn't depend on the position
  // of the subcommand keyword.
  UpdateFlags              update_flags;

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
    } else if (args[i] == "--manifest") {
      auto v = pop_value(args, i, "--manifest");
      if (!v) return kExitUsage;
      update_flags.manifest_path = *v;
    } else if (args[i] == "--signature") {
      auto v = pop_value(args, i, "--signature");
      if (!v) return kExitUsage;
      update_flags.signature_path = *v;
    } else if (args[i] == "--trusted-key") {
      auto v = pop_value(args, i, "--trusted-key");
      if (!v) return kExitUsage;
      std::pair<std::string, std::string> kv;
      if (!parse_trusted_key_flag(*v, kv)) {
        fmt::print(stderr,
            "error: --trusted-key must be <id>=<hex> (got '{}')\n", *v);
        return kExitUsage;
      }
      update_flags.trusted_keys.push_back(std::move(kv));
    } else if (args[i] == "--current-version") {
      auto v = pop_value(args, i, "--current-version");
      if (!v) return kExitUsage;
      update_flags.current_version_override = *v;
    } else if (args[i] == "--state") {
      auto v = pop_value(args, i, "--state");
      if (!v) return kExitUsage;
      update_flags.state_path_override = *v;
    } else if (args[i] == "--platform") {
      auto v = pop_value(args, i, "--platform");
      if (!v) return kExitUsage;
      update_flags.platform_override = *v;
    } else if (args[i] == "--as-of") {
      auto v = pop_value(args, i, "--as-of");
      if (!v) return kExitUsage;
      update_flags.as_of_override = *v;
    } else if (args[i] == "--json") {
      update_flags.json = true;
    } else if (args[i] == "--dry-run") {
      update_flags.dry_run = true;
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
  if (sub == "update") {
    if (positionals.empty()) {
      fmt::print(stderr,
          "error: `souxmar update` requires a sub-action "
          "(check | apply)\n");
      return kExitUsage;
    }
    if (positionals[0] == "check") {
      return cmd_update_check(update_flags);
    }
    if (positionals[0] == "apply") {
      return cmd_update_apply(update_flags);
    }
    fmt::print(stderr, "error: unknown update action '{}'\n",
               positionals[0]);
    return kExitUsage;
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
