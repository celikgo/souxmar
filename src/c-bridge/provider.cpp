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

#include "souxmar/ai/agent.h"
#include "souxmar/ai/provider.h"
#include "souxmar/ai/provider_config.h"
#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"
#include "souxmar/version.h"

#include <map>
#include <memory>
#include <mutex>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <regex>
#include <string>
#include <variant>
#include <vector>

namespace {

std::string fmt_provider_not_yet_wired(int32_t kind) {
  switch (kind) {
    case SOUXMAR_BRIDGE_PROVIDER_OPENAI:
      // Reachable only if a config predates the loader change that
      // aliases `byok-openai` onto the `openai` preset.
      return "provider = \"byok-openai\" is the old spelling. Use "
             "provider = \"openai\" in project.ai.toml.";
    case SOUXMAR_BRIDGE_PROVIDER_MANAGED:
      return "The managed (subscription) provider is not reachable yet — "
             "the account portal that issues its tokens is not wired. Use your own key "
             "instead: set provider to \"anthropic\", \"openai\", \"grok\", or any other "
             "service in project.ai.toml, or \"ollama\" to run locally.";
    default:
      return "provider not yet wired";
  }
}

int32_t bridge_error_kind_for(souxmar::ai::ProviderErrorKind kind) {
  using K = souxmar::ai::ProviderErrorKind;
  switch (kind) {
    case K::ProviderHttpError:
      return SOUXMAR_BRIDGE_PE_HTTP_ERROR;
    case K::RateLimited:
      return SOUXMAR_BRIDGE_PE_RATE_LIMITED;
    case K::LocalDaemonUnreachable:
      return SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    case K::ModelNotFound:
      return SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    case K::HttpClientFailed:
      return SOUXMAR_BRIDGE_PE_INTERNAL;
    case K::MalformedResponse:
      return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::ProtocolMismatch:
      return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::BadRequest:
      return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
    case K::ContextLengthExceeded:
      return SOUXMAR_BRIDGE_PE_INVALID_RESPONSE;
  }
  return SOUXMAR_BRIDGE_PE_INTERNAL;
}

// Extract a "key": "string" value from a tiny JSON-ish blob. The
// bridge does not validate the JSON; it surfaces a bad-request
// error if a required field is missing. Sprint 15 push 1 swaps
// this for a real parser via the OpenAPI generator the proxy
// shares with us.
std::string extract_string_field(const std::string& json, const std::string& key) {
  std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
  std::smatch m;
  if (std::regex_search(json, m, re))
    return m[1].str();
  return {};
}

// Extract a list of "role"/"content" pairs from a `messages: [...]`
// block. Intentionally minimal — anything beyond simple role +
// content lands once the proxy's OpenAPI generator runs.
std::vector<souxmar::ai::ChatMessage> extract_messages(const std::string& json) {
  std::vector<souxmar::ai::ChatMessage> out;
  std::regex msg(
      "\\{\\s*\"role\"\\s*:\\s*\"([^\"]+)\"\\s*,"
      "\\s*\"content\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\}");
  auto begin = std::sregex_iterator(json.begin(), json.end(), msg);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    souxmar::ai::ChatMessage m;
    const std::string r = (*it)[1].str();
    if (r == "system")
      m.role = souxmar::ai::ChatMessage::Role::System;
    else if (r == "user")
      m.role = souxmar::ai::ChatMessage::Role::User;
    else if (r == "assistant")
      m.role = souxmar::ai::ChatMessage::Role::Assistant;
    else if (r == "tool")
      m.role = souxmar::ai::ChatMessage::Role::Tool;
    m.content = (*it)[2].str();
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace

struct souxmar_bridge_chat_tool_call_t {
  std::string name;
  std::string summary;
  bool ok = true;
  bool refused = false;
};

struct souxmar_bridge_chat_response_t {
  int32_t error_kind = SOUXMAR_BRIDGE_PE_OK;
  std::string error_text;
  std::string reply_text;
  int32_t provider = SOUXMAR_BRIDGE_PROVIDER_UNKNOWN;
  int64_t tokens_in = 0;
  int64_t tokens_out = 0;
  // The tools the turn ran, so the panel can show what happened to the
  // project rather than only the closing prose.
  std::vector<souxmar_bridge_chat_tool_call_t> tool_calls;
  // Set when the turn suspended on a confirmation.
  std::string pending_tool;
  std::string pending_arguments;
};

namespace {

// Resolve the per-project provider config. Sprint 15 push 2 reads
// `<project_dir>/project.ai.toml` per ADR-0020. project_id is
// interpreted as the project directory path; an empty / missing
// directory falls back to the default (StubProvider). A malformed
// config surfaces an error to the caller.
struct ResolvedProvider {
  int32_t bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;
  std::string model;
  std::string endpoint;
  // OpenAI-compatible only: which service, where, and the name of the
  // environment variable holding its key.
  std::string provider_id;
  std::string base_url;
  std::string api_key_env;
  // When non-empty, a config-parse error to surface back to the
  // caller (instead of attempting a stub call as a silent fallback).
  std::string config_error;
};

ResolvedProvider resolve_provider(const std::string& project_id) {
  ResolvedProvider rp;
  if (project_id.empty())
    return rp;

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
  rp.model = cfg.model;
  rp.endpoint = cfg.endpoint;
  using K = souxmar::ai::ProviderKind;
  switch (cfg.provider) {
    case K::Stub:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;
      break;
    case K::BYOKAnthropic:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC;
      rp.provider_id = cfg.provider_id;
      rp.base_url = cfg.base_url;
      rp.api_key_env = cfg.api_key_env;
      break;
    case K::BYOKOpenAI:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_OPENAI;
      break;
    case K::Ollama:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_OLLAMA;
      break;
    case K::Managed:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_MANAGED;
      break;
    case K::OpenAICompatible:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_OPENAI_COMPATIBLE;
      rp.provider_id = cfg.provider_id;
      rp.base_url = cfg.base_url;
      rp.api_key_env = cfg.api_key_env;
      break;
    default:
      rp.bridge_kind = SOUXMAR_BRIDGE_PROVIDER_STUB;
      break;
  }
  return rp;
}

}  // namespace

namespace {

// ---- Agent session ----------------------------------------------------
//
// A chat turn can pause on a confirmation, and the answer arrives in a
// later call from the UI thread. Everything the loop needs to carry on
// therefore has to outlive the call: the message history, and — more
// importantly — the ToolContext, which holds the mesh and field handles
// earlier tools produced. Rebuilding that per call would silently lose
// the session's work between "mesh it" and "now solve it".
//
// Keyed by project id. One session per project; starting a new turn
// replaces any suspended one, which is the same thing the user sees in
// the panel.
struct AgentSession {
  souxmar::plugin::Registry plugin_registry;
  std::vector<souxmar::plugin::LoadedPlugin> plugins;
  std::unique_ptr<souxmar::pipeline::RegistryDispatcher> dispatcher;
  souxmar::pipeline::Cache cache;
  souxmar::pipeline::Value session_state = souxmar::pipeline::Value::map({});
  souxmar::ai::ToolContext ctx;
  souxmar::ai::ConfirmationPolicy policy;
  souxmar::ai::ToolRegistry tools = souxmar::ai::default_v1_tools();

  std::vector<souxmar::ai::ChatMessage> history;
  souxmar::ai::PendingConfirmation pending;
  std::uint32_t steps_used = 0;
  bool suspended = false;

  AgentSession() {
    // Load every discoverable plugin so mesh / solve have a populated
    // registry. A failure to load one is not fatal to the session; the
    // tool that needed it reports the missing capability by name.
    souxmar::plugin::PluginLoader loader(plugin_registry,
                                         std::string{souxmar::version_string()});
    const auto report = souxmar::plugin::discover_plugins(souxmar::plugin::DiscoveryOptions{});
    for (const auto& d : report.loaded) {
      auto loaded = loader.load(d);
      if (std::holds_alternative<souxmar::plugin::LoadedPlugin>(loaded)) {
        plugins.push_back(std::move(std::get<souxmar::plugin::LoadedPlugin>(loaded)));
      }
    }
    dispatcher = std::make_unique<souxmar::pipeline::RegistryDispatcher>(plugin_registry);
    ctx.registry = &plugin_registry;
    ctx.dispatcher = dispatcher.get();
    ctx.cache = &cache;
    ctx.session_state = &session_state;
  }
};

std::mutex& sessions_mutex() {
  static std::mutex m;
  return m;
}

std::map<std::string, std::unique_ptr<AgentSession>>& sessions() {
  static std::map<std::string, std::unique_ptr<AgentSession>> map;
  return map;
}

// Build the Provider named by the resolved config. Returns nullptr and
// fills `error` when the configuration cannot produce one.
std::unique_ptr<souxmar::ai::Provider> provider_for(const ResolvedProvider& resolved,
                                                    std::string& error) {
  namespace ai = souxmar::ai;
  if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_OLLAMA) {
    ai::OllamaProviderOptions opts;
    if (!resolved.endpoint.empty())
      opts.endpoint = resolved.endpoint;
    return std::make_unique<ai::OllamaProvider>(std::move(opts));
  }
  if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_OPENAI_COMPATIBLE) {
    ai::OpenAICompatibleOptions opts;
    opts.provider_id = resolved.provider_id;
    opts.base_url = resolved.base_url;
    if (!resolved.api_key_env.empty()) {
      if (const char* key = std::getenv(resolved.api_key_env.c_str()); key && *key) {
        opts.api_key = key;
      }
    }
    const bool local = resolved.base_url.rfind("http://localhost", 0) == 0
                       || resolved.base_url.rfind("http://127.0.0.1", 0) == 0;
    if (opts.api_key.empty() && !resolved.api_key_env.empty() && !local) {
      error = "no API key for provider '" + resolved.provider_id + "': set $"
              + resolved.api_key_env + " in the environment souxmar runs in.";
      return nullptr;
    }
    return std::make_unique<ai::OpenAICompatibleProvider>(std::move(opts));
  }
  if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC) {
    ai::AnthropicProviderOptions opts;
    if (!resolved.base_url.empty())
      opts.base_url = resolved.base_url;
    const std::string env_name =
        resolved.api_key_env.empty() ? "ANTHROPIC_API_KEY" : resolved.api_key_env;
    if (const char* key = std::getenv(env_name.c_str()); key && *key) {
      opts.api_key = key;
    }
    if (opts.api_key.empty()) {
      error = "no API key for Anthropic: set $" + env_name
              + " in the environment souxmar runs in. The desktop app reads the key you "
                "saved during setup out of the OS keychain and sets this for you — if you "
                "are seeing this, re-run setup from Settings.";
      return nullptr;
    }
    return std::make_unique<ai::AnthropicProvider>(std::move(opts));
  }
  if (resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_OPENAI
      || resolved.bridge_kind == SOUXMAR_BRIDGE_PROVIDER_MANAGED) {
    error = fmt_provider_not_yet_wired(resolved.bridge_kind);
    return nullptr;
  }

  // Stub. Programmed with one honest catch-all so the panel exercises
  // the full path before a real provider is configured.
  auto stub = std::make_unique<ai::StubProvider>();
  ai::ChatResponse canned;
  canned.text =
      "souxmar stub provider — no real model is configured for this project, "
      "so this reply is generated locally and the request never left your "
      "machine. Set `provider` in project.ai.toml to talk to a real model.";
  stub->program_reply(resolved.model.empty() ? "stub-model" : resolved.model, "", canned);
  return stub;
}

// Copy a finished or suspended outcome onto the C response handle.
void fill_response(souxmar_bridge_chat_response_t* out,
                   const souxmar::ai::AgentOutcome& outcome) {
  namespace ai = souxmar::ai;
  for (const auto& step : outcome.steps) {
    for (const auto& call : step.tool_calls) {
      souxmar_bridge_chat_tool_call_t c;
      c.name = call.name;
      c.summary = call.result_summary;
      c.ok = call.ok;
      c.refused = call.refused;
      out->tool_calls.push_back(std::move(c));
    }
  }
  out->tokens_in = static_cast<int64_t>(outcome.input_tokens);
  out->tokens_out = static_cast<int64_t>(outcome.output_tokens);

  switch (outcome.stop_reason) {
    case ai::AgentStopReason::FinalAnswer:
      out->error_kind = SOUXMAR_BRIDGE_PE_OK;
      out->reply_text = outcome.final_text;
      break;
    case ai::AgentStopReason::AwaitingConfirmation:
      out->error_kind = SOUXMAR_BRIDGE_PE_AWAITING_CONFIRMATION;
      out->pending_tool = outcome.pending.tool_name;
      out->pending_arguments = outcome.pending.arguments_json;
      out->reply_text = outcome.final_text;
      break;
    case ai::AgentStopReason::MaxStepsReached:
      out->error_kind = SOUXMAR_BRIDGE_PE_OK;
      out->reply_text =
          outcome.final_text
          + (outcome.final_text.empty() ? "" : "\n\n")
          + "(stopped after the step limit with work still in progress — "
            "this answer is incomplete.)";
      break;
    case ai::AgentStopReason::ProviderFailed:
    case ai::AgentStopReason::Aborted:
      // Preserve the distinction the provider drew — a rate limit and a
      // missing key need different words in the panel.
      out->error_kind = bridge_error_kind_for(outcome.error_kind);
      out->error_text = outcome.error;
      break;
  }
}

}  // namespace

extern "C" souxmar_bridge_chat_response_t* souxmar_bridge_chat_send(const char* request_json,
                                                                    const char* project_id_c,
                                                                    char** out_err) {
  if (out_err)
    *out_err = nullptr;
  if (request_json == nullptr) {
    if (out_err) {
      const char* msg = "request_json is NULL";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err)
        std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }

  const std::string json(request_json);
  const std::string project_id = project_id_c ? std::string(project_id_c) : std::string{};

  // Sprint 15 push 2 — consult per-project config (ADR-0020).
  const auto resolved = resolve_provider(project_id);

  // Pull the bits we need from the request.
  const std::string model = extract_string_field(json, "model");
  const auto messages = extract_messages(json);

  souxmar::ai::ChatRequest req;
  req.model = !resolved.model.empty() ? resolved.model : (model.empty() ? "stub-model" : model);
  req.messages = messages;

  auto* out = new (std::nothrow) souxmar_bridge_chat_response_t;
  if (out == nullptr) {
    if (out_err) {
      const char* msg = "bridge: out of memory";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err)
        std::memcpy(*out_err, msg, std::strlen(msg) + 1);
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

  // Run a full agent turn: the model gets the tool catalogue, its calls
  // are dispatched against this project's engine session, and results go
  // back until it answers or asks for something needing approval.
  std::string provider_error;
  auto provider = provider_for(resolved, provider_error);
  if (!provider) {
    out->error_kind = SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    out->error_text = provider_error;
    return out;
  }

  std::lock_guard<std::mutex> lock(sessions_mutex());
  auto& slot = sessions()[project_id];
  // A new turn discards any half-finished one — the panel shows the same.
  slot = std::make_unique<AgentSession>();
  AgentSession& session = *slot;

  souxmar::ai::AgentOptions options;
  options.model = req.model;
  options.max_steps = 8;
  // The desktop cannot answer a blocking prompt from inside this call,
  // so the loop suspends and the panel asks.
  options.suspend_on_confirmation = true;

  const auto outcome = souxmar::ai::run_agent_turn(
      *provider, session.tools, session.ctx, session.policy, req.messages, options);

  fill_response(out, outcome);
  if (outcome.stop_reason == souxmar::ai::AgentStopReason::AwaitingConfirmation) {
    session.history = outcome.history;
    session.pending = outcome.pending;
    session.steps_used = outcome.steps_used;
    session.suspended = true;
  } else {
    sessions().erase(project_id);
  }
  return out;
}

extern "C" souxmar_bridge_chat_response_t* souxmar_bridge_chat_confirm(const char* project_id_c,
                                                                      int32_t allow,
                                                                      char** out_err) {
  if (out_err)
    *out_err = nullptr;
  const std::string project_id = project_id_c ? std::string(project_id_c) : std::string{};

  std::lock_guard<std::mutex> lock(sessions_mutex());
  auto it = sessions().find(project_id);
  if (it == sessions().end() || !it->second || !it->second->suspended) {
    if (out_err) {
      const char* msg = "no suspended agent turn for this project";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err)
        std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }
  AgentSession& session = *it->second;

  const auto resolved = resolve_provider(project_id);
  std::string provider_error;
  auto provider = provider_for(resolved, provider_error);

  auto* out = new (std::nothrow) souxmar_bridge_chat_response_t;
  if (out == nullptr) {
    if (out_err) {
      const char* msg = "bridge: out of memory";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err)
        std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }
  out->provider = resolved.bridge_kind;
  if (!provider) {
    out->error_kind = SOUXMAR_BRIDGE_PE_NOT_CONFIGURED;
    out->error_text = provider_error;
    sessions().erase(project_id);
    return out;
  }

  souxmar::ai::AgentOptions options;
  options.model = resolved.model.empty() ? "stub-model" : resolved.model;
  options.max_steps = 8;
  options.suspend_on_confirmation = true;
  options.steps_already_used = session.steps_used;

  const auto outcome = souxmar::ai::resume_agent_turn(*provider,
                                                      session.tools,
                                                      session.ctx,
                                                      session.policy,
                                                      session.history,
                                                      session.pending,
                                                      allow != 0,
                                                      options);
  fill_response(out, outcome);
  if (outcome.stop_reason == souxmar::ai::AgentStopReason::AwaitingConfirmation) {
    session.history = outcome.history;
    session.pending = outcome.pending;
    session.steps_used = outcome.steps_used;
    session.suspended = true;
  } else {
    sessions().erase(project_id);
  }
  return out;
}

extern "C" int32_t souxmar_bridge_chat_tool_call_count(const souxmar_bridge_chat_response_t* r) {
  return r ? static_cast<int32_t>(r->tool_calls.size()) : 0;
}

extern "C" const char* souxmar_bridge_chat_tool_call_name(const souxmar_bridge_chat_response_t* r,
                                                          int32_t index) {
  if (r == nullptr || index < 0 || static_cast<std::size_t>(index) >= r->tool_calls.size())
    return "";
  return r->tool_calls[static_cast<std::size_t>(index)].name.c_str();
}

extern "C" const char* souxmar_bridge_chat_tool_call_summary(
    const souxmar_bridge_chat_response_t* r, int32_t index) {
  if (r == nullptr || index < 0 || static_cast<std::size_t>(index) >= r->tool_calls.size())
    return "";
  return r->tool_calls[static_cast<std::size_t>(index)].summary.c_str();
}

extern "C" int32_t souxmar_bridge_chat_tool_call_ok(const souxmar_bridge_chat_response_t* r,
                                                    int32_t index) {
  if (r == nullptr || index < 0 || static_cast<std::size_t>(index) >= r->tool_calls.size())
    return 0;
  return r->tool_calls[static_cast<std::size_t>(index)].ok ? 1 : 0;
}

extern "C" int32_t souxmar_bridge_chat_tool_call_refused(const souxmar_bridge_chat_response_t* r,
                                                         int32_t index) {
  if (r == nullptr || index < 0 || static_cast<std::size_t>(index) >= r->tool_calls.size())
    return 0;
  return r->tool_calls[static_cast<std::size_t>(index)].refused ? 1 : 0;
}

extern "C" const char* souxmar_bridge_chat_pending_tool(const souxmar_bridge_chat_response_t* r) {
  return r ? r->pending_tool.c_str() : "";
}

extern "C" const char* souxmar_bridge_chat_pending_arguments(
    const souxmar_bridge_chat_response_t* r) {
  return r ? r->pending_arguments.c_str() : "";
}

extern "C" int32_t souxmar_bridge_chat_error_kind(const souxmar_bridge_chat_response_t* r) {
  return r ? r->error_kind : SOUXMAR_BRIDGE_PE_INTERNAL;
}

extern "C" const char* souxmar_bridge_chat_error_text(const souxmar_bridge_chat_response_t* r) {
  return r ? r->error_text.c_str() : "";
}

extern "C" const char* souxmar_bridge_chat_reply_text(const souxmar_bridge_chat_response_t* r) {
  return r ? r->reply_text.c_str() : "";
}

extern "C" int32_t souxmar_bridge_chat_provider(const souxmar_bridge_chat_response_t* r) {
  return r ? r->provider : SOUXMAR_BRIDGE_PROVIDER_UNKNOWN;
}

extern "C" int64_t souxmar_bridge_chat_tokens_in(const souxmar_bridge_chat_response_t* r) {
  return r ? r->tokens_in : 0;
}

extern "C" int64_t souxmar_bridge_chat_tokens_out(const souxmar_bridge_chat_response_t* r) {
  return r ? r->tokens_out : 0;
}

extern "C" void souxmar_bridge_chat_response_free(souxmar_bridge_chat_response_t* r) {
  delete r;
}
