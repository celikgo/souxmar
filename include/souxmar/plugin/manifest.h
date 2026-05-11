// SPDX-License-Identifier: Apache-2.0
//
// Parsed `souxmar-plugin.toml` manifest.
//
// Manifest format is documented in docs/PLUGIN_SDK.md. This module is
// strictly a parser + validator; loading binaries lives in plugin/discovery.h
// and the loader (Sprint 2).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::plugin {

enum class ThreadingModel : std::uint8_t {
  Reentrant       = 0,
  SingleThreaded  = 1,
  InternalParallel = 2,
};

struct Manifest {
  // [plugin]
  std::string  id;            // e.g. "com.example.netgen-mesher"
  std::string  name;
  std::string  version;       // SemVer string
  std::int32_t abi = 0;       // major ABI version targeted (1 for now)
  std::string  license;       // SPDX expression
  std::string  homepage;      // optional, may be empty

  // [plugin.binary]
  std::string  binary_file;   // path relative to manifest

  // [plugin.capabilities]
  std::vector<std::string> capabilities;

  // [plugin.threading]
  ThreadingModel threading = ThreadingModel::SingleThreaded;

  // [plugin.dependencies]
  std::string souxmar_version_constraint;  // e.g. ">=1.0,<2.0"
};

// Parse outcome — either a manifest or a structured error with line context.
struct ParseError {
  std::string message;
  std::optional<std::size_t> line;
};

using ParseResult = std::variant<Manifest, ParseError>;

// Parse from a TOML string.
[[nodiscard]] ParseResult parse_manifest(std::string_view toml_source);

// Parse from a TOML file. Returns ParseError if the file cannot be read or
// fails parsing/validation.
[[nodiscard]] ParseResult parse_manifest_file(const std::filesystem::path& path);

// Helpers for converting the threading-model enum to/from its TOML keyword.
[[nodiscard]] std::string_view to_string(ThreadingModel m) noexcept;
[[nodiscard]] std::optional<ThreadingModel> threading_from_string(std::string_view s) noexcept;

}  // namespace souxmar::plugin
