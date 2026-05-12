// SPDX-License-Identifier: Apache-2.0
//
// Parsed `souxmar-plugin.toml` manifest.
//
// Manifest format is documented in docs/PLUGIN_SDK.md. This module is
// strictly a parser + validator; loading binaries lives in plugin/discovery.h
// and the loader (Sprint 2).
//
// Sprint 6 push 2 hardens the validation surface: every rejection carries
// a stable `ManifestRejection` code (so tooling can group / count without
// regex on the message), and the parser surfaces both line + column from
// toml++. Additive minor fields (description / documentation / tags /
// min_souxmar_abi_minor) ride along under the ABI v1 soak ratchet — they
// are forward-compatible by construction (missing → defaults).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::plugin {

enum class ThreadingModel : std::uint8_t {
  Reentrant = 0,
  SingleThreaded = 1,
  InternalParallel = 2,
};

// Stable rejection codes — STRINGS that ship in audit logs and surface
// to tooling. Append-only; never renumber. (The numeric values are not
// crossed across the ABI today, but stability lets future log readers
// trust on-disk records too.)
enum class ManifestRejection : std::uint16_t {
  Ok = 0,
  TomlSyntax = 1,                  // toml++ rejected the bytes
  MissingField = 2,                // required key absent
  WrongType = 3,                   // key present but wrong type
  AbiUnsupported = 4,              // plugin.abi != 1 in souxmar 1.x
  EmptyCapabilities = 5,           // provides = []
  UnknownThreading = 6,            // model = "garbage"
  InvalidCapabilityNamespace = 7,  // "garbage.foo" — not a known namespace
  InvalidPluginId = 8,             // doesn't look like reverse-DNS
  InvalidVersion = 9,              // doesn't look like SemVer
  FileIo = 10,                     // file couldn't be opened
};

[[nodiscard]] std::string_view to_string(ManifestRejection r) noexcept;

struct Manifest {
  // [plugin]
  std::string id;  // e.g. "com.example.netgen-mesher"
  std::string name;
  std::string version;   // SemVer string
  std::int32_t abi = 0;  // major ABI version targeted (1 for now)
  std::string license;   // SPDX expression
  std::string homepage;  // optional, may be empty

  // [plugin.binary]
  std::string binary_file;  // path relative to manifest

  // [plugin.capabilities]
  std::vector<std::string> capabilities;

  // [plugin.threading]
  ThreadingModel threading = ThreadingModel::SingleThreaded;

  // [plugin.dependencies]
  std::string souxmar_version_constraint;  // e.g. ">=1.0,<2.0"

  // ------------------------------------------------------------------
  // Sprint 6 push 2 — additive optional fields. Missing → defaults.
  // The host treats them as advisory metadata for the plugin index +
  // diagnostics; the loader does not gate on any of them.
  // ------------------------------------------------------------------
  std::string description;                 // optional one-liner
  std::string documentation;               // optional URL
  std::vector<std::string> tags;           // optional, plugin-index hints
  std::int32_t min_souxmar_abi_minor = 0;  // minimum required minor
};

// Parse outcome — either a manifest or a structured error.
struct ParseError {
  std::string message;
  std::optional<std::size_t> line;
  // Sprint 6 push 2 additions. The struct stays brace-initialisable
  // with the legacy `{message, line}` form (defaults fill the new
  // slots), so existing callers keep compiling.
  std::optional<std::size_t> column;
  std::string field;  // dotted path "plugin.abi", or empty
  ManifestRejection code = ManifestRejection::TomlSyntax;
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

// Capability-namespace allow-list. A capability id must look like
// `<namespace>.<name>...` where <namespace> is one of these.
//
// Order matches docs/PLUGIN_SDK.md. Append-only.
[[nodiscard]] std::span<const std::string_view> allowed_capability_namespaces() noexcept;
[[nodiscard]] bool is_allowed_capability(std::string_view capability_id) noexcept;

}  // namespace souxmar::plugin
