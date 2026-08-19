// SPDX-License-Identifier: Apache-2.0
//
// Per-project AI provider configuration. ADR-0020 ratifies the
// on-disk format + the sibling-file location decision; this
// header declares the in-memory shape + the loader function.
//
// Sprint 15 push 2.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace souxmar::ai {

// Selected provider. Mirrors the C-side
// SOUXMAR_BRIDGE_PROVIDER_* constants for cross-checking but is
// the canonical engine-side type. Adding a value is non-breaking
// (ADR-0016 additive-Tier-0 pattern); renaming or removing
// requires a Tier-2 deprecation cycle.
enum class ProviderKind : std::uint8_t {
  // No project.ai.toml found (or schema=1 not satisfied);
  // engine defaults to stub.
  Default = 0,
  Stub = 1,
  BYOKAnthropic = 2,
  BYOKOpenAI = 3,
  Ollama = 4,
  Managed = 5,
  // Any service speaking OpenAI's /chat/completions shape: xAI (Grok),
  // OpenAI, DeepSeek, Groq, Mistral, OpenRouter, Together, or a local
  // server. Which one is decided by `base_url`, resolved either from a
  // named preset (`provider = "grok"`) or given explicitly. This is the
  // kind that makes adding a service configuration rather than code.
  OpenAICompatible = 6,
};

[[nodiscard]] std::string_view to_string(ProviderKind) noexcept;

struct ProviderConfig {
  ProviderKind provider = ProviderKind::Default;
  std::string model;     // empty when not specified
  std::string endpoint;  // populated for ollama / managed when overridden

  // --- OpenAICompatible only ---
  // The `provider` string exactly as written, e.g. "grok". Kept so the
  // UI and log lines can name the service the user chose rather than
  // the generic kind.
  std::string provider_id;
  // API root, no trailing slash. Resolved from the named preset unless
  // the file set `[openai_compatible] base_url`.
  std::string base_url;
  // Name of the environment variable holding the API key. The key
  // itself is never read from the config file — see the loader.
  std::string api_key_env;

  // Source file that was parsed. Empty when no file was found.
  // Useful for error messages + the chat panel's "via <provider>"
  // chip when reporting "no project.ai.toml — falling back to
  // stub."
  std::filesystem::path source;
};

// Errors from load_provider_config. Typed enum so callers don't
// string-parse the reason (same pattern as the rest of the
// engine since Sprint 6).
enum class ProviderConfigErrorKind : std::uint8_t {
  // No project.ai.toml at the looked-for path. Not necessarily
  // an error — load_provider_config() returns this when the
  // caller asked for a project that doesn't ship a config; the
  // bridge treats this as "fall back to default" rather than a
  // hard error.
  NotFound = 0,
  // Schema discriminator absent or != 1.
  SchemaMismatch = 1,
  // File parsed but `provider` value was missing or unknown.
  ProviderUnknown = 2,
  // File parsed but a required key for the chosen provider was
  // missing (e.g. byok-anthropic without a model).
  MissingField = 3,
  // toml++ raised a parse error.
  MalformedToml = 4,
  // I/O error reading the file (perms, disk failure, etc.).
  IoError = 5,
  // The file contained an inline API key. project.ai.toml sits next to
  // pipeline.yaml in the user's project and is routinely committed, so
  // the loader refuses the file outright rather than reading a secret
  // out of it. Use `api_key_env` and an environment variable.
  SecretInConfig = 6,
};

[[nodiscard]] std::string_view to_string(ProviderConfigErrorKind) noexcept;

struct ProviderConfigError {
  ProviderConfigErrorKind kind = ProviderConfigErrorKind::NotFound;
  std::string message;
  std::filesystem::path source;  // file (or directory) the loader looked at
};

using ProviderConfigResult = std::variant<ProviderConfig, ProviderConfigError>;

// Load + parse the project.ai.toml that sits next to a project's
// pipeline.yaml / project.souxmar.toml.
//
// `project_dir` is the directory containing the project files.
// The loader looks for `<project_dir>/project.ai.toml`.
//
// Returns ProviderConfig on success. Returns ProviderConfigError
// of kind NotFound when no file exists at the expected path —
// the caller (today: the bridge's provider.cpp) is expected to
// treat this as "fall back to default" rather than a hard
// failure. Other ProviderConfigError kinds are real configuration
// errors that should surface to the user.
[[nodiscard]] ProviderConfigResult load_provider_config(const std::filesystem::path& project_dir);

}  // namespace souxmar::ai
