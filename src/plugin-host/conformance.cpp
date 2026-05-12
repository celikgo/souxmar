// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/conformance.h"

#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/manifest.h"
#include "souxmar/plugin/registry.h"

#include "souxmar-c/abi.h"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace souxmar::plugin {

namespace fs = std::filesystem;

std::string_view to_string(ConformanceOutcome o) noexcept {
  switch (o) {
    case ConformanceOutcome::Pass:
      return "PASS";
    case ConformanceOutcome::Fail:
      return "FAIL";
    case ConformanceOutcome::Skip:
      return "SKIP";
  }
  return "?";
}

// ============================================================================
// Catalogue
// ============================================================================

namespace {

constexpr std::pair<const char*, const char*> kChecks[] = {
    {"C001", "manifest ABI version matches host"},
    {"C002", "manifest binary file resolves to an existing path"},
    {"C003", "plugin binary loads (dlopen / LoadLibrary succeeds)"},
    {"C004", "souxmar_plugin_register_v1 symbol is exported"},
    {"C005", "registration returns success"},
    {"C006", "every capability announced in the manifest is registered"},
    {"C007", "no unannounced capabilities are registered"},
    {"C008", "each registered capability's threading model matches the manifest"},
    {"C009", "plugin unload removes every capability owned by this plugin"},
    {"C010", "three load/unload cycles leave the registry at the same baseline"},
};

// Heuristic attribution from a loader error message back to the
// specific contract that was violated. The loader's existing error
// strings (see src/plugin-host/loader.cpp) include "dlopen", "LoadLibrary",
// "missing exported symbol", and the per-plugin "register_v1 returned"
// signature; we map those to C003 / C004 / C005 respectively.
struct LoadAttribution {
  std::string c003;  // dlopen / LoadLibrary verdict
  std::string c004;  // symbol resolution verdict
  std::string c005;  // registration verdict
  bool loaded = false;
};

LoadAttribution attribute_load_error(const std::string& message) {
  LoadAttribution a;
  if (message.find("dlopen") != std::string::npos
      || message.find("LoadLibraryExW") != std::string::npos
      || message.find("LoadLibrary") != std::string::npos) {
    a.c003 = message;
    return a;
  }
  if (message.find("missing exported symbol") != std::string::npos) {
    a.c004 = message;
    return a;
  }
  // Anything else came from the plugin's registration call itself.
  a.c005 = message;
  return a;
}

}  // namespace

std::vector<ConformanceCheck> all_checks() {
  std::vector<ConformanceCheck> out;
  out.reserve(std::size(kChecks));
  for (const auto& [id, desc] : kChecks) {
    out.push_back({std::string(id), std::string(desc)});
  }
  return out;
}

// ============================================================================
// Report
// ============================================================================

bool ConformanceReport::all_passed() const noexcept {
  for (const auto& r : results) {
    if (r.outcome == ConformanceOutcome::Fail)
      return false;
  }
  // A clean run is one where everything Passed (Skips are inconclusive
  // and indicate an earlier failure). Treat any Skip as not-passed too.
  for (const auto& r : results) {
    if (r.outcome != ConformanceOutcome::Pass)
      return false;
  }
  return true;
}

std::size_t ConformanceReport::pass_count() const noexcept {
  return static_cast<std::size_t>(std::count_if(results.begin(), results.end(), [](const auto& r) {
    return r.outcome == ConformanceOutcome::Pass;
  }));
}

std::size_t ConformanceReport::fail_count() const noexcept {
  return static_cast<std::size_t>(std::count_if(results.begin(), results.end(), [](const auto& r) {
    return r.outcome == ConformanceOutcome::Fail;
  }));
}

std::size_t ConformanceReport::skip_count() const noexcept {
  return static_cast<std::size_t>(std::count_if(results.begin(), results.end(), [](const auto& r) {
    return r.outcome == ConformanceOutcome::Skip;
  }));
}

// ============================================================================
// run_conformance
// ============================================================================

namespace {

void add_result(ConformanceReport& report,
                std::string_view id,
                ConformanceOutcome outcome,
                std::string detail = {}) {
  report.results.push_back({std::string(id), outcome, std::move(detail)});
}

// Mark every id in `ids` as Skip with the same predecessor reason.
void skip_remaining(ConformanceReport& report,
                    std::initializer_list<const char*> ids,
                    std::string_view predecessor) {
  for (const auto* id : ids) {
    add_result(report,
               id,
               ConformanceOutcome::Skip,
               fmt::format("prior check {} did not pass", predecessor));
  }
}

}  // namespace

ConformanceReport run_conformance(const DiscoveredPlugin& plugin, std::string host_version) {
  ConformanceReport report;
  report.plugin_id = plugin.manifest.id;
  report.manifest_path = plugin.manifest_path;
  report.results.reserve(std::size(kChecks));

  // ---- C001: manifest ABI version matches host -------------------------
  if (plugin.manifest.abi == SOUXMAR_ABI_VERSION_MAJOR) {
    add_result(report, "C001", ConformanceOutcome::Pass);
  } else {
    add_result(
        report,
        "C001",
        ConformanceOutcome::Fail,
        fmt::format(
            "manifest abi = {}, host expects {}", plugin.manifest.abi, SOUXMAR_ABI_VERSION_MAJOR));
    skip_remaining(
        report, {"C002", "C003", "C004", "C005", "C006", "C007", "C008", "C009", "C010"}, "C001");
    return report;
  }

  // ---- C002: binary file resolves to an existing path ------------------
  std::error_code ec;
  const bool binary_exists = fs::exists(plugin.binary_path, ec) && !ec;
  if (binary_exists) {
    add_result(report, "C002", ConformanceOutcome::Pass);
  } else {
    add_result(report,
               "C002",
               ConformanceOutcome::Fail,
               fmt::format("binary not found at {}", plugin.binary_path.string()));
    skip_remaining(
        report, {"C003", "C004", "C005", "C006", "C007", "C008", "C009", "C010"}, "C002");
    return report;
  }

  // ---- C003 / C004 / C005: load + register sequence --------------------
  //
  // We pull these three checks from a single loader.load() call; the
  // loader's structured error message tells us which contract broke.
  // The downside: a plugin that fails registration cleanly still shows
  // C003 + C004 as Pass — but that's the correct semantics, those
  // checks _did_ pass.
  Registry registry;
  PluginLoader loader(registry, host_version);
  auto load_result = loader.load(plugin);

  if (auto* err = std::get_if<LoadError>(&load_result)) {
    const auto attr = attribute_load_error(err->message);
    if (!attr.c003.empty()) {
      add_result(report, "C003", ConformanceOutcome::Fail, attr.c003);
      skip_remaining(report, {"C004", "C005", "C006", "C007", "C008", "C009", "C010"}, "C003");
    } else if (!attr.c004.empty()) {
      add_result(report, "C003", ConformanceOutcome::Pass);
      add_result(report, "C004", ConformanceOutcome::Fail, attr.c004);
      skip_remaining(report, {"C005", "C006", "C007", "C008", "C009", "C010"}, "C004");
    } else {
      add_result(report, "C003", ConformanceOutcome::Pass);
      add_result(report, "C004", ConformanceOutcome::Pass);
      add_result(
          report, "C005", ConformanceOutcome::Fail, attr.c005.empty() ? err->message : attr.c005);
      skip_remaining(report, {"C006", "C007", "C008", "C009", "C010"}, "C005");
    }
    return report;
  }

  auto loaded = std::move(std::get<LoadedPlugin>(load_result));
  add_result(report, "C003", ConformanceOutcome::Pass);
  add_result(report, "C004", ConformanceOutcome::Pass);
  add_result(report, "C005", ConformanceOutcome::Pass);

  // ---- C006: every announced capability is registered ------------------
  std::set<std::string> announced(plugin.manifest.capabilities.begin(),
                                  plugin.manifest.capabilities.end());
  // The registry's list_capabilities returns every capability across all
  // loaded plugins. In a fresh registry that's exactly this plugin's set,
  // but to be safe we filter by plugin_id via Registry::find().
  std::set<std::string> registered;
  for (const auto& id : registry.list_capabilities()) {
    if (const auto* e = registry.find(id); e && e->plugin_id == plugin.manifest.id) {
      registered.insert(id);
    }
  }

  {
    std::vector<std::string> missing;
    for (const auto& id : announced) {
      if (!registered.contains(id))
        missing.push_back(id);
    }
    if (missing.empty()) {
      add_result(report, "C006", ConformanceOutcome::Pass);
    } else {
      add_result(report,
                 "C006",
                 ConformanceOutcome::Fail,
                 "manifest announced capabilities that were not registered: "
                     + fmt::format("{}", fmt::join(missing, ", ")));
    }
  }

  // ---- C007: no unannounced capabilities -------------------------------
  {
    std::vector<std::string> extras;
    for (const auto& id : registered) {
      if (!announced.contains(id))
        extras.push_back(id);
    }
    if (extras.empty()) {
      add_result(report, "C007", ConformanceOutcome::Pass);
    } else {
      add_result(report,
                 "C007",
                 ConformanceOutcome::Fail,
                 "registered capabilities not announced in the manifest: "
                     + fmt::format("{}", fmt::join(extras, ", ")));
    }
  }

  // ---- C008: registered threading model matches the manifest -----------
  //
  // Every capability the plugin registered should carry the manifest's
  // declared threading model — that's how the parallel runner enforces
  // reentrancy. A mismatch means the loader's current_plugin_threading_
  // slot didn't reach add_*_c, which would silently break the runner.
  {
    bool all_match = true;
    std::string mismatch_detail;
    for (const auto& id : registered) {
      auto m = registry.find_threading(id);
      if (!m.has_value() || *m != plugin.manifest.threading) {
        all_match = false;
        mismatch_detail = fmt::format("capability '{}' threading = {}, manifest declared {}",
                                      id,
                                      m.has_value() ? static_cast<int>(*m) : -1,
                                      static_cast<int>(plugin.manifest.threading));
        break;
      }
    }
    add_result(report,
               "C008",
               all_match ? ConformanceOutcome::Pass : ConformanceOutcome::Fail,
               all_match ? "" : mismatch_detail);
  }

  // ---- C009: unload removes every capability owned by this plugin ------
  //
  // LoadedPlugin's destructor calls registry.remove_plugin(); we exercise
  // it by moving `loaded` into a nested scope and re-querying afterward.
  {
    {
      auto sink = std::move(loaded);
      (void)sink;
    }
    bool any_leftover = false;
    for (const auto& id : registry.list_capabilities()) {
      if (const auto* e = registry.find(id); e && e->plugin_id == plugin.manifest.id) {
        any_leftover = true;
        break;
      }
    }
    if (!any_leftover && registry.size() == 0) {
      add_result(report, "C009", ConformanceOutcome::Pass);
    } else if (!any_leftover) {
      // A leftover from a DIFFERENT plugin id would be a host bug, not
      // a plugin conformance issue. Treat as Pass here; the host's own
      // tests cover that case.
      add_result(report, "C009", ConformanceOutcome::Pass);
    } else {
      add_result(report,
                 "C009",
                 ConformanceOutcome::Fail,
                 "registry still contains capabilities owned by this plugin "
                 "after LoadedPlugin destruction");
      skip_remaining(report, {"C010"}, "C009");
      return report;
    }
  }

  // ---- C010: three load/unload cycles leave registry at baseline -------
  //
  // The baseline is "registry.size() == 0" — we already proved that in
  // C009. Now we exercise the cycle two more times and confirm the count
  // returns to zero each time + the same capability set re-appears.
  {
    bool all_clean = true;
    std::string detail;
    for (int i = 0; i < 3; ++i) {
      Registry r;
      PluginLoader l(r, host_version);
      auto lr = l.load(plugin);
      if (auto* lerr = std::get_if<LoadError>(&lr)) {
        all_clean = false;
        detail = fmt::format("cycle {}/3 failed to load: {}", i + 1, lerr->message);
        break;
      }
      {
        auto sink = std::move(std::get<LoadedPlugin>(lr));
        // Re-check the registered set matches the announced set on every
        // cycle — a plugin that has internal state could regress between
        // cycles, and we'd rather catch it here than in production.
        std::set<std::string> reg;
        for (const auto& id : r.list_capabilities())
          reg.insert(id);
        if (reg != announced) {
          all_clean = false;
          detail = fmt::format(
              "cycle {}/3 registered a different capability set "
              "({} vs declared {})",
              i + 1,
              reg.size(),
              announced.size());
          break;
        }
      }
      if (r.size() != 0) {
        all_clean = false;
        detail = fmt::format(
            "cycle {}/3 left {} capabilities in the registry after unload", i + 1, r.size());
        break;
      }
    }
    add_result(report,
               "C010",
               all_clean ? ConformanceOutcome::Pass : ConformanceOutcome::Fail,
               all_clean ? "" : detail);
  }

  return report;
}

}  // namespace souxmar::plugin
