// SPDX-License-Identifier: Apache-2.0
//
// AI provider implementations. See include/souxmar/ai/provider.h for
// the interface and the Sprint-10-push-9 design notes.

#include "souxmar/ai/provider.h"

#include "souxmar/plugin/subprocess.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace souxmar::ai {

// ---- Stringification ---------------------------------------------------

std::string_view to_string(ChatMessage::Role r) noexcept {
  switch (r) {
    case ChatMessage::Role::System:    return "system";
    case ChatMessage::Role::User:      return "user";
    case ChatMessage::Role::Assistant: return "assistant";
    case ChatMessage::Role::Tool:      return "tool";
  }
  return "unknown";
}

std::string_view to_string(ProviderErrorKind k) noexcept {
  switch (k) {
    case ProviderErrorKind::ProviderHttpError:      return "provider-http-error";
    case ProviderErrorKind::RateLimited:            return "rate-limited";
    case ProviderErrorKind::LocalDaemonUnreachable: return "local-daemon-unreachable";
    case ProviderErrorKind::ModelNotFound:          return "model-not-found";
    case ProviderErrorKind::HttpClientFailed:       return "http-client-failed";
    case ProviderErrorKind::MalformedResponse:      return "malformed-response";
    case ProviderErrorKind::ProtocolMismatch:       return "protocol-mismatch";
    case ProviderErrorKind::BadRequest:             return "bad-request";
    case ProviderErrorKind::ContextLengthExceeded:  return "context-length-exceeded";
  }
  return "unknown";
}

// ---- JSON encoding helpers --------------------------------------------
//
// Hand-rolled because rendering the request needs to be
// deterministic (test fixtures compare byte-exact), and the bring-in
// cost of a JSON library at this layer is more than the surface
// here justifies. Six escape sequences (\" \\ \n \r \t backslash-u
// for control codes) cover everything Ollama's wire format needs.

namespace {

void json_escape_append(std::string& out, std::string_view s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\"";  break;
      case '\\': out += "\\\\";  break;
      case '\b': out += "\\b";   break;
      case '\f': out += "\\f";   break;
      case '\n': out += "\\n";   break;
      case '\r': out += "\\r";   break;
      case '\t': out += "\\t";   break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

}  // namespace

// ---- StubProvider ------------------------------------------------------

StubProvider::StubProvider() = default;

void StubProvider::program_reply(std::string_view  model,
                                 std::string_view  trigger_substring,
                                 ChatResponse      reply) {
  table_.push_back({std::string(model), std::string(trigger_substring),
                    std::move(reply)});
}

std::vector<std::string> StubProvider::available_models() const {
  std::vector<std::string> models;
  for (const auto& p : table_) {
    if (std::find(models.begin(), models.end(), p.model) == models.end()) {
      models.push_back(p.model);
    }
  }
  return models;
}

ChatResult StubProvider::chat_completion(const ChatRequest& req) {
  if (req.messages.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest,
                         "stub: empty messages"};
  }
  std::string_view last;
  for (auto it = req.messages.rbegin(); it != req.messages.rend(); ++it) {
    if (it->role == ChatMessage::Role::User) {
      last = it->content;
      break;
    }
  }
  for (const auto& p : table_) {
    if (p.model != req.model) continue;
    if (p.trigger.empty() ||
        last.find(p.trigger) != std::string_view::npos) {
      return p.reply;
    }
  }
  return ProviderError{ProviderErrorKind::ProtocolMismatch,
                       "stub: no programmed reply matched (model='" +
                       req.model + "')"};
}

// ---- OllamaProvider ----------------------------------------------------

OllamaProvider::OllamaProvider(OllamaProviderOptions opts)
    : opts_(std::move(opts)) {
  if (opts_.curl_binary.empty()) opts_.curl_binary = "curl";
  if (const char* env = std::getenv("SOUXMAR_OLLAMA_TIMEOUT_SECS");
      env && *env) {
    try {
      opts_.timeout = std::chrono::seconds(std::stoi(env));
    } catch (...) { /* keep default */ }
  }
}

std::string OllamaProvider::render_request_body(
    const ChatRequest&         req,
    const std::vector<Tool>&   tool_definitions) {
  std::string out;
  out += "{";
  out += "\"model\":";
  json_escape_append(out, req.model);
  out += ",\"stream\":false";

  // messages
  out += ",\"messages\":[";
  for (std::size_t i = 0; i < req.messages.size(); ++i) {
    if (i > 0) out += ',';
    const auto& m = req.messages[i];
    out += "{\"role\":";
    json_escape_append(out, to_string(m.role));
    out += ",\"content\":";
    json_escape_append(out, m.content);
    if (!m.tool_call_id.empty()) {
      out += ",\"tool_call_id\":";
      json_escape_append(out, m.tool_call_id);
    }
    out += "}";
  }
  out += "]";

  // tools
  if (!tool_definitions.empty()) {
    out += ",\"tools\":[";
    for (std::size_t i = 0; i < tool_definitions.size(); ++i) {
      if (i > 0) out += ',';
      const auto& t = tool_definitions[i];
      out += "{\"type\":\"function\",\"function\":{";
      out += "\"name\":";
      json_escape_append(out, t.name);
      out += ",\"description\":";
      json_escape_append(out, t.description);
      // Ollama expects a JSON Schema in `parameters`; the project's
      // v1 catalogue uses a free-form doc string per ADR-0001, so
      // emit an empty object schema. Sprint 12 swaps in real
      // JSON Schemas when the tool catalogue gains them.
      out += ",\"parameters\":{\"type\":\"object\",\"properties\":{}}";
      out += "}}";
    }
    out += "]";
  }

  // sampling
  if (req.temperature || req.max_tokens) {
    out += ",\"options\":{";
    bool first = true;
    if (req.temperature) {
      out += "\"temperature\":";
      out += std::to_string(*req.temperature);
      first = false;
    }
    if (req.max_tokens) {
      if (!first) out += ',';
      out += "\"num_predict\":";
      out += std::to_string(*req.max_tokens);
    }
    out += "}";
  }

  out += "}";
  return out;
}

ChatResult OllamaProvider::parse_response_body(std::string_view body) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(body));
  } catch (const std::exception& e) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         std::string("yaml-cpp: ") + e.what()};
  }
  if (!root || !root.IsMap()) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         "ollama: top-level is not an object"};
  }

  // Typed error body? Ollama returns 200 with { "error": "..." } when
  // the model isn't installed.
  if (root["error"] && root["error"].IsScalar()) {
    const auto msg = root["error"].as<std::string>();
    auto kind = ProviderErrorKind::ProviderHttpError;
    if (msg.find("not found") != std::string::npos) {
      kind = ProviderErrorKind::ModelNotFound;
    }
    return ProviderError{kind, "ollama: " + msg};
  }

  ChatResponse r;
  if (auto m = root["message"]) {
    if (m["content"] && m["content"].IsScalar()) {
      r.text = m["content"].as<std::string>();
    }
    if (auto calls = m["tool_calls"]; calls && calls.IsSequence()) {
      for (std::size_t i = 0; i < calls.size(); ++i) {
        ToolCall tc;
        if (calls[i]["id"]) {
          tc.id = calls[i]["id"].as<std::string>();
        } else {
          tc.id = "call_" + std::to_string(i);
        }
        if (auto fn = calls[i]["function"]) {
          if (fn["name"]) tc.name = fn["name"].as<std::string>();
          if (fn["arguments"]) {
            // arguments is either a string (already JSON) or an
            // embedded object; we want a string in either shape.
            if (fn["arguments"].IsScalar()) {
              tc.arguments_json = fn["arguments"].as<std::string>();
            } else {
              YAML::Emitter e;
              e.SetMapFormat(YAML::Flow);
              e << fn["arguments"];
              tc.arguments_json = e.c_str();
            }
          }
        }
        r.tool_calls.push_back(std::move(tc));
      }
    }
  }
  if (auto p = root["prompt_eval_count"];      p && p.IsScalar()) {
    try { r.input_tokens = p.as<std::uint64_t>(); } catch (...) {}
  }
  if (auto p = root["eval_count"];             p && p.IsScalar()) {
    try { r.output_tokens = p.as<std::uint64_t>(); } catch (...) {}
  }
  if (auto p = root["total_duration"];         p && p.IsScalar()) {
    try {
      // Ollama reports nanoseconds.
      const auto ns = p.as<std::uint64_t>();
      r.latency = std::chrono::milliseconds(ns / 1'000'000);
    } catch (...) {}
  }
  if (auto p = root["done"]; p && p.IsScalar() && p.as<bool>() == false) {
    r.truncated = true;
  }
  return r;
}

std::vector<std::string> OllamaProvider::available_models() const {
  // GET /api/tags returns the installed-model list. We invoke curl
  // and parse the same yaml-cpp way; failure returns an empty list
  // (the caller treats this as "daemon unreachable" implicitly).
  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary, "--silent", "--max-time",
            std::to_string(opts_.timeout.count()),
            "--fail",
            opts_.endpoint + "/api/tags"};
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  auto r = plugin::run_subprocess(o);
  if (!r.succeeded()) return {};
  try {
    auto root = YAML::Load(r.stdout_bytes);
    if (!root["models"] || !root["models"].IsSequence()) return {};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < root["models"].size(); ++i) {
      auto node = root["models"][i];
      if (node["name"] && node["name"].IsScalar()) {
        out.push_back(node["name"].as<std::string>());
      }
    }
    return out;
  } catch (...) {
    return {};
  }
}

ChatResult OllamaProvider::chat_completion(const ChatRequest& req) {
  if (req.model.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest,
                         "ollama: empty model name"};
  }
  if (req.messages.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest,
                         "ollama: empty messages"};
  }
  // We don't materialise the tool definitions here; the eval
  // runner is the typical caller and it has the registry, so it
  // pre-renders via render_request_body. As a direct-call
  // convenience this overload renders with an empty tool list.
  const std::string body = render_request_body(req, {});

  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary,
            "--silent",
            "--max-time", std::to_string(opts_.timeout.count()),
            "--fail-with-body",
            "-H", "Content-Type: application/json",
            "--data-binary", "@-",
            opts_.endpoint + "/api/chat"};
  o.stdin_bytes = body;
  o.timeout     = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  o.max_capture_bytes = 4 * 1024 * 1024;  // models can emit long replies

  const auto started = std::chrono::steady_clock::now();
  auto r = plugin::run_subprocess(o);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (!r.ok) {
    // The spawn failed (curl not found, fork denied). Distinct from
    // "curl ran and the daemon refused" — that returns a non-zero
    // exit code with a structured body below.
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         "ollama: curl spawn failed: " + r.error_message};
  }
  if (r.timed_out) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         "ollama: request timed out after " +
                             std::to_string(opts_.timeout.count()) + "s"};
  }
  if (r.exit_code == 7 || r.exit_code == 28) {
    // curl: 7 = couldn't connect; 28 = operation timeout. Both map
    // to "the local daemon isn't accepting connections."
    return ProviderError{ProviderErrorKind::LocalDaemonUnreachable,
                         "ollama: daemon unreachable at " + opts_.endpoint +
                             " (curl exit " + std::to_string(r.exit_code) + ")"};
  }
  if (r.exit_code != 0) {
    // --fail-with-body emits the response body before exiting non-zero
    // for any non-2xx response. The body usually carries Ollama's
    // typed { "error": ... } JSON, so try to parse it.
    auto parsed = parse_response_body(r.stdout_bytes);
    if (auto* err = std::get_if<ProviderError>(&parsed)) {
      return *err;
    }
    return ProviderError{ProviderErrorKind::ProviderHttpError,
                         "ollama: curl exit " + std::to_string(r.exit_code) +
                             "; body=" + r.stdout_bytes};
  }

  auto parsed = parse_response_body(r.stdout_bytes);
  if (auto* resp = std::get_if<ChatResponse>(&parsed)) {
    // Override the latency with our wall-clock measurement — Ollama's
    // total_duration field is the model's CPU time, not the round-trip
    // a caller would observe.
    resp->latency = elapsed;
  }
  return parsed;
}

}  // namespace souxmar::ai
