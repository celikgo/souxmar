// SPDX-License-Identifier: Apache-2.0
//
// Per-project AI provider configuration loader. ADR-0020.
//
// Sprint 15 push 2.

#include "souxmar/ai/provider_config.h"

// The preset table lives with the provider that consumes it; the
// loader only resolves names against it.
#include "souxmar/ai/provider.h"

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
    case ProviderKind::OpenAICompatible:
      return "openai-compatible";
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
    case ProviderConfigErrorKind::SecretInConfig:
      return "SecretInConfig";
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
  // "anthropic" is the name to use; "byok-anthropic" is the original
  // spelling and stays accepted so existing configs keep working.
  if (s == "byok-anthropic" || s == "anthropic" || s == "claude")
    return ProviderKind::BYOKAnthropic;
  // "byok-openai" predates the OpenAI-compatible provider and used to
  // resolve to a kind nothing could construct. It is now an alias for
  // the `openai` preset, so the configs that already say it start
  // working rather than continuing to error.
  if (s == "byok-openai")
    return ProviderKind::OpenAICompatible;
  if (s == "ollama")
    return ProviderKind::Ollama;
  if (s == "managed")
    return ProviderKind::Managed;
  // Any preset id ("grok", "openai", "deepseek", ...) and the generic
  // "openai-compatible" escape hatch resolve to one kind; which service
  // it is comes from base_url. `byok-openai` above keeps its own kind
  // for backwards compatibility with configs written before this.
  if (find_openai_compatible_preset(s) != nullptr)
    return ProviderKind::OpenAICompatible;
  return ProviderKind::Default;  // sentinel for "unknown" — caller maps to error
}

// The `provider` values the loader accepts, for the error message.
std::string known_provider_list() {
  std::string out = "stub / anthropic / ollama / managed";
  for (const auto& p : openai_compatible_presets()) {
    out += " / ";
    out += p.id;
  }
  return out;
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
                      fmt::format("'provider' = '{}' at '{}' is not one of {}",
                                  *provider_name,
                                  config_path.string(),
                                  known_provider_list()),
                      config_path);
  }

  ProviderConfig out;
  out.provider = kind;
  out.source = config_path;
  if (auto model = tbl["model"].value<std::string>(); model) {
    out.model = *model;
  }

  // A key in this file would be a secret in a file that sits next to
  // pipeline.yaml and gets committed. Refuse the whole config rather
  // than reading it — a hard error is the only version of this the user
  // cannot ignore. Checked before anything else uses the table so the
  // message is the first thing they see.
  for (const char* key : {"api_key", "apikey", "key", "token", "secret"}) {
    if (tbl[key]
        || (tbl["openai_compatible"].as_table() && (*tbl["openai_compatible"].as_table())[key])) {
      return make_error(
          ProviderConfigErrorKind::SecretInConfig,
          fmt::format("'{}' must not appear in '{}' — this file lives beside pipeline.yaml "
                      "and is routinely committed. Put the key in an environment variable "
                      "and name it with `[openai_compatible] api_key_env`.",
                      key,
                      config_path.string()),
          config_path);
    }
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

  // Anthropic: fixed endpoint and conventional key variable, both
  // overridable from an `[anthropic]` subtable so a gateway or a
  // differently-named variable is reachable without a code change.
  if (kind == ProviderKind::BYOKAnthropic) {
    out.provider_id = "anthropic";
    out.base_url = "https://api.anthropic.com/v1";
    out.api_key_env = "ANTHROPIC_API_KEY";
    if (auto sub = tbl["anthropic"].as_table(); sub) {
      if (auto base = (*sub)["base_url"].value<std::string>(); base) {
        out.base_url = *base;
      }
      if (auto env = (*sub)["api_key_env"].value<std::string>(); env) {
        out.api_key_env = *env;
      }
    }
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
  } else if (kind == ProviderKind::OpenAICompatible) {
    // `byok-openai` is the legacy spelling of the `openai` preset;
    // normalise so preset lookup and the UI's "via <provider>" chip
    // both see the service name rather than the alias.
    out.provider_id = *provider_name == "byok-openai" ? "openai" : *provider_name;
    // Start from the named preset, then let the file override. A
    // service we have never heard of is reachable by setting base_url
    // under `provider = "openai-compatible"`.
    if (const auto* preset = find_openai_compatible_preset(out.provider_id); preset) {
      out.base_url = std::string(preset->base_url);
      out.api_key_env = std::string(preset->key_env);
    }
    if (auto sub = tbl["openai_compatible"].as_table(); sub) {
      if (auto base = (*sub)["base_url"].value<std::string>(); base) {
        out.base_url = *base;
      }
      if (auto env = (*sub)["api_key_env"].value<std::string>(); env) {
        out.api_key_env = *env;
      }
    }
    // Every service needs a model id and we will not guess one: a
    // wrong default produces a 404 that reads like a broken install.
    if (out.model.empty()) {
      return make_error(ProviderConfigErrorKind::MissingField,
                        fmt::format("provider '{}' requires a `model` value at '{}' — set the "
                                    "model id your account can reach",
                                    *provider_name,
                                    config_path.string()),
                        config_path);
    }
    if (out.base_url.empty()) {
      return make_error(
          ProviderConfigErrorKind::MissingField,
          fmt::format("provider '{}' needs `[openai_compatible] base_url` at '{}' — it has no "
                      "built-in endpoint",
                      *provider_name,
                      config_path.string()),
          config_path);
    }
  }

  return out;
}

}  // namespace souxmar::ai
