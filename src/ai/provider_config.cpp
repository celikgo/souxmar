// SPDX-License-Identifier: Apache-2.0
//
// Per-project AI provider configuration loader. ADR-0020.
//
// Sprint 15 push 2.

#include "souxmar/ai/provider_config.h"

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace souxmar::ai {

std::string_view to_string(ProviderKind k) noexcept {
  switch (k) {
    case ProviderKind::Default:
      return "default";
    case ProviderKind::Stub:
      return "stub";
    case ProviderKind::BYOKAnthropic:
      return "byok-anthropic";
    case ProviderKind::BYOKOpenAI:
      return "byok-openai";
    case ProviderKind::Ollama:
      return "ollama";
    case ProviderKind::Managed:
      return "managed";
  }
  return "?";
}

std::string_view to_string(ProviderConfigErrorKind k) noexcept {
  switch (k) {
    case ProviderConfigErrorKind::NotFound:
      return "NotFound";
    case ProviderConfigErrorKind::SchemaMismatch:
      return "SchemaMismatch";
    case ProviderConfigErrorKind::ProviderUnknown:
      return "ProviderUnknown";
    case ProviderConfigErrorKind::MissingField:
      return "MissingField";
    case ProviderConfigErrorKind::MalformedToml:
      return "MalformedToml";
    case ProviderConfigErrorKind::IoError:
      return "IoError";
  }
  return "?";
}

namespace {

ProviderConfigError make_error(ProviderConfigErrorKind kind,
                               std::string message,
                               std::filesystem::path source) {
  return ProviderConfigError{kind, std::move(message), std::move(source)};
}

ProviderKind parse_provider_kind(std::string_view s) {
  if (s == "stub")
    return ProviderKind::Stub;
  if (s == "byok-anthropic")
    return ProviderKind::BYOKAnthropic;
  if (s == "byok-openai")
    return ProviderKind::BYOKOpenAI;
  if (s == "ollama")
    return ProviderKind::Ollama;
  if (s == "managed")
    return ProviderKind::Managed;
  return ProviderKind::Default;  // sentinel for "unknown" — caller maps to error
}

}  // namespace

ProviderConfigResult load_provider_config(const std::filesystem::path& project_dir) {
  const auto config_path = project_dir / "project.ai.toml";

  std::error_code ec;
  if (!std::filesystem::exists(config_path, ec)) {
    return make_error(ProviderConfigErrorKind::NotFound,
                      fmt::format("no project.ai.toml at '{}'", config_path.string()),
                      config_path);
  }
  if (ec) {
    return make_error(
        ProviderConfigErrorKind::IoError,
        fmt::format("filesystem error checking '{}': {}", config_path.string(), ec.message()),
        config_path);
  }

  toml::table tbl;
  try {
    tbl = toml::parse_file(config_path.string());
  } catch (const toml::parse_error& e) {
    return make_error(
        ProviderConfigErrorKind::MalformedToml,
        fmt::format("toml parse failed at '{}': {}", config_path.string(), e.description()),
        config_path);
  } catch (const std::exception& e) {
    return make_error(ProviderConfigErrorKind::IoError,
                      fmt::format("io error reading '{}': {}", config_path.string(), e.what()),
                      config_path);
  }

  const auto schema = tbl["schema"].value<std::int64_t>();
  if (!schema || *schema != 1) {
    return make_error(ProviderConfigErrorKind::SchemaMismatch,
                      fmt::format("schema discriminator must be 1 at '{}'", config_path.string()),
                      config_path);
  }

  const auto provider_name = tbl["provider"].value<std::string>();
  if (!provider_name) {
    return make_error(ProviderConfigErrorKind::ProviderUnknown,
                      fmt::format("'provider' key missing at '{}'", config_path.string()),
                      config_path);
  }
  const auto kind = parse_provider_kind(*provider_name);
  if (kind == ProviderKind::Default) {
    return make_error(ProviderConfigErrorKind::ProviderUnknown,
                      fmt::format("'provider' = '{}' at '{}' is not one of "
                                  "stub / byok-anthropic / byok-openai / ollama / managed",
                                  *provider_name,
                                  config_path.string()),
                      config_path);
  }

  ProviderConfig out;
  out.provider = kind;
  out.source = config_path;
  if (auto model = tbl["model"].value<std::string>(); model) {
    out.model = *model;
  }

  // BYOK providers require a model — the upstream API
  // doesn't pick one for us.
  if ((kind == ProviderKind::BYOKAnthropic || kind == ProviderKind::BYOKOpenAI)
      && out.model.empty()) {
    return make_error(
        ProviderConfigErrorKind::MissingField,
        fmt::format(
            "provider '{}' requires a `model` value at '{}'", *provider_name, config_path.string()),
        config_path);
  }

  // Provider-specific endpoint subtable.
  if (kind == ProviderKind::Ollama) {
    if (auto sub = tbl["ollama"].as_table(); sub) {
      if (auto endpoint = (*sub)["endpoint"].value<std::string>(); endpoint) {
        out.endpoint = *endpoint;
      }
    }
  } else if (kind == ProviderKind::Managed) {
    if (auto sub = tbl["managed"].as_table(); sub) {
      if (auto endpoint = (*sub)["endpoint"].value<std::string>(); endpoint) {
        out.endpoint = *endpoint;
      }
    }
  }

  return out;
}

}  // namespace souxmar::ai
