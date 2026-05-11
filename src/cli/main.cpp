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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/discovery.h"
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
    fmt::print("  capabilities:\n");
    for (const auto& cap : d.manifest.capabilities) {
      fmt::print("    - {}\n", cap);
    }
    fmt::print("\n");
  }

  if (!report.rejected.empty()) {
    fmt::print(stderr, "{} plugin(s) rejected:\n", report.rejected.size());
    for (const auto& r : report.rejected) {
      fmt::print(stderr, "  - {}: {}\n", r.candidate_path.string(), r.reason);
    }
  }
  return kExitOk;
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

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
    print_usage();
    return args.empty() ? kExitUsage : kExitOk;
  }

  const std::string& sub = args[0];

  // Common: collect --plugin-path flags from anywhere in the tail.
  std::vector<fs::path> extra_paths;
  fs::path  cache_dir_override;
  bool      use_cache = true;
  fs::path  positional;

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
    } else if (!args[i].empty() && args[i].front() == '-') {
      fmt::print(stderr, "error: unknown flag '{}'\n", args[i]);
      print_usage();
      return kExitUsage;
    } else if (positional.empty()) {
      positional = args[i];
    } else {
      fmt::print(stderr, "error: unexpected positional argument '{}'\n", args[i]);
      return kExitUsage;
    }
  }

  if (sub == "version" || sub == "--version") {
    return cmd_version();
  }
  if (sub == "plugin") {
    if (positional.empty() || positional.string() != "list") {
      fmt::print(stderr, "error: only `souxmar plugin list` is supported in v0.0.1\n");
      return kExitUsage;
    }
    return cmd_plugin_list(extra_paths);
  }
  if (sub == "run") {
    if (positional.empty()) {
      fmt::print(stderr, "error: `souxmar run` requires a pipeline file\n");
      print_usage();
      return kExitUsage;
    }
    return cmd_run(positional, use_cache, cache_dir_override, extra_paths);
  }

  fmt::print(stderr, "error: unknown subcommand '{}'\n", sub);
  print_usage();
  return kExitUsage;
}
