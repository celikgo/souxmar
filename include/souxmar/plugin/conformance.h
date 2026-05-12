// SPDX-License-Identifier: Apache-2.0
//
// Plugin conformance suite — the v1 contract every plugin must honor to
// load + serve capabilities against a souxmar host. The same suite gates
// the ABI v1 freeze candidate (see docs/adr/0004-plugin-conformance-suite.md).
//
// Two surfaces:
//   * `run_conformance(plugin, host_version)` — programmatic entry; tests
//     and tooling (CI, the souxmar-conformance CLI, the desktop "plugin
//     diagnostics" panel) call this.
//   * `tools/conformance/main.cpp` — `souxmar-conformance <dir>` binary
//     wrapping the same call with a pretty results table.
//
// The 10 v1 checks (see implementation in src/plugin-host/conformance.cpp
// for the contract each one enforces):
//
//   C001  manifest ABI version matches host
//   C002  manifest binary file resolves to an existing path
//   C003  plugin binary loads (dlopen / LoadLibrary succeeds)
//   C004  souxmar_plugin_register_v1 symbol is exported
//   C005  registration returns success
//   C006  every capability announced in the manifest is registered
//   C007  no unannounced capabilities are registered
//   C008  each registered capability's threading model matches the manifest
//   C009  plugin unload removes every capability owned by this plugin
//   C010  three load/unload cycles leave the registry at the same baseline
//
// Ratchet policy: new checks only join the v1 set in patch revisions if
// they cannot make a previously-passing plugin fail. Otherwise they wait
// for a future versioned suite (C100+ family for v2 etc.).

#pragma once

#include "souxmar/plugin/discovery.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::plugin {

enum class ConformanceOutcome : std::uint8_t {
  Pass = 0,
  Fail = 1,
  // Skip is intentional, not "test didn't run": it means a prior check
  // failed and this check's preconditions are no longer satisfied
  // (e.g. C005 skipped because C003 — the binary didn't load).
  Skip = 2,
};

[[nodiscard]] std::string_view to_string(ConformanceOutcome o) noexcept;

// Catalogue entry — id + short description. The id is the stable key the
// CI / dashboard pivots on; the description is the human-readable label.
struct ConformanceCheck {
  std::string id;           // "C001", "C002", ...
  std::string description;  // "manifest ABI version matches host"
};

struct ConformanceCheckResult {
  std::string check_id;
  ConformanceOutcome outcome = ConformanceOutcome::Skip;
  std::string detail;  // failure reason or skip predecessor
};

struct ConformanceReport {
  std::string plugin_id;
  std::filesystem::path manifest_path;
  std::vector<ConformanceCheckResult> results;

  [[nodiscard]] bool all_passed() const noexcept;
  [[nodiscard]] std::size_t pass_count() const noexcept;
  [[nodiscard]] std::size_t fail_count() const noexcept;
  [[nodiscard]] std::size_t skip_count() const noexcept;
};

// The v1 check catalogue. Returned in canonical (C001 → C010) order.
[[nodiscard]] std::vector<ConformanceCheck> all_checks();

// Run every conformance check against `plugin`. The host_version is
// threaded through the loader as souxmar_host_info_t.host_version, so
// plugins that look at it during registration see the same value any
// production host would supply.
[[nodiscard]] ConformanceReport run_conformance(
    const DiscoveredPlugin& plugin,
    std::string host_version = "souxmar-conformance/0.0.0");

}  // namespace souxmar::plugin
