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
    case ChatMessage::Role::System:
      return "system";
    case ChatMessage::Role::User:
      return "user";
    case ChatMessage::Role::Assistant:
      return "assistant";
    case ChatMessage::Role::Tool:
      return "tool";
  }
  return "unknown";
}

std::string_view to_string(ProviderErrorKind k) noexcept {
  switch (k) {
    case ProviderErrorKind::ProviderHttpError:
      return "provider-http-error";
    case ProviderErrorKind::RateLimited:
      return "rate-limited";
    case ProviderErrorKind::LocalDaemonUnreachable:
      return "local-daemon-unreachable";
    case ProviderErrorKind::ModelNotFound:
      return "model-not-found";
    case ProviderErrorKind::HttpClientFailed:
      return "http-client-failed";
    case ProviderErrorKind::MalformedResponse:
      return "malformed-response";
    case ProviderErrorKind::ProtocolMismatch:
      return "protocol-mismatch";
    case ProviderErrorKind::BadRequest:
      return "bad-request";
    case ProviderErrorKind::ContextLengthExceeded:
      return "context-length-exceeded";
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
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(
              buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
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

void StubProvider::program_reply(std::string_view model,
                                 std::string_view trigger_substring,
                                 ChatResponse reply) {
  table_.push_back({std::string(model), std::string(trigger_substring), std::move(reply)});
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
    return ProviderError{ProviderErrorKind::BadRequest, "stub: empty messages"};
  }
  std::string_view last;
  for (auto it = req.messages.rbegin(); it != req.messages.rend(); ++it) {
    if (it->role == ChatMessage::Role::User) {
      last = it->content;
      break;
    }
  }
  for (const auto& p : table_) {
    if (p.model != req.model)
      continue;
    if (p.trigger.empty() || last.find(p.trigger) != std::string_view::npos) {
      return p.reply;
    }
  }
  return ProviderError{ProviderErrorKind::ProtocolMismatch,
                       "stub: no programmed reply matched (model='" + req.model + "')"};
}

// ---- OllamaProvider ----------------------------------------------------

OllamaProvider::OllamaProvider(OllamaProviderOptions opts) : opts_(std::move(opts)) {
  if (opts_.curl_binary.empty())
    opts_.curl_binary = "curl";
  if (const char* env = std::getenv("SOUXMAR_OLLAMA_TIMEOUT_SECS"); env && *env) {
    try {
      opts_.timeout = std::chrono::seconds(std::stoi(env));
    } catch (...) { /* keep default */
    }
  }
}

std::string OllamaProvider::render_request_body(const ChatRequest& req,
                                                const std::vector<Tool>& tool_definitions) {
  // Tools come from either the caller's explicit list (the historical
  // overload, still used by tests) or the request itself. Requests win
  // when both are present; in practice only one is ever set.
  std::vector<ToolDefinition> tools;
  tools.reserve(tool_definitions.size() + req.tools.size());
  for (const auto& t : tool_definitions)
    tools.push_back({t.name, t.description});
  for (const auto& t : req.tools)
    tools.push_back(t);
  std::string out;
  out += "{";
  out += "\"model\":";
  json_escape_append(out, req.model);
  out += ",\"stream\":false";

  // messages
  out += ",\"messages\":[";
  for (std::size_t i = 0; i < req.messages.size(); ++i) {
    if (i > 0)
      out += ',';
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
  if (!tools.empty()) {
    out += ",\"tools\":[";
    for (std::size_t i = 0; i < tools.size(); ++i) {
      if (i > 0)
        out += ',';
      const auto& t = tools[i];
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
      if (!first)
        out += ',';
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
          if (fn["name"])
            tc.name = fn["name"].as<std::string>();
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
  if (auto p = root["prompt_eval_count"]; p && p.IsScalar()) {
    try {
      r.input_tokens = p.as<std::uint64_t>();
    } catch (...) {}
  }
  if (auto p = root["eval_count"]; p && p.IsScalar()) {
    try {
      r.output_tokens = p.as<std::uint64_t>();
    } catch (...) {}
  }
  if (auto p = root["total_duration"]; p && p.IsScalar()) {
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
  o.argv = {opts_.curl_binary,
            "--silent",
            "--max-time",
            std::to_string(opts_.timeout.count()),
            "--fail",
            opts_.endpoint + "/api/tags"};
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  auto r = plugin::run_subprocess(o);
  if (!r.succeeded())
    return {};
  try {
    auto root = YAML::Load(r.stdout_bytes);
    if (!root["models"] || !root["models"].IsSequence())
      return {};
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
    return ProviderError{ProviderErrorKind::BadRequest, "ollama: empty model name"};
  }
  if (req.messages.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest, "ollama: empty messages"};
  }
  // Tools ride on the request now. This used to pass `{}`
  // unconditionally, so a local model was never offered a souxmar tool
  // and could only ever reply in prose — the reason the published
  // Ollama tool-calling results were not reproducible.
  const std::string body = render_request_body(req, {});

  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary,
            "--silent",
            "--max-time",
            std::to_string(opts_.timeout.count()),
            "--fail-with-body",
            "-H",
            "Content-Type: application/json",
            "--data-binary",
            "@-",
            opts_.endpoint + "/api/chat"};
  o.stdin_bytes = body;
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
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
    return ProviderError{
        ProviderErrorKind::HttpClientFailed,
        "ollama: request timed out after " + std::to_string(opts_.timeout.count()) + "s"};
  }
  if (r.exit_code == 7 || r.exit_code == 28) {
    // curl: 7 = couldn't connect; 28 = operation timeout. Both map
    // to "the local daemon isn't accepting connections."
    return ProviderError{ProviderErrorKind::LocalDaemonUnreachable,
                         "ollama: daemon unreachable at " + opts_.endpoint + " (curl exit "
                             + std::to_string(r.exit_code) + ")"};
  }
  if (r.exit_code != 0) {
    // --fail-with-body emits the response body before exiting non-zero
    // for any non-2xx response. The body usually carries Ollama's
    // typed { "error": ... } JSON, so try to parse it.
    auto parsed = parse_response_body(r.stdout_bytes);
    if (auto* err = std::get_if<ProviderError>(&parsed)) {
      return *err;
    }
    return ProviderError{
        ProviderErrorKind::ProviderHttpError,
        "ollama: curl exit " + std::to_string(r.exit_code) + "; body=" + r.stdout_bytes};
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

// ---- OpenAICompatibleProvider ------------------------------------------

const std::vector<OpenAICompatiblePreset>& openai_compatible_presets() noexcept {
  // Base URLs are each service's documented OpenAI-compatible root.
  // They are a convenience, not a contract: `base_url` in
  // project.ai.toml overrides any of them, which is the supported fix
  // if a service moves its endpoint.
  static const std::vector<OpenAICompatiblePreset> kPresets = {
      {"grok", "xAI (Grok)", "https://api.x.ai/v1", "XAI_API_KEY"},
      {"openai", "OpenAI", "https://api.openai.com/v1", "OPENAI_API_KEY"},
      {"deepseek", "DeepSeek", "https://api.deepseek.com/v1", "DEEPSEEK_API_KEY"},
      {"groq", "Groq", "https://api.groq.com/openai/v1", "GROQ_API_KEY"},
      {"mistral", "Mistral", "https://api.mistral.ai/v1", "MISTRAL_API_KEY"},
      {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY"},
      {"together", "Together AI", "https://api.together.xyz/v1", "TOGETHER_API_KEY"},
      // The escape hatch: any other service, or a local server. Requires
      // an explicit base_url.
      {"openai-compatible", "Other (OpenAI-compatible)", "", "SOUXMAR_AI_API_KEY"},
  };
  return kPresets;
}

const OpenAICompatiblePreset* find_openai_compatible_preset(std::string_view id) noexcept {
  for (const auto& p : openai_compatible_presets()) {
    if (p.id == id)
      return &p;
  }
  return nullptr;
}

namespace {

// Strip trailing '/' so base_url + "/chat/completions" is well-formed
// whether or not the user typed the slash.
std::string join_url(std::string_view base, std::string_view path) {
  std::string out(base);
  while (!out.empty() && out.back() == '/')
    out.pop_back();
  out += path;
  return out;
}

// Escape a value for curl's config-file grammar (`-K`). curl unescapes
// \\ \" \t \n \r \v inside a double-quoted value and takes any other
// backslash sequence literally, so escaping exactly backslash, quote
// and the three whitespace controls round-trips any byte string.
void curl_config_escape_append(std::string& out, std::string_view s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        out += c;
    }
  }
  out += '"';
}

// Marker used to recover the HTTP status from curl's --write-out
// without a second request. Chosen to be absent from any JSON body.
constexpr std::string_view kHttpStatusMarker = "\n__souxmar_http_status__:";

}  // namespace

std::string OpenAICompatibleProvider::render_curl_config(std::string_view url,
                                                         std::string_view api_key,
                                                         std::string_view json_body,
                                                         std::chrono::seconds timeout) {
  std::string cfg;
  cfg += "url = ";
  curl_config_escape_append(cfg, url);
  cfg += "\nrequest = \"POST\"\n";
  cfg += "silent\n";
  cfg += "show-error\n";
  // No --fail-with-body: these APIs return their most useful diagnostics
  // in the body of a 4xx, and we want to read it and map it to a typed
  // ProviderErrorKind rather than see only an exit code.
  cfg += "max-time = " + std::to_string(timeout.count()) + "\n";
  cfg += "header = \"Content-Type: application/json\"\n";
  if (!api_key.empty()) {
    // The reason this whole function exists: on stdin, so the
    // credential never reaches argv or the filesystem.
    cfg += "header = ";
    std::string header = "Authorization: Bearer ";
    header += api_key;
    curl_config_escape_append(cfg, header);
    cfg += "\n";
  }
  cfg += "write-out = ";
  curl_config_escape_append(cfg, std::string(kHttpStatusMarker) + "%{http_code}");
  cfg += "\n";
  cfg += "data-binary = ";
  curl_config_escape_append(cfg, json_body);
  cfg += "\n";
  return cfg;
}

std::string OpenAICompatibleProvider::render_request_body(
    const ChatRequest& req, const std::vector<Tool>& tool_definitions) {
  std::vector<ToolDefinition> tools;
  tools.reserve(tool_definitions.size() + req.tools.size());
  for (const auto& t : tool_definitions)
    tools.push_back({t.name, t.description});
  for (const auto& t : req.tools)
    tools.push_back(t);

  std::string out;
  out += "{\"model\":";
  json_escape_append(out, req.model);
  out += ",\"stream\":false";

  out += ",\"messages\":[";
  for (std::size_t i = 0; i < req.messages.size(); ++i) {
    if (i > 0)
      out += ',';
    const auto& m = req.messages[i];
    out += "{\"role\":";
    json_escape_append(out, to_string(m.role));
    out += ",\"content\":";
    json_escape_append(out, m.content);
    if (m.role == ChatMessage::Role::Tool && !m.tool_call_id.empty()) {
      out += ",\"tool_call_id\":";
      json_escape_append(out, m.tool_call_id);
    }
    // An assistant turn that called tools has to carry them, or the
    // `tool` messages that follow are orphaned and OpenAI rejects the
    // whole request ("messages with role 'tool' must be a response to a
    // preceding message with 'tool_calls'"). Local mocks accept the
    // malformed shape, which is how this went unnoticed.
    if (m.role == ChatMessage::Role::Assistant && !m.tool_calls.empty()) {
      out += ",\"tool_calls\":[";
      for (std::size_t j = 0; j < m.tool_calls.size(); ++j) {
        if (j > 0)
          out += ',';
        const auto& tc = m.tool_calls[j];
        out += "{\"id\":";
        json_escape_append(out, tc.id);
        out += ",\"type\":\"function\",\"function\":{\"name\":";
        json_escape_append(out, tc.name);
        out += ",\"arguments\":";
        // The spec wants a JSON-encoded *string* here, so the arguments
        // are escaped into one rather than inlined as an object.
        json_escape_append(out, tc.arguments_json.empty() ? "{}" : tc.arguments_json);
        out += "}}";
      }
      out += "]";
    }
    out += "}";
  }
  out += "]";

  if (!tools.empty()) {
    out += ",\"tools\":[";
    for (std::size_t i = 0; i < tools.size(); ++i) {
      if (i > 0)
        out += ',';
      const auto& t = tools[i];
      out += "{\"type\":\"function\",\"function\":{\"name\":";
      json_escape_append(out, t.name);
      out += ",\"description\":";
      json_escape_append(out, t.description);
      // Same limitation as the Ollama renderer: the v1 tool catalogue
      // documents its inputs in prose, not JSON Schema, so we advertise
      // a permissive object. A model can still call the tool; it just
      // gets no per-argument typing from us.
      out += ",\"parameters\":{\"type\":\"object\",\"properties\":{}}";
      out += "}}";
    }
    out += "]";
  }

  if (req.temperature) {
    out += ",\"temperature\":";
    out += std::to_string(*req.temperature);
  }
  if (req.max_tokens) {
    out += ",\"max_tokens\":";
    out += std::to_string(*req.max_tokens);
  }

  out += "}";
  return out;
}

ChatResult OpenAICompatibleProvider::parse_response_body(std::string_view body) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(body));
  } catch (const std::exception& e) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         std::string("openai-compatible: yaml-cpp: ") + e.what()};
  }
  if (!root || !root.IsMap()) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         "openai-compatible: top-level is not an object"};
  }

  // Typed error object. Shape: {"error":{"message":..,"type":..,"code":..}}
  if (auto err = root["error"]; err) {
    std::string msg;
    std::string type;
    try {
      if (err.IsScalar()) {
        msg = err.as<std::string>();
      } else if (err.IsMap()) {
        if (err["message"] && err["message"].IsScalar())
          msg = err["message"].as<std::string>();
        if (err["type"] && err["type"].IsScalar())
          type = err["type"].as<std::string>();
      }
    } catch (const std::exception&) {
      msg = "unparseable error object";
    }
    auto kind = ProviderErrorKind::ProviderHttpError;
    const auto haystack = msg + " " + type;
    if (haystack.find("rate limit") != std::string::npos
        || haystack.find("rate_limit") != std::string::npos) {
      kind = ProviderErrorKind::RateLimited;
    } else if (haystack.find("context length") != std::string::npos
               || haystack.find("context_length") != std::string::npos
               || haystack.find("maximum context") != std::string::npos) {
      kind = ProviderErrorKind::ContextLengthExceeded;
    } else if (haystack.find("does not exist") != std::string::npos
               || haystack.find("model_not_found") != std::string::npos
               || haystack.find("Incorrect model") != std::string::npos) {
      kind = ProviderErrorKind::ModelNotFound;
    }
    return ProviderError{kind, "openai-compatible: " + (msg.empty() ? "unspecified error" : msg)};
  }

  auto choices = root["choices"];
  if (!choices || !choices.IsSequence() || choices.size() == 0) {
    return ProviderError{ProviderErrorKind::ProtocolMismatch,
                         "openai-compatible: response has no `choices` array"};
  }

  ChatResponse r;
  try {
    auto first = choices[0];
    if (auto m = first["message"]; m && m.IsMap()) {
      if (m["content"] && m["content"].IsScalar()) {
        r.text = m["content"].as<std::string>();
      }
      if (auto calls = m["tool_calls"]; calls && calls.IsSequence()) {
        for (std::size_t i = 0; i < calls.size(); ++i) {
          ToolCall tc;
          if (calls[i]["id"] && calls[i]["id"].IsScalar()) {
            tc.id = calls[i]["id"].as<std::string>();
          } else {
            tc.id = "call_" + std::to_string(i);
          }
          if (auto fn = calls[i]["function"]; fn && fn.IsMap()) {
            if (fn["name"] && fn["name"].IsScalar())
              tc.name = fn["name"].as<std::string>();
            if (auto args = fn["arguments"]; args) {
              // Spec says a JSON-encoded string; several services send
              // an embedded object instead. Normalise to a string.
              if (args.IsScalar()) {
                tc.arguments_json = args.as<std::string>();
              } else {
                YAML::Emitter e;
                e.SetMapFormat(YAML::Flow);
                e << args;
                tc.arguments_json = e.c_str();
              }
            }
          }
          if (!tc.name.empty())
            r.tool_calls.push_back(std::move(tc));
        }
      }
    }
    if (auto fr = first["finish_reason"]; fr && fr.IsScalar()) {
      r.truncated = fr.as<std::string>() == "length";
    }
  } catch (const std::exception& e) {
    // Never let a yaml-cpp type error escape into the caller — the
    // eval runner and the C bridge both treat an exception here as
    // fatal, and a malformed reply from a third-party service is an
    // expected condition, not a crash.
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         std::string("openai-compatible: malformed choice: ") + e.what()};
  }

  if (auto usage = root["usage"]; usage && usage.IsMap()) {
    try {
      if (usage["prompt_tokens"] && usage["prompt_tokens"].IsScalar())
        r.input_tokens = usage["prompt_tokens"].as<std::uint64_t>();
      if (usage["completion_tokens"] && usage["completion_tokens"].IsScalar())
        r.output_tokens = usage["completion_tokens"].as<std::uint64_t>();
    } catch (const std::exception&) { /* counts are advisory */
    }
  }
  return r;
}

OpenAICompatibleProvider::OpenAICompatibleProvider(OpenAICompatibleOptions opts)
    : opts_(std::move(opts)) {
  if (opts_.curl_binary.empty())
    opts_.curl_binary = "curl";
  if (opts_.provider_id.empty())
    opts_.provider_id = "openai-compatible";
  name_ = opts_.provider_id;
  if (const char* env = std::getenv("SOUXMAR_HTTP_TIMEOUT_SECS"); env && *env) {
    try {
      opts_.timeout = std::chrono::seconds(std::stoi(env));
    } catch (...) { /* keep default */
    }
  }
}

std::vector<std::string> OpenAICompatibleProvider::available_models() const {
  if (opts_.base_url.empty())
    return {};
  std::string cfg;
  cfg += "url = ";
  curl_config_escape_append(cfg, join_url(opts_.base_url, "/models"));
  cfg += "\nsilent\nmax-time = " + std::to_string(opts_.timeout.count()) + "\n";
  if (!opts_.api_key.empty()) {
    cfg += "header = ";
    curl_config_escape_append(cfg, "Authorization: Bearer " + opts_.api_key);
    cfg += "\n";
  }

  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary, "-K", "-"};
  o.stdin_bytes = cfg;
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  o.max_capture_bytes = 1024 * 1024;

  auto r = plugin::run_subprocess(o);
  if (!r.ok || r.exit_code != 0)
    return {};

  std::vector<std::string> models;
  try {
    auto root = YAML::Load(r.stdout_bytes);
    if (auto data = root["data"]; data && data.IsSequence()) {
      for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i]["id"] && data[i]["id"].IsScalar())
          models.push_back(data[i]["id"].as<std::string>());
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return models;
}

ChatResult OpenAICompatibleProvider::chat_completion(const ChatRequest& req) {
  if (opts_.base_url.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest,
                         opts_.provider_id + ": no base_url configured"};
  }
  if (req.model.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest, opts_.provider_id + ": empty model name"};
  }
  if (req.messages.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest, opts_.provider_id + ": empty messages"};
  }

  const std::string body = render_request_body(req, opts_.tools);
  const std::string url = join_url(opts_.base_url, "/chat/completions");

  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary, "-K", "-"};
  o.stdin_bytes = render_curl_config(url, opts_.api_key, body, opts_.timeout);
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  o.max_capture_bytes = 4 * 1024 * 1024;

  const auto started = std::chrono::steady_clock::now();
  auto r = plugin::run_subprocess(o);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (!r.ok) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         opts_.provider_id + ": curl spawn failed: " + r.error_message};
  }
  if (r.timed_out) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         opts_.provider_id + ": request timed out after "
                             + std::to_string(opts_.timeout.count()) + "s"};
  }
  if (r.exit_code == 6 || r.exit_code == 7) {
    return ProviderError{ProviderErrorKind::LocalDaemonUnreachable,
                         opts_.provider_id + ": could not reach " + opts_.base_url + " (curl exit "
                             + std::to_string(r.exit_code) + ")"};
  }
  if (r.exit_code != 0) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         opts_.provider_id + ": curl exit " + std::to_string(r.exit_code) + ": "
                             + r.stderr_bytes};
  }

  // Split the status marker off the tail of the body.
  std::string payload = r.stdout_bytes;
  long http_status = 0;
  if (const auto pos = payload.rfind(kHttpStatusMarker); pos != std::string::npos) {
    try {
      http_status = std::stol(payload.substr(pos + kHttpStatusMarker.size()));
    } catch (...) { /* leave 0 */
    }
    payload.resize(pos);
  }

  // A 4xx/5xx usually carries a typed error body; prefer that wording.
  auto parsed = parse_response_body(payload);
  if (auto* err = std::get_if<ProviderError>(&parsed)) {
    if (http_status == 401 || http_status == 403) {
      return ProviderError{ProviderErrorKind::ProviderHttpError,
                           opts_.provider_id + ": authentication rejected (HTTP "
                               + std::to_string(http_status)
                               + "). Check the API key for this service. Detail: " + err->message};
    }
    if (http_status == 429) {
      return ProviderError{ProviderErrorKind::RateLimited,
                           opts_.provider_id + ": rate limited (HTTP 429). " + err->message};
    }
    if (http_status == 404) {
      return ProviderError{ProviderErrorKind::ModelNotFound,
                           opts_.provider_id + ": HTTP 404 from " + url
                               + " — check base_url and model. Detail: " + err->message};
    }
    return *err;
  }
  if (auto* resp = std::get_if<ChatResponse>(&parsed)) {
    resp->latency = elapsed;
  }
  return parsed;
}

// ---- AnthropicProvider -------------------------------------------------

namespace {

// Emit `{"type":"text","text":...}`. Anthropic rejects an empty text
// block, so callers check for content first.
void append_text_block(std::string& out, std::string_view text) {
  out += "{\"type\":\"text\",\"text\":";
  json_escape_append(out, text);
  out += "}";
}

// Arguments arrive as a JSON string. Anthropic wants `input` as a real
// object, so it is inlined verbatim rather than escaped. A model that
// emitted something unusable gets `{}` — the tool's own "missing
// required field" error is a better message than a 400 from the API.
void append_tool_use_block(std::string& out, const ToolCall& tc) {
  out += "{\"type\":\"tool_use\",\"id\":";
  json_escape_append(out, tc.id);
  out += ",\"name\":";
  json_escape_append(out, tc.name);
  out += ",\"input\":";
  const auto& a = tc.arguments_json;
  const auto first = a.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && (a[first] == '{' || a[first] == '[')) {
    out += a;
  } else {
    out += "{}";
  }
  out += "}";
}

}  // namespace

std::string AnthropicProvider::render_request_body(const ChatRequest& req,
                                                    const std::vector<Tool>& tool_definitions,
                                                    std::uint32_t default_max_tokens) {
  std::vector<ToolDefinition> tools;
  tools.reserve(tool_definitions.size() + req.tools.size());
  for (const auto& t : tool_definitions)
    tools.push_back({t.name, t.description});
  for (const auto& t : req.tools)
    tools.push_back(t);

  std::string out;
  out += "{\"model\":";
  json_escape_append(out, req.model);

  // Required by the API — omitting it is a 400, not a default.
  out += ",\"max_tokens\":";
  out += std::to_string(req.max_tokens ? *req.max_tokens : default_max_tokens);

  // System turns are hoisted to the top-level `system` field. Several
  // may arrive (a caller extending the default prompt); they join in
  // order rather than the last one silently winning.
  std::string system_text;
  for (const auto& m : req.messages) {
    if (m.role != ChatMessage::Role::System || m.content.empty())
      continue;
    if (!system_text.empty())
      system_text += "\n\n";
    system_text += m.content;
  }
  if (!system_text.empty()) {
    out += ",\"system\":";
    json_escape_append(out, system_text);
  }

  out += ",\"messages\":[";
  bool first_msg = true;
  for (std::size_t i = 0; i < req.messages.size(); ++i) {
    const auto& m = req.messages[i];
    if (m.role == ChatMessage::Role::System)
      continue;  // already hoisted

    if (m.role == ChatMessage::Role::Tool) {
      // A tool result is a *user* message holding a tool_result block.
      // Consecutive results from one assistant turn are merged into a
      // single user message, which is what the API expects when the
      // model made several calls at once.
      if (!first_msg)
        out += ',';
      first_msg = false;
      out += "{\"role\":\"user\",\"content\":[";
      bool first_block = true;
      std::size_t j = i;
      for (; j < req.messages.size() && req.messages[j].role == ChatMessage::Role::Tool; ++j) {
        if (!first_block)
          out += ',';
        first_block = false;
        out += "{\"type\":\"tool_result\",\"tool_use_id\":";
        json_escape_append(out, req.messages[j].tool_call_id);
        out += ",\"content\":";
        json_escape_append(out, req.messages[j].content);
        out += "}";
      }
      out += "]}";
      i = j - 1;  // the outer loop's ++i lands on the first unconsumed message
      continue;
    }

    const bool assistant = m.role == ChatMessage::Role::Assistant;
    // An assistant turn with neither text nor calls has nothing the API
    // will accept — an empty content array is a 400.
    if (assistant && m.content.empty() && m.tool_calls.empty())
      continue;

    if (!first_msg)
      out += ',';
    first_msg = false;
    out += "{\"role\":";
    json_escape_append(out, assistant ? "assistant" : "user");
    out += ",\"content\":[";
    bool first_block = true;
    if (!m.content.empty()) {
      append_text_block(out, m.content);
      first_block = false;
    }
    if (assistant) {
      for (const auto& tc : m.tool_calls) {
        if (!first_block)
          out += ',';
        first_block = false;
        append_tool_use_block(out, tc);
      }
    }
    // A user turn is never empty in practice, but an empty content
    // array is a hard 400, so guarantee at least one block.
    if (first_block)
      append_text_block(out, " ");
    out += "]}";
  }
  out += "]";

  if (!tools.empty()) {
    out += ",\"tools\":[";
    for (std::size_t i = 0; i < tools.size(); ++i) {
      if (i > 0)
        out += ',';
      out += "{\"name\":";
      json_escape_append(out, tools[i].name);
      out += ",\"description\":";
      json_escape_append(out, tools[i].description);
      // Same limitation as the other renderers: the v1 tool catalogue
      // documents its inputs in prose, so we advertise a permissive
      // object. The prose is appended to the description by the agent
      // loop, which is what actually tells the model the argument names.
      out += ",\"input_schema\":{\"type\":\"object\",\"properties\":{}}";
      out += "}";
    }
    out += "]";
  }

  if (req.temperature) {
    out += ",\"temperature\":";
    out += std::to_string(*req.temperature);
  }

  out += "}";
  return out;
}

ChatResult AnthropicProvider::parse_response_body(std::string_view body) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string(body));
  } catch (const std::exception& e) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         std::string("anthropic: yaml-cpp: ") + e.what()};
  }
  if (!root || !root.IsMap()) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         "anthropic: top-level is not an object"};
  }

  // Typed error: {"type":"error","error":{"type":..,"message":..}}
  if (auto err = root["error"]; err) {
    std::string msg;
    std::string type;
    try {
      if (err.IsScalar()) {
        msg = err.as<std::string>();
      } else if (err.IsMap()) {
        if (err["message"] && err["message"].IsScalar())
          msg = err["message"].as<std::string>();
        if (err["type"] && err["type"].IsScalar())
          type = err["type"].as<std::string>();
      }
    } catch (const std::exception&) {
      msg = "unparseable error object";
    }
    // Anthropic's error `type` is a stable enum, so it is matched
    // directly rather than by scanning prose.
    auto kind = ProviderErrorKind::ProviderHttpError;
    if (type == "rate_limit_error" || type == "overloaded_error") {
      kind = ProviderErrorKind::RateLimited;
    } else if (type == "not_found_error") {
      kind = ProviderErrorKind::ModelNotFound;
    } else if (type == "invalid_request_error") {
      kind = msg.find("context") != std::string::npos
                     || msg.find("max_tokens") != std::string::npos
                 ? ProviderErrorKind::ContextLengthExceeded
                 : ProviderErrorKind::BadRequest;
    }
    return ProviderError{kind, "anthropic: " + (msg.empty() ? "unspecified error" : msg)};
  }

  ChatResponse r;
  try {
    auto content = root["content"];
    if (!content || !content.IsSequence()) {
      return ProviderError{ProviderErrorKind::ProtocolMismatch,
                           "anthropic: response has no `content` array"};
    }
    for (std::size_t i = 0; i < content.size(); ++i) {
      auto block = content[i];
      if (!block || !block.IsMap())
        continue;
      const std::string type =
          block["type"] && block["type"].IsScalar() ? block["type"].as<std::string>() : "";
      if (type == "text") {
        if (block["text"] && block["text"].IsScalar()) {
          // Several text blocks concatenate — a model that reasons
          // aloud before calling a tool produces more than one.
          if (!r.text.empty())
            r.text += "\n";
          r.text += block["text"].as<std::string>();
        }
      } else if (type == "tool_use") {
        ToolCall tc;
        tc.id = block["id"] && block["id"].IsScalar() ? block["id"].as<std::string>()
                                                       : "toolu_" + std::to_string(i);
        if (block["name"] && block["name"].IsScalar())
          tc.name = block["name"].as<std::string>();
        if (auto input = block["input"]; input) {
          // `input` is an object; re-emit it as the JSON string the
          // dispatcher parses.
          YAML::Emitter e;
          e.SetMapFormat(YAML::Flow);
          e.SetSeqFormat(YAML::Flow);
          e << input;
          tc.arguments_json = e.c_str();
        }
        if (!tc.name.empty())
          r.tool_calls.push_back(std::move(tc));
      }
    }
    if (auto sr = root["stop_reason"]; sr && sr.IsScalar()) {
      r.truncated = sr.as<std::string>() == "max_tokens";
    }
  } catch (const std::exception& e) {
    return ProviderError{ProviderErrorKind::MalformedResponse,
                         std::string("anthropic: malformed content block: ") + e.what()};
  }

  if (auto usage = root["usage"]; usage && usage.IsMap()) {
    try {
      if (usage["input_tokens"] && usage["input_tokens"].IsScalar())
        r.input_tokens = usage["input_tokens"].as<std::uint64_t>();
      if (usage["output_tokens"] && usage["output_tokens"].IsScalar())
        r.output_tokens = usage["output_tokens"].as<std::uint64_t>();
    } catch (const std::exception&) { /* counts are advisory */
    }
  }
  return r;
}

std::string AnthropicProvider::render_curl_config(std::string_view url,
                                                   std::string_view api_key,
                                                   std::string_view api_version,
                                                   std::string_view json_body,
                                                   std::chrono::seconds timeout) {
  std::string cfg;
  cfg += "url = ";
  curl_config_escape_append(cfg, url);
  cfg += "\nrequest = \"POST\"\n";
  cfg += "silent\n";
  cfg += "show-error\n";
  cfg += "max-time = " + std::to_string(timeout.count()) + "\n";
  cfg += "header = \"Content-Type: application/json\"\n";
  cfg += "header = ";
  curl_config_escape_append(cfg, std::string("anthropic-version: ") + std::string(api_version));
  cfg += "\n";
  if (!api_key.empty()) {
    // On stdin, so the credential never reaches argv or disk.
    cfg += "header = ";
    curl_config_escape_append(cfg, std::string("x-api-key: ") + std::string(api_key));
    cfg += "\n";
  }
  cfg += "write-out = ";
  curl_config_escape_append(cfg, std::string(kHttpStatusMarker) + "%{http_code}");
  cfg += "\n";
  cfg += "data-binary = ";
  curl_config_escape_append(cfg, json_body);
  cfg += "\n";
  return cfg;
}

AnthropicProvider::AnthropicProvider(AnthropicProviderOptions opts) : opts_(std::move(opts)) {
  if (opts_.curl_binary.empty())
    opts_.curl_binary = "curl";
  if (opts_.base_url.empty())
    opts_.base_url = "https://api.anthropic.com/v1";
  if (opts_.api_version.empty())
    opts_.api_version = "2023-06-01";
  if (opts_.default_max_tokens == 0)
    opts_.default_max_tokens = 4096;
  if (const char* env = std::getenv("SOUXMAR_HTTP_TIMEOUT_SECS"); env && *env) {
    try {
      opts_.timeout = std::chrono::seconds(std::stoi(env));
    } catch (...) { /* keep default */
    }
  }
}

std::vector<std::string> AnthropicProvider::available_models() const {
  // Deliberately a static list. The Messages API has no model-listing
  // endpoint every account tier can reach, and returning an empty
  // vector — the contract OllamaProvider uses for "couldn't
  // enumerate" — would make `--list-models` useless. These are ids the
  // tool loop is known to work against; any other id the user's account
  // can reach is still accepted, since the id goes through untouched.
  return {
      "claude-opus-4-20250514",
      "claude-sonnet-4-20250514",
      "claude-3-7-sonnet-20250219",
      "claude-3-5-haiku-20241022",
  };
}

ChatResult AnthropicProvider::chat_completion(const ChatRequest& req) {
  if (req.model.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest, "anthropic: empty model name"};
  }
  if (req.messages.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest, "anthropic: empty messages"};
  }
  if (opts_.api_key.empty()) {
    return ProviderError{ProviderErrorKind::BadRequest,
                         "anthropic: no API key — set $ANTHROPIC_API_KEY"};
  }

  const std::string body = render_request_body(req, opts_.tools, opts_.default_max_tokens);
  const std::string url = join_url(opts_.base_url, "/messages");

  plugin::SubprocessOptions o;
  o.argv = {opts_.curl_binary, "-K", "-"};
  o.stdin_bytes = render_curl_config(url, opts_.api_key, opts_.api_version, body, opts_.timeout);
  o.timeout = std::chrono::milliseconds(opts_.timeout.count() * 1000);
  o.max_capture_bytes = 4 * 1024 * 1024;

  const auto started = std::chrono::steady_clock::now();
  auto r = plugin::run_subprocess(o);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (!r.ok) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         "anthropic: curl spawn failed: " + r.error_message};
  }
  if (r.timed_out) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         "anthropic: request timed out after "
                             + std::to_string(opts_.timeout.count()) + "s"};
  }
  if (r.exit_code == 6 || r.exit_code == 7) {
    return ProviderError{ProviderErrorKind::ProviderHttpError,
                         "anthropic: could not reach " + opts_.base_url + " (curl exit "
                             + std::to_string(r.exit_code) + ")"};
  }
  if (r.exit_code != 0) {
    return ProviderError{ProviderErrorKind::HttpClientFailed,
                         "anthropic: curl exit " + std::to_string(r.exit_code) + ": "
                             + r.stderr_bytes};
  }

  std::string payload = r.stdout_bytes;
  long http_status = 0;
  if (const auto pos = payload.rfind(kHttpStatusMarker); pos != std::string::npos) {
    try {
      http_status = std::stol(payload.substr(pos + kHttpStatusMarker.size()));
    } catch (...) { /* leave 0 */
    }
    payload.resize(pos);
  }

  auto parsed = parse_response_body(payload);
  if (auto* err = std::get_if<ProviderError>(&parsed)) {
    if (http_status == 401 || http_status == 403) {
      return ProviderError{ProviderErrorKind::ProviderHttpError,
                           "anthropic: authentication rejected (HTTP "
                               + std::to_string(http_status)
                               + "). Check $ANTHROPIC_API_KEY. Detail: " + err->message};
    }
    if (http_status == 429) {
      return ProviderError{ProviderErrorKind::RateLimited,
                           "anthropic: rate limited (HTTP 429). " + err->message};
    }
    if (http_status == 404) {
      return ProviderError{ProviderErrorKind::ModelNotFound,
                           "anthropic: HTTP 404 — check the model id. Detail: " + err->message};
    }
    return *err;
  }
  if (auto* resp = std::get_if<ChatResponse>(&parsed)) {
    resp->latency = elapsed;
  }
  return parsed;
}

}  // namespace souxmar::ai
