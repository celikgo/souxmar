// SPDX-License-Identifier: Apache-2.0
//
// souxmar version + ABI introspection.
//
// The numeric version is the project's release version (semver).
// The ABI version is the plugin C ABI's major version, frozen at 1 for the
// entire 1.x release series. See docs/PLUGIN_SDK.md and ADR-0001.

#pragma once

#include <cstdint>
#include <string_view>

namespace souxmar {

struct Version {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;
};

[[nodiscard]] Version version() noexcept;
[[nodiscard]] std::string_view version_string() noexcept;
[[nodiscard]] std::uint32_t abi_version() noexcept;

}  // namespace souxmar
