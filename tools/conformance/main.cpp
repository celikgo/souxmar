// SPDX-License-Identifier: Apache-2.0
//
// souxmar-conformance — runs the v1 plugin conformance suite against
// every plugin discovered under a directory. Exits 0 iff every plugin
// passes every check.
//
// Usage:
//   souxmar-conformance <search-dir> [--plugin-id <id>] [--quiet] [--summary-only]
//
// Examples:
//   souxmar-conformance ./build/dev/examples/plugins
//   souxmar-conformance --plugin-id dev.souxmar.examples.hello-mesher ./plugins
//
// Exit codes:
//   0   every plugin passed every check
//   1   at least one plugin failed at least one check
//   2   usage error
//   3   no plugins discovered under the given directory

#include <fmt/core.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "souxmar/plugin/conformance.h"
#include "souxmar/plugin/discovery.h"

namespace fs = std::filesystem;
using namespace souxmar::plugin;

namespace {

constexpr int kExitOk        = 0;
constexpr int kExitFail      = 1;
constexpr int kExitUsage     = 2;
constexpr int kExitNoPlugins = 3;

void print_usage() {
  fmt::print(stderr,
      "souxmar-conformance — plugin conformance suite v1\n"
      "\n"
      "Usage:\n"
      "  souxmar-conformance <search-dir> [--plugin-id <id>] [--quiet] [--summary-only]\n"
      "\n"
      "Discovers every plugin under <search-dir> (its immediate subdirectories\n"
      "are scanned for souxmar-plugin.toml manifests) and runs all 10 v1\n"
      "conformance checks against each. Exits 0 iff every plugin passes\n"
      "every check.\n"
      "\n"
      "Flags:\n"
      "  --plugin-id <id>   Only run against the plugin with this manifest id.\n"
      "  --quiet            Suppress per-plugin tables; emit only the final tally.\n"
      "  --summary-only     Print one line per plugin (PASS/FAIL + count).\n");
}

void print_report_table(const ConformanceReport& r) {
  fmt::print("\nplugin: {}\n", r.plugin_id);
  fmt::print("  manifest: {}\n", r.manifest_path.string());
  fmt::print("  {:<8} {:<5}  {}\n", "check", "outcome", "detail");
  fmt::print("  {:-<8} {:-<5}  {:-<60}\n", "", "", "");
  for (const auto& cr : r.results) {
    fmt::print("  {:<8} {:<5}  {}\n",
               cr.check_id, to_string(cr.outcome), cr.detail);
  }
  fmt::print("  {} passed, {} failed, {} skipped\n",
             r.pass_count(), r.fail_count(), r.skip_count());
}

void print_report_summary(const ConformanceReport& r) {
  const auto* tag = r.all_passed() ? "PASS" : "FAIL";
  fmt::print("  [{}] {}  ({}/{} passed)\n",
             tag, r.plugin_id, r.pass_count(), r.results.size());
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
    print_usage();
    return args.empty() ? kExitUsage : kExitOk;
  }

  fs::path                  search_dir;
  std::optional<std::string> only_plugin_id;
  bool                      quiet        = false;
  bool                      summary_only = false;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--plugin-id") {
      if (i + 1 >= args.size()) {
        fmt::print(stderr, "error: --plugin-id requires a value\n");
        return kExitUsage;
      }
      only_plugin_id = args[++i];
    } else if (a == "--quiet") {
      quiet = true;
    } else if (a == "--summary-only") {
      summary_only = true;
    } else if (!a.empty() && a.front() == '-') {
      fmt::print(stderr, "error: unknown flag '{}'\n", a);
      return kExitUsage;
    } else if (search_dir.empty()) {
      search_dir = a;
    } else {
      fmt::print(stderr, "error: unexpected positional argument '{}'\n", a);
      return kExitUsage;
    }
  }

  if (search_dir.empty()) {
    fmt::print(stderr, "error: a search directory is required\n");
    print_usage();
    return kExitUsage;
  }

  const auto report = discover_plugins({search_dir});
  if (report.loaded.empty()) {
    fmt::print(stderr, "no plugins discovered under {}", search_dir.string());
    if (!report.rejected.empty()) {
      fmt::print(stderr, " ({} rejected; first: {})",
                 report.rejected.size(), report.rejected.front().reason);
    }
    fmt::print(stderr, "\n");
    return kExitNoPlugins;
  }

  std::size_t total_plugins = 0;
  std::size_t total_failed_plugins = 0;
  for (const auto& d : report.loaded) {
    if (only_plugin_id && d.manifest.id != *only_plugin_id) continue;
    total_plugins += 1;

    const auto cr = run_conformance(d);
    if (!cr.all_passed()) total_failed_plugins += 1;

    if (summary_only) {
      print_report_summary(cr);
    } else if (!quiet) {
      print_report_table(cr);
    }
  }

  if (total_plugins == 0) {
    fmt::print(stderr, "error: no plugins matched --plugin-id '{}'\n",
               only_plugin_id.value_or(""));
    return kExitUsage;
  }

  fmt::print("\n{} plugin(s) scanned, {} failed\n",
             total_plugins, total_failed_plugins);
  return total_failed_plugins == 0 ? kExitOk : kExitFail;
}
