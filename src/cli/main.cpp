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
// Push 7 adds install_layout / rollback_log / apply / rollback.
// Push 8 adds the embedded release trust store.
#include "souxmar/update/apply.h"
#include "souxmar/update/embedded_trust.h"
#include "souxmar/update/fetcher.h"
#include "souxmar/update/install_layout.h"
#include "souxmar/update/manifest.h"
#include "souxmar/update/rollback_log.h"
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
      "  souxmar plugin install <id> [--license <sxm_lic_...>] [--yes] [--json]\n"
      "  souxmar agent list [--json]\n"
      "  souxmar agent invoke <tool> [--input <yaml>] [--input-file <path>] [--yes]\n"
      "                              [--audit-log <path>] [--budget-config <path>]\n"
      "                              [--plugin-path <dir>]...\n"
      "  souxmar update check       --manifest <path> --signature <path>\n"
      "                             --trusted-key <id>=<hex> [--trusted-key <id>=<hex>]...\n"
      "                             [--current-version <ver>] [--state <path>]\n"
      "                             [--platform <os>/<arch>] [--as-of <rfc3339>] [--json]\n"
      "  souxmar update apply       (same flags as `check`)\n"
      "                             --artifact <path> --target-root <dir> [--dry-run]\n"
      "  souxmar update rollback    --target-root <dir> [--state <path>] [--json]\n"
      "  souxmar update fetch       --manifest-url <https-url> [--out-dir <dir>] [--insecure]\n"
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

// Sprint 16 push 4 — `souxmar plugin install` ratchets the CLI side
// of the marketplace plumbing (ADR-0023). The actual download +
// extract loop is queued for Sprint 17 alongside the desktop's
// shell-out per ADR-0022; this command's surface is the contract
// the desktop client polls against.
int cmd_plugin_install(const std::string& plugin_id,
                        const std::string& license_key,
                        bool               auto_yes,
                        bool               json_output,
                        const fs::path&    index_override) {
  const fs::path index_path = resolve_index_path(index_override);
  if (index_path.empty() || !fs::exists(index_path)) {
    fmt::print(stderr,
        "error: plugin index not found. Set $SOUXMAR_PLUGIN_INDEX or pass "
        "--index <path>.\n");
    return kExitUsage;
  }
  auto result = souxmar::plugin::load_index_file(index_path);
  if (auto* err = std::get_if<souxmar::plugin::IndexParseError>(&result)) {
    fmt::print(stderr, "error: failed to parse index: {}\n", err->message);
    return kExitInternal;
  }
  const auto& entries = std::get<std::vector<souxmar::plugin::IndexEntry>>(result);

  const souxmar::plugin::IndexEntry* hit = nullptr;
  for (const auto& e : entries) {
    if (e.id == plugin_id) { hit = &e; break; }
  }
  if (!hit) {
    if (json_output) {
      fmt::print(
          "{{\"status\":\"error\",\"code\":\"not_found\",\"id\":\"{}\"}}\n",
          plugin_id);
    } else {
      fmt::print(stderr, "error: plugin id '{}' not in index\n", plugin_id);
    }
    return kExitInputData;
  }

  // ADR-0023: paid plugins require a license key. Without one
  // we refuse; the desktop's shell-out per ADR-0022 supplies
  // `--license <sxm_lic_...>` after prompting the user.
  if (hit->paid && license_key.empty()) {
    if (json_output) {
      fmt::print(
          "{{\"status\":\"error\",\"code\":\"license_required\","
          "\"id\":\"{}\",\"detail\":\"plugin is paid; pass --license <sxm_lic_...>\"}}\n",
          plugin_id);
    } else {
      fmt::print(stderr,
          "error: '{}' is a paid plugin; pass --license <sxm_lic_...>\n",
          plugin_id);
    }
    return kExitInputData;
  }
  if (!license_key.empty() && license_key.find("sxm_lic_") != 0) {
    if (json_output) {
      fmt::print(
          "{{\"status\":\"error\",\"code\":\"license_malformed\","
          "\"id\":\"{}\"}}\n",
          plugin_id);
    } else {
      fmt::print(stderr,
          "error: license key must start with 'sxm_lic_'\n");
    }
    return kExitInputData;
  }

  // Sprint 16 push 4 — the actual install body (license check
  // against marketplace.souxmar.dev + fetch + verify + extract)
  // is queued for Sprint 17 push 2 when the marketplace service
  // returns real responses instead of 503. The CLI surface
  // already names the contract; the desktop client (per ADR-0022)
  // shells out + polls the install-status read FFI for
  // completion. Reporting honestly here.
  if (json_output) {
    fmt::print(
        "{{\"status\":\"not_yet_wired\","
        "\"code\":\"sprint_17_pending\","
        "\"id\":\"{}\",\"version\":\"{}\","
        "\"paid\":{},\"license_supplied\":{},"
        "\"detail\":\"CLI surface in place; marketplace download + verify "
        "loop lands in Sprint 17.\"}}\n",
        hit->id, hit->souxmar_versions,
        hit->paid ? "true" : "false",
        license_key.empty() ? "false" : "true");
  } else {
    fmt::print(
        "souxmar plugin install: {}\n"
        "  paid:             {}\n"
        "  license supplied: {}\n"
        "  status:           not yet wired\n"
        "\n"
        "The CLI surface ratchets in Sprint 16 push 4 (this build); the\n"
        "marketplace download + signature-verify + extract loop lands in\n"
        "Sprint 17 push 2. The desktop's shell-out per ADR-0022 polls the\n"
        "install-status read FFI for completion.\n",
        hit->id,
        hit->paid ? "yes" : "no",
        license_key.empty() ? "no" : "yes");
  }
  (void)auto_yes;  // honoured once the actual install body lands
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

// Sprint 13 push 2 — escape a string for JSON. Same scope as
// the encoder in src/ai/provider.cpp (the existing Ollama path
// hand-rolls its own); kept local because the dependency direction
// (cli → ai → provider) would otherwise drag the provider into the
// CLI link line.
std::string json_escape(std::string_view in) {
  std::string out;
  out.reserve(in.size() + 2);
  for (char c : in) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
          out += c;
        }
    }
  }
  return out;
}

int cmd_agent_list(bool json_output) {
  const auto registry = souxmar::ai::default_v1_tools();

  if (json_output) {
    // Sprint 13 push 2 — schema=1 emit. The shape is consumed by
    // scripts/docs-site/gen-agent-tools.py; documented in
    // docs-site/agents/tools.md's header. Tools listed sorted by
    // name (registry.list() already sorts) so output is
    // byte-deterministic across runs.
    fmt::print("{{\n  \"schema\": 1,\n");
    fmt::print("  \"contract_version\": \"v1\",\n");
    fmt::print("  \"tool_count\": {},\n", registry.size());
    fmt::print("  \"tools\": [\n");
    const auto names = registry.list();
    for (std::size_t i = 0; i < names.size(); ++i) {
      const auto* t = registry.find(names[i]);
      const char* comma = (i + 1 == names.size()) ? "" : ",";
      fmt::print("    {{\n");
      fmt::print("      \"name\": \"{}\",\n",         json_escape(t->name));
      fmt::print("      \"category\": \"{}\",\n",     json_escape(t->category));
      fmt::print("      \"confirmation\": \"{}\",\n", confirmation_name(t->confirmation));
      fmt::print("      \"description\": \"{}\"\n",   json_escape(t->description));
      fmt::print("    }}{}\n", comma);
    }
    fmt::print("  ]\n}}\n");
    return kExitOk;
  }

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
  // Sprint 10 push 7 — apply / rollback specific.
  fs::path                                   artifact_path;
  fs::path                                   target_root;
  // Sprint 11 push 2 — fetch specific.
  std::string                                manifest_url;
  fs::path                                   out_dir;
  bool                                       insecure = false;
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

// Shared pre-flight for `check`, `apply`, and `apply --dry-run`: read
// the manifest + signature off disk, parse + validate, build the
// trust store, verify the signature, load the per-user state file,
// resolve the host platform, build a time source. Returns either an
// exit code (on failure) or a populated UpdatePreflight that the
// caller turns into a gate decision / apply call.
struct UpdatePreflight {
  // Inputs preserved so the caller can also pass them to apply_update.
  std::string                        manifest_text;
  std::vector<std::uint8_t>          signature_bytes;
  souxmar::update::Manifest          manifest;
  souxmar::update::TrustStore        trust;
  // Resolved state.
  souxmar::update::UpdateState       state;        // loaded; "" fields on fresh install
  fs::path                           state_path;
  std::string                        current_version;
  souxmar::update::HostPlatform      host{};
  std::unique_ptr<souxmar::update::TimeSource> clock;
};

// Returns 0 on success and writes into *out; returns a non-zero exit
// code on failure (already-logged-via-stderr).
int update_preflight(const UpdateFlags& f, UpdatePreflight* out) {
  using namespace souxmar::update;

  if (f.manifest_path.empty() || f.signature_path.empty()) {
    fmt::print(stderr,
        "error: this `souxmar update` subcommand requires --manifest "
        "and --signature; pass --trusted-key <id>=<hex> to override "
        "the embedded release trust store\n");
    return kExitUsage;
  }

  std::string err;
  out->manifest_text = read_file_to_string(f.manifest_path, err);
  if (!err.empty()) {
    fmt::print(stderr, "error: {}\n", err);
    return kExitInputData;
  }
  const std::string signature_text =
      read_file_to_string(f.signature_path, err);
  if (!err.empty()) {
    fmt::print(stderr, "error: {}\n", err);
    return kExitInputData;
  }
  const std::string sig_hex = strip_ascii_ws(signature_text);
  if (!hex_decode(sig_hex, out->signature_bytes)) {
    fmt::print(stderr,
        "error: signature file '{}' is not valid lowercase hex\n",
        f.signature_path.string());
    return kExitInputData;
  }

  auto parse_result = parse_manifest_string(out->manifest_text);
  if (auto* perr = std::get_if<ManifestParseError>(&parse_result)) {
    fmt::print(stderr, "error: manifest parse failed: {}\n", perr->message);
    return kExitInputData;
  }
  out->manifest = std::get<Manifest>(std::move(parse_result));

  bool any_error = false;
  for (const auto& i : validate_manifest(out->manifest)) {
    if (i.severity == ManifestIssueSeverity::Error) {
      any_error = true;
      fmt::print(stderr, "manifest error: {}: {}\n", i.field, i.message);
    } else {
      fmt::print(stderr, "manifest warning: {}: {}\n", i.field, i.message);
    }
  }
  if (any_error) return kExitInputData;

  if (f.trusted_keys.empty()) {
    // Fall back to the embedded release trust store. Dev / unsigned
    // builds carry the placeholder souxmar-dev-key; the release CI
    // pipeline injects the real release-2026 key at configure time
    // and asserts build_uses_dev_key() == false before publishing.
    out->trust = embedded_trust_store();
    if (build_uses_dev_key()) {
      fmt::print(stderr,
          "note: using the in-tree development trust key "
          "(souxmar-dev-key); pass --trusted-key to override or rebuild "
          "with -DSOUXMAR_RELEASE_PUBKEY_ID=... for a release-trust "
          "configuration\n");
    }
  } else {
    for (const auto& [id, hex] : f.trusted_keys) {
      if (!out->trust.add_hex(id, hex)) {
        fmt::print(stderr,
            "error: --trusted-key {}=... rejected (id empty or hex pubkey "
            "is not 32 bytes / not lowercase hex)\n", id);
        return kExitUsage;
      }
    }
  }

  const std::span<const std::uint8_t> message_span{
      reinterpret_cast<const std::uint8_t*>(out->manifest_text.data()),
      out->manifest_text.size()};
  const auto sig_status = verify_manifest_signature(
      message_span, out->signature_bytes,
      out->manifest.signing.public_key_id, out->trust);
  if (sig_status != SignatureStatus::Ok) {
    if (f.json) {
      fmt::print("{{\"status\":\"signature-failed\",\"reason\":\"{}\"}}\n",
                 to_string(sig_status));
    } else {
      fmt::print(stderr,
          "signature verification failed: {} (manifest signed by "
          "public_key_id={})\n",
          to_string(sig_status), out->manifest.signing.public_key_id);
    }
    return kExitSignatureBad;
  }

  // State + current_version. Precedence: --current-version flag > state
  // file > running CLI's version.
  out->current_version = !f.current_version_override.empty()
      ? f.current_version_override
      : std::string(souxmar::version_string());
  out->state_path = f.state_path_override.empty()
      ? default_update_state_path()
      : f.state_path_override;
  {
    auto loaded = load_update_state(out->state_path);
    if (auto* lerr = std::get_if<UpdateStateLoadError>(&loaded)) {
      fmt::print(stderr,
          "warning: update-state file '{}' could not be read ({}); "
          "treating as fresh install\n",
          out->state_path.string(), lerr->message);
    } else {
      out->state = std::get<UpdateState>(std::move(loaded));
      if (!out->state.current_installed_version.empty() &&
          f.current_version_override.empty()) {
        out->current_version = out->state.current_installed_version;
      }
    }
  }

  out->host = detect_host_platform();
  if (!f.platform_override.empty()) {
    auto p = parse_host_platform(f.platform_override);
    if (!p) {
      fmt::print(stderr,
          "error: --platform '{}' is not <os>/<arch> "
          "(os in linux|macos|windows; arch in x86_64|aarch64)\n",
          f.platform_override);
      return kExitUsage;
    }
    out->host = *p;
  }

  if (!f.as_of_override.empty()) {
    auto tp = parse_rfc3339_utc(f.as_of_override);
    if (!tp) {
      fmt::print(stderr,
          "error: --as-of '{}' is not canonical RFC-3339 UTC "
          "(YYYY-MM-DDTHH:MM:SSZ)\n", f.as_of_override);
      return kExitUsage;
    }
    out->clock = std::make_unique<FixedTimeSource>(*tp);
  } else {
    out->clock = std::make_unique<SystemTimeSource>();
  }
  return 0;
}

int cmd_update_check(const UpdateFlags& f) {
  using namespace souxmar::update;

  UpdatePreflight pf;
  if (const int rc = update_preflight(f, &pf); rc != 0) return rc;

  CurrentInstall install{pf.current_version,
                         pf.state.max_version_ever_seen,
                         pf.host};
  const auto decision = apply_gate(pf.manifest, install, *pf.clock);

  // Push 7 closes the replay-defence gap noted in push 6's commit: on
  // every successful verification + gate-Apply decision, bump
  // max_version_ever_seen so a subsequent replay of an older signed
  // manifest is refused. last_check_at is recorded unconditionally.
  pf.state.last_check_at = format_rfc3339_utc(pf.clock->now());
  if (auto* apply = std::get_if<UpdateApply>(&decision)) {
    if (pf.state.max_version_ever_seen.empty()) {
      pf.state.max_version_ever_seen = apply->version;
    } else {
      const auto cmp = compare_versions(apply->version,
                                        pf.state.max_version_ever_seen);
      if (cmp && *cmp > 0) {
        pf.state.max_version_ever_seen = apply->version;
      }
    }
  }
  if (!save_update_state(pf.state_path, pf.state)) {
    fmt::print(stderr,
        "warning: failed to persist update-state to '{}' — replay "
        "defence is degraded until the next successful write\n",
        pf.state_path.string());
  }

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
                 pf.current_version.empty() ? "(fresh install)"
                                            : pf.current_version,
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
                 pf.current_version);
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

// Read a file into bytes. Returns empty vector + sets err on failure.
std::vector<std::uint8_t> read_file_bytes(const fs::path& p,
                                          std::string&    err) {
  std::ifstream src(p, std::ios::binary);
  if (!src.is_open()) {
    err = "cannot open '" + p.string() + "'";
    return {};
  }
  std::ostringstream buf;
  buf << src.rdbuf();
  const auto s = buf.str();
  return {reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data() + s.size())};
}

int cmd_update_apply(const UpdateFlags& f) {
  using namespace souxmar::update;

  // --dry-run path: same as check.
  if (f.dry_run) return cmd_update_check(f);

  if (f.target_root.empty() || f.artifact_path.empty()) {
    fmt::print(stderr,
        "error: `souxmar update apply` (without --dry-run) requires "
        "--target-root <dir> and --artifact <path>; the artifact bytes "
        "are read from --artifact, verified against the manifest's "
        "sha256, staged under <target-root>/versions/<version>/, and "
        "the swap is recorded in <target-root>/rollback.log.\n");
    return kExitUsage;
  }

  UpdatePreflight pf;
  if (const int rc = update_preflight(f, &pf); rc != 0) return rc;

  std::string err;
  const auto artifact_bytes = read_file_bytes(f.artifact_path, err);
  if (!err.empty()) {
    fmt::print(stderr, "error: {}\n", err);
    return kExitInputData;
  }

  InstallLayout layout(f.target_root);
  ApplyContext  actx;
  actx.manifest       = &pf.manifest;
  actx.artifact_bytes = artifact_bytes;
  actx.layout         = &layout;
  actx.state          = &pf.state;
  actx.clock          = pf.clock.get();
  actx.platform       = pf.host;
  actx.current_version = pf.current_version;

  const auto result = apply_update(actx);

  // Persist state regardless — apply_update mutates the in-memory
  // state object on success, and we want the bumps recorded even if
  // the on-disk write is the final step that fails (the next
  // operation will retry the rename via save_update_state's tmp+rename
  // path).
  if (!save_update_state(pf.state_path, pf.state)) {
    fmt::print(stderr,
        "warning: failed to persist update-state to '{}'\n",
        pf.state_path.string());
  }

  if (f.json) {
    fmt::print(
        "{{\"status\":\"{}\",\"version\":\"{}\",\"refusal\":\"{}\","
        "\"detail\":\"{}\"}}\n",
        to_string(result.outcome),
        result.applied_version,
        result.outcome == ApplyOutcome::RefusedByGate
            ? to_string(result.refusal) : "",
        result.detail);
  } else {
    fmt::print("apply: {} ({})\n",
               to_string(result.outcome), result.detail);
    if (!result.applied_version.empty()) {
      fmt::print("  version:  {}\n", result.applied_version);
      fmt::print("  root:     {}\n", layout.root().string());
    }
  }

  switch (result.outcome) {
    case ApplyOutcome::Applied:
    case ApplyOutcome::AppliedButLogWriteFailed:
      // Both are "the install moved forward" — exit zero so a
      // wrapping CI step doesn't bail. The log-write-failed path
      // surfaces a warning in the human-readable detail string.
      return kExitOk;
    case ApplyOutcome::RefusedByGate:
      return kExitUpdateRefused;
    case ApplyOutcome::ArtifactHashMismatch:
    case ApplyOutcome::ArtifactSizeMismatch:
      // The signature checked, the gate passed, but the artifact
      // bytes don't match the manifest's hash/size declaration. That
      // is "the input was wrong" (the artifact was fetched from a
      // mirror serving the wrong file, or the file was tampered with
      // post-download), not "the program crashed".
      return kExitInputData;
    case ApplyOutcome::StageFailed:
    case ApplyOutcome::SwitchFailed:
    default:
      return kExitInternal;
  }
}

// Sprint 11 push 2 — HTTPS fetch of the manifest + its detached
// signature. Lands them under <out-dir>/{manifest.toml,manifest.toml.sig}
// so the user can immediately follow with `souxmar update apply
// --manifest <out>/manifest.toml --signature <out>/manifest.toml.sig
// --artifact <separately-downloaded-bytes>`. The artifact bytes
// themselves are fetched lazily — `apply` reads `--artifact <path>`
// today, so the fetch flow stops at the manifest. A future push
// (Sprint 12+) can grow `souxmar update fetch --include-artifact`
// once the apply state machine drives the artifact URL discovery
// itself.
int cmd_update_fetch(const UpdateFlags& f) {
  using namespace souxmar::update;
  if (f.manifest_url.empty()) {
    fmt::print(stderr,
        "error: `souxmar update fetch` requires --manifest-url <https-url>\n");
    return kExitUsage;
  }
  const fs::path out_dir = f.out_dir.empty() ? fs::current_path() : f.out_dir;
  std::error_code ec;
  fs::create_directories(out_dir, ec);

  FetcherOptions opts;
  opts.require_https = !f.insecure;

  const auto man_result = fetch_to_memory(f.manifest_url, opts);
  if (auto* err = std::get_if<FetchError>(&man_result)) {
    fmt::print(stderr, "fetch manifest failed: {} ({})\n",
               to_string(err->kind), err->message);
    return kExitInputData;
  }
  const auto& man = std::get<FetchedBytes>(man_result);
  const fs::path man_path = out_dir / "manifest.toml";
  {
    std::ofstream sink(man_path, std::ios::binary | std::ios::trunc);
    sink.write(reinterpret_cast<const char*>(man.bytes.data()),
               static_cast<std::streamsize>(man.bytes.size()));
  }
  fmt::print("fetched manifest: {} ({} bytes, {}ms)\n",
             man_path.string(), man.bytes.size(), man.duration.count());

  const auto sig_result = fetch_to_memory(f.manifest_url + ".sig", opts);
  if (auto* err = std::get_if<FetchError>(&sig_result)) {
    fmt::print(stderr, "fetch signature failed: {} ({})\n",
               to_string(err->kind), err->message);
    return kExitInputData;
  }
  const auto& sig = std::get<FetchedBytes>(sig_result);
  const fs::path sig_path = out_dir / "manifest.toml.sig";
  {
    std::ofstream sink(sig_path, std::ios::binary | std::ios::trunc);
    sink.write(reinterpret_cast<const char*>(sig.bytes.data()),
               static_cast<std::streamsize>(sig.bytes.size()));
  }
  fmt::print("fetched signature: {} ({} bytes, {}ms)\n",
             sig_path.string(), sig.bytes.size(), sig.duration.count());

  // Print the suggested follow-up so the user has a copy-paste path
  // from `fetch` to `apply`. The artifact URL lives in the
  // manifest; we don't parse it here (parsing happens during
  // apply / check), but the user knows what to do.
  fmt::print("next: souxmar update check "
             "--manifest {} --signature {}\n",
             man_path.string(), sig_path.string());
  return kExitOk;
}

int cmd_update_rollback(const UpdateFlags& f) {
  using namespace souxmar::update;

  if (f.target_root.empty()) {
    fmt::print(stderr,
        "error: `souxmar update rollback` requires --target-root <dir>\n");
    return kExitUsage;
  }

  // Rollback doesn't need a manifest / signature / trust store — the
  // previous version's bytes are already on disk and were verified
  // when they were originally applied. We still load the per-user
  // state so current_installed_version + last_apply_at get bumped.
  const fs::path state_path = f.state_path_override.empty()
      ? default_update_state_path()
      : f.state_path_override;
  UpdateState state;
  {
    auto loaded = load_update_state(state_path);
    if (auto* lerr = std::get_if<UpdateStateLoadError>(&loaded)) {
      fmt::print(stderr,
          "warning: update-state file '{}' could not be read ({}); "
          "rollback proceeds against on-disk install layout only\n",
          state_path.string(), lerr->message);
    } else {
      state = std::get<UpdateState>(std::move(loaded));
    }
  }

  std::unique_ptr<TimeSource> clock;
  if (!f.as_of_override.empty()) {
    auto tp = parse_rfc3339_utc(f.as_of_override);
    if (!tp) {
      fmt::print(stderr,
          "error: --as-of '{}' is not canonical RFC-3339 UTC\n",
          f.as_of_override);
      return kExitUsage;
    }
    clock = std::make_unique<FixedTimeSource>(*tp);
  } else {
    clock = std::make_unique<SystemTimeSource>();
  }

  InstallLayout layout(f.target_root);
  RollbackContext rctx{&layout, &state, clock.get()};
  const auto result = rollback(rctx);

  if (!save_update_state(state_path, state)) {
    fmt::print(stderr,
        "warning: failed to persist update-state to '{}'\n",
        state_path.string());
  }

  if (f.json) {
    fmt::print(
        "{{\"status\":\"{}\",\"from\":\"{}\",\"to\":\"{}\","
        "\"detail\":\"{}\"}}\n",
        to_string(result.outcome), result.from_version,
        result.to_version, result.detail);
  } else {
    fmt::print("rollback: {} ({})\n",
               to_string(result.outcome), result.detail);
    if (!result.to_version.empty()) {
      fmt::print("  from: {}\n", result.from_version);
      fmt::print("  to:   {}\n", result.to_version);
    }
  }

  switch (result.outcome) {
    case RollbackOutcome::RolledBack:
    case RollbackOutcome::RolledBackButLogWriteFailed:
      return kExitOk;
    case RollbackOutcome::NoCurrentInstall:
    case RollbackOutcome::NoRollbackTarget:
    case RollbackOutcome::TargetPayloadMissing:
      return kExitUpdateRefused;
    default:
      return kExitInternal;
  }
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
  // Sprint 16 push 4 — `souxmar plugin install` flags.
  std::string              plugin_install_id;       // --id <plugin-id>
  std::string              plugin_install_license;  // --license <sxm_lic_...>
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
    } else if (args[i] == "--id") {
      auto v = pop_value(args, i, "--id");
      if (!v) return kExitUsage;
      plugin_install_id = *v;
    } else if (args[i] == "--license") {
      auto v = pop_value(args, i, "--license");
      if (!v) return kExitUsage;
      plugin_install_license = *v;
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
    } else if (args[i] == "--artifact") {
      auto v = pop_value(args, i, "--artifact");
      if (!v) return kExitUsage;
      update_flags.artifact_path = *v;
    } else if (args[i] == "--target-root") {
      auto v = pop_value(args, i, "--target-root");
      if (!v) return kExitUsage;
      update_flags.target_root = *v;
    } else if (args[i] == "--manifest-url") {
      auto v = pop_value(args, i, "--manifest-url");
      if (!v) return kExitUsage;
      update_flags.manifest_url = *v;
    } else if (args[i] == "--out-dir") {
      auto v = pop_value(args, i, "--out-dir");
      if (!v) return kExitUsage;
      update_flags.out_dir = *v;
    } else if (args[i] == "--insecure") {
      update_flags.insecure = true;
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
    if (positionals[0] == "install") {
      // Sprint 16 push 4 — `--id` is required; positional plugin
      // id is accepted as a shorthand (`souxmar plugin install
      // <id>` is equivalent to `--id <id>`).
      std::string id = plugin_install_id;
      if (id.empty() && positionals.size() >= 2) id = positionals[1];
      if (id.empty()) {
        fmt::print(stderr,
            "error: `souxmar plugin install` requires --id <plugin-id> or a positional id\n");
        return kExitUsage;
      }
      return cmd_plugin_install(id, plugin_install_license,
                                auto_yes, update_flags.json,
                                index_path_override);
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
    if (positionals[0] == "rollback") {
      return cmd_update_rollback(update_flags);
    }
    if (positionals[0] == "fetch") {
      return cmd_update_fetch(update_flags);
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
      // Sprint 13 push 2 — reuse the shared --json flag bool
      // (already collected in the top-level parse loop). The
      // generator at scripts/docs-site/gen-agent-tools.py is the
      // first consumer.
      return cmd_agent_list(update_flags.json);
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
