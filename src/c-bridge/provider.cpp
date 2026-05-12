// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge — provider_call surface. Sprint 14 push 4.
//
// Second real FFI surface. Routes ChatRequest JSON from the Rust
// side to the engine's Provider abstraction (Sprint 10 push 9),
// returns ChatResponse or ProviderError as typed enums + strings.
//
// Today's implementation uses StubProvider so the wiring is
// exercisable end-to-end without requiring Anthropic / OpenAI /
// Ollama credentials at FFI-test time. Sprint 15 push 1 swaps
// in a per-project provider lookup that returns the configured
// real provider (BYOK Anthropic, BYOK OpenAI, Ollama, or the
// managed-AI proxy from ADR-0019).
//
// JSON parsing on this surface is intentionally minimal — we use
// a small regex-based extractor for the few fields we need rather
// than dragging a JSON library into the bridge's link line. The
// shape was designed with souxmar::ai::ChatRequest's defaults in
// mind: missing fields fall back to safe defaults; an obviously
// malformed request comes back as BadRequest at the engine layer.

#include "souxmar-c-bridge/provider.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <regex>
#include <string>
#include <variant>
#include <vector>

#include "souxmar/ai/provider.h"
#include "souxmar/ai/provider_config.h"

#include <filesystem>

namespace {

std::string fmt_provider_not_yet_wired(int32_t kind) {
  switch (kind) {
    case SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC:
      return "BYOK Anthropic not yet wired through the bridge "
             "(Sprint 15 push 3 lands the forwarder). For now, "
             "set provider = \"stub\" or \"ollama\" in project.ai.toml.";
    case SOUXMAR_BRIDGE_PROVIDER_OPENAI:
      return "BYOK OpenAI not yet wired through the bridge "
             "(Sprint 16+ lands the forwarder). For now, "
             "set provider = \"stub\" or \"ollama\" in project.ai.toml.";
    case SOUXMAR_BRIDGE_PROVIDER_MANAGED:
      return "Managed-AI proxy not yet reachable through the bridge "
             "(Sprint 17 wires the account portal). For now, "
             "set provider = \"stub\" or \"ollama\" in project.ai.toml.";
    default:
      return "provider not yet wired";
  }
}

int32_t bridge_error_kind_for(souxmar::ai::ProviderErrorKind kind) {
  using K = souxmar::ai::ProviderErrorKind;
  switch (kind) {
    case K::ProviderHttpError:      return SOUXMAR_BRIDGE_PE_HTTP_ERROR;
    case K::RateLimited:            return SOUXMAR_BRIDGE_PE_RATE_LIMITED;
    case K::LocalDaemonUnreachable: return SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    case K::ModelNotFound:          return SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    case K::HttpClientFailed:       return SOUXMAR_BRIDGE_PE_INTERNAL;
    case K::MalformedResponse:      return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::ProtocolMismatch:       return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::BadRequest:             return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::ContextLengthExceeded:  return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
  }
  return SOUXMAR_BRIDGE_PE_INTERNAL;
}

// Extract a "key": "string" value from a tiny JSON-ish blob. The
// bridge does not validate the JSON; it surfaces a bad-request
// error if a required field is missing. Sprint 15 push 1 swaps
// this for a real parser via the OpenAPI generator the proxy
// shares with us.
std::string extract_string_field(const std::string& json,
                                  const std::string& key) {
  std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re)) return m[1].str();
  return {};
}

// Extract a list of "role"/"content" pairs from a `messages: [...]`
// block. Intentionally minimal — anything beyond simple role +
// content lands once the proxy's OpenAPI generator runs.
std::vector<souxmar::ai::ChatMessage>
extract_messages(const std::string& json) {
  std::vector<souxmar::ai::ChatMessage> out;
  std::regex msg(
      "\\{\\s*\"role\"\\s*:\\s*\"([^\"]+)\"\\s*,"
      "\\s*\"content\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\}");
  auto begin = std::sregex_iterator(json.begin(), json.end(), msg);
  auto end   = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    souxmar::ai::ChatMessage m;
    const std::string r = (*it)[1].str();
    if      (r == "system")    m.role = souxmar::ai::ChatMessage::Role::System;
    else if (r == "user")      m.role = souxmar::ai::ChatMessage::Role::User;
    else if (r == "assistant") m.role = souxmar::ai::ChatMessage::Role::Assistant;
    else if (r == "tool")      m.role = souxmar::ai::ChatMessage::Role::Tool;
    m.content = (*it)[2].str();
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace

struct souxmar_bridge_chat_response_t {
  int32_t      error_kind   = SOUXMAR_BRIDGE_PE_OK;
  std::string  error_text;
  std::string  reply_text;
  int32_t      provider     = SOUXMAR_BRIDGE_PROVIDER_UNKNOWN;
  int64_t      tokens_in    = 0;
  int64_t      tokens_out   = 0;
};

namespace {

// Resolve the per-project provider config. Sprint 15 push 2 reads
// `<project_dir>/project.ai.toml` per ADR-0020. project_id is
// interpreted as the project directory path; an empty / missing
// directory falls back to the default (StubProvider). A malformed
// config surfaces an error to the caller.
struct ResolvedProvider {
  int32_t                              bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;
  std::string                          model;
  std::string                          endpoint;
  // When non-empty, a config-parse error to surface back to the
  // caller (instead of attempting a stub call as a silent fallback).
  std::string                          config_error;
};

ResolvedProvider resolve_provider(const std::string& project_id) {
  ResolvedProvider rp;
  if (project_id.empty()) return rp;

  std::filesystem::path dir(project_id);
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    // The project_id could be a path to a file (e.g. a YAML).
    // Try its parent dir.
    if (std::filesystem::exists(dir, ec)) {
      dir = dir.parent_path();
    } else {
      return rp;  // no such project; default
    }
  }

  auto r = souxmar::ai::load_provider_config(dir);
  if (auto* err = std::get_if<souxmar::ai::ProviderConfigError>(&r)) {
    if (err->kind == souxmar::ai::ProviderConfigErrorKind::NotFound) {
      return rp;  // absent → default (StubProvider)
    }
    rp.config_error = err->message;
    return rp;
  }

  const auto& cfg = std::get<souxmar::ai::ProviderConfig>(r);
  rp.model    = cfg.model;
  rp.endpoint = cfg.endpoint;
  using K = souxmar::ai::ProviderKind;
  switch (cfg.provider) {
    case K::Stub:          rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;       break;
    case K::BYOKAnthropic: rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC;  break;
    case K::BYOKOpenAI:    rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_OPENAI;     break;
    case K::Ollama:        rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_OLLAMA;     break;
    case K::Managed:       rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_MANAGED;    break;
    default:               rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;       break;
  }
  return rp;
}

}  // namespace

extern "C"
souxmar_bridge_chat_response_t*
souxmar_bridge_chat_send(const char* request_json,
                         const char* project_id_c,
                         char**      out_err) {
  if (out_err) *out_err = nullptr;
  if (request_json == nullptr) {
    if (out_err) {
      const char* msg = "request_json is NULL";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err) std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }

  const std::string json(request_json);
  const std::string project_id = project_id_c ? std::string(project_id_c) : std::string{};

  // Sprint 15 push 2 — consult per-project config (ADR-0020).
  const auto resolved = resolve_provider(project_id);

  // Pull the bits we need from the request.
  const std::string model    = extract_string_field(json, "model");
  const auto        messages = extract_messages(json);

  souxmar::ai::ChatRequest req;
  req.model    = !resolved.model.empty()
                  ? resolved.model
                  : (model.empty() ? "stub-model" : model);
  req.messages = messages;

  auto* out = new (std::nothrow) souxmar_bridge_chat_response_t;
  if (out == nullptr) {
    if (out_err) {
      const char* msg = "bridge: out of memory";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err) std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }
  out->provider = resolved.bridge_kind;

  // Config parse error surfaces directly to the caller — silent
  // fallback to stub would mislead the user (ADR-0020 § Risks
  // R-019).
  if (!resolved.config_error.empty()) {
    out->error_kind = SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    out->error_text = resolved.config_error;
    return out;
  }

  // Pick the provider. Today only Stub + Ollama are implementable
  // entirely engine-side; Anthropic + OpenAI + Managed need
  // out-of-process resources Sprint 15 push 3 / Sprint 17 wire.
  souxmar::ai::ChatResult result;
  if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_OLLAMA) {
    souxmar::ai::OllamaProviderOptions opts;
    if (!resolved.endpoint.empty()) opts.endpoint = resolved.endpoint;
    souxmar::ai::OllamaProvider ollama(opts);
    result = ollama.chat_completion(req);
  } else if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC ||
             resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_OPENAI    ||
             resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_MANAGED) {
    // Not yet wired — return NotConfigured with an honest
    // explanation. Sprint 15 push 3 lands the Anthropic
    // forwarder; Sprint 17 wires Managed. Until then, the
    // chat panel surfaces this through its typed error path.
    out->error_kind = SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    out->error_text = fmt_provider_not_yet_wired(resolved.bridge_kind);
    return out;
  } else {
    souxmar::ai::StubProvider stub;
    result = stub.chat_completion(req);
  }

  if (auto* resp = std::get_if<souxmar::ai::ChatResponse>(&result)) {
    out->error_kind = SOUXMAR_BRIDGE_PE_OK;
    out->reply_text = resp->text;
    out->tokens_in  = static_cast<int64_t>(resp->input_tokens);
    out->tokens_out = static_cast<int64_t>(resp->output_tokens);
  } else {
    const auto& err = std::get<souxmar::ai::ProviderError>(result);
    out->error_kind = bridge_error_kind_for(err.kind);
    out->error_text = err.message;
  }
  return out;
}

extern "C"
int32_t
souxmar_bridge_chat_error_kind(const souxmar_bridge_chat_response_t* r) {
  return r ? r->error_kind : SOUXMAR_BRIDGE_PE_INTERNAL;
}

extern "C"
const char*
souxmar_bridge_chat_error_text(const souxmar_bridge_chat_response_t* r) {
  return r ? r->error_text.c_str() : "";
}

extern "C"
const char*
souxmar_bridge_chat_reply_text(const souxmar_bridge_chat_response_t* r) {
  return r ? r->reply_text.c_str() : "";
}

extern "C"
int32_t
souxmar_bridge_chat_provider(const souxmar_bridge_chat_response_t* r) {
  return r ? r->provider : SOUXMAR_BRIDGE_PROVIDER_UNKNOWN;
}

extern "C"
int64_t
souxmar_bridge_chat_tokens_in(const souxmar_bridge_chat_response_t* r) {
  return r ? r->tokens_in : 0;
}

extern "C"
int64_t
souxmar_bridge_chat_tokens_out(const souxmar_bridge_chat_response_t* r) {
  return r ? r->tokens_out : 0;
}

extern "C"
void souxmar_bridge_chat_response_free(souxmar_bridge_chat_response_t* r) {
  delete r;
}
