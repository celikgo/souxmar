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

namespace {

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

extern "C"
souxmar_bridge_chat_response_t*
souxmar_bridge_chat_send(const char* request_json,
                         const char* /*project_id*/,
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

  // Pull the bits we need from the request. The shape matches the
  // proxy's openapi.yaml ChatRequest, which itself mirrors
  // souxmar::ai::ChatRequest.
  const std::string model    = extract_string_field(json, "model");
  const auto        messages = extract_messages(json);

  souxmar::ai::ChatRequest req;
  req.model    = model.empty() ? "stub-model" : model;
  req.messages = messages;

  // Sprint 14 push 4 — always StubProvider. Sprint 15 push 1
  // replaces this with a per-project provider lookup against the
  // engine's `provider.h` factory; the FFI shape doesn't change.
  souxmar::ai::StubProvider stub;
  auto result = stub.chat_completion(req);

  auto* out = new (std::nothrow) souxmar_bridge_chat_response_t;
  if (out == nullptr) {
    if (out_err) {
      const char* msg = "bridge: out of memory";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err) std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }
  out->provider = SOUXMAR_BRIDGE_PROVIDER_STUB;

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
