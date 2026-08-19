// SPDX-License-Identifier: Apache-2.0
//
// AI provider abstraction.
//
// Sprint 10 push 9. Until this push, every interaction with an LLM
// happened through the eval suite's *scripted* mode — the runner
// played the agent role itself, calling tools directly. That gave us
// the deterministic regression gate (R-007 closed at Sprint 9
// push 10) but did not exercise the full "model emits a tool call,
// dispatcher executes it, output goes back to the model" loop.
//
// This header introduces the Provider abstraction the LLM-driven
// eval mode + the desktop app + the Pro-tier managed-AI proxy all
// share. The contract is intentionally narrow: one synchronous
// chat_completion() call returning either a typed Response or a
// typed ProviderError. Streaming + tool-result interleaving land
// in Sprint 11's "agent eval suite v2" alongside the dogfood week.
//
// The three Sprint 10 implementations of this interface are:
//   * StubProvider     — canned replies for unit + integration tests.
//                        Always available; no dependencies.
//   * OllamaProvider   — POST to http://localhost:11434/api/chat via
//                        curl-as-subprocess (no in-process HTTP
//                        client dep until Sprint 11). Verifies
//                        function-calling on Llama-3.x, Qwen-2.x,
//                        Mistral-Nemo (docs/ai-providers/
//                        ollama-compatibility.md).
//
// Since then two more landed, and between them they cover every
// service a user is likely to bring:
//
//   * OpenAICompatibleProvider — OpenAI, xAI (Grok), DeepSeek, Groq,
//                        Mistral, OpenRouter, Together, and any local
//                        server speaking /chat/completions.
//   * AnthropicProvider — Claude, over the Messages API.
//
// Why this isn't a v1 frozen surface (yet): the function-calling
// payload shapes vary across providers in ways that surface in the
// type system. Ollama's tools field is OpenAI-compatible
// {"type":"function","function":{...}}; Anthropic's tools field is
// {"name":..., "input_schema":...}. We elide the difference here
// (souxmar's tool catalogue is the single source of truth, and the
// provider knows how to render it). A frozen-surface ADR is queued
// for Sprint 12.

#pragma once

#include "souxmar/ai/tool.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::ai {

// A tool call emitted by the assistant. The dispatcher resolves
// `name` against the tool registry and parses `arguments_json`
// against the tool's input schema. Multiple tool calls per
// assistant turn are allowed.
struct ToolCall {
  std::string id;              // provider-assigned identifier
  std::string name;            // matches Tool::name
  std::string arguments_json;  // raw JSON; dispatcher parses
};

// One turn in a chat history. Order is preserved; the provider sends
// every message it receives.
struct ChatMessage {
  enum class Role : std::uint8_t {
    System = 0,
    User = 1,
    Assistant = 2,
    Tool = 3,  // tool-result message, replying to a prior tool call
  };
  Role role = Role::User;
  std::string content;
  // For Role::Tool: the id of the tool call this message responds to.
  // Empty for non-Tool roles.
  std::string tool_call_id;
  // For Role::Assistant: the calls this turn emitted, echoed back on
  // the next request.
  //
  // This is not bookkeeping — both major APIs reject a history without
  // it. OpenAI requires that a `tool` message be preceded by an
  // assistant message carrying the matching `tool_calls` entry;
  // Anthropic requires a `tool_use` block whose id the `tool_result`
  // refers to. The loop previously dropped the calls when appending the
  // assistant turn, so any conversation that ran a tool and came back
  // for a second step was malformed on the wire. It survived testing
  // only because the stub and a permissive mock never checked.
  std::vector<ToolCall> tool_calls;
};

[[nodiscard]] std::string_view to_string(ChatMessage::Role) noexcept;

// Response from one chat_completion call. Either the assistant
// produced a text reply (`text` non-empty, `tool_calls` empty), one
// or more tool calls (`text` may be empty), or both (some providers
// emit a "thinking" prefix alongside the call list).
struct ChatResponse {
  std::string text;
  std::vector<ToolCall> tool_calls;
  // Provider-reported token counts; zero when the provider doesn't
  // expose them (Ollama, today).
  std::uint64_t input_tokens = 0;
  std::uint64_t output_tokens = 0;
  // Wall-clock latency the *provider* took. The eval-suite's
  // dispatcher records this end-to-end for the latency gate.
  std::chrono::milliseconds latency{0};
  // True iff the response was truncated (max-tokens reached).
  bool truncated = false;
};

enum class ProviderErrorKind : std::uint8_t {
  // Provider returned a non-2xx HTTP status that wasn't a typed
  // rate-limit. The detail string carries the body.
  ProviderHttpError = 0,
  RateLimited = 1,
  // The local Ollama daemon refused the connection (not running).
  LocalDaemonUnreachable = 2,
  // The model name doesn't exist in the daemon's library; Ollama
  // returns this as a 404 with a typed body.
  ModelNotFound = 3,
  // The HTTP client (curl subprocess) failed to spawn or returned
  // a non-zero exit unrelated to the HTTP status. Diagnostic only.
  HttpClientFailed = 4,
  // The provider's response body couldn't be parsed (malformed
  // JSON, missing required field).
  MalformedResponse = 5,
  // The provider returned a response shape we don't know how to
  // map onto ChatResponse (e.g. a model that doesn't speak the
  // function-calling protocol Ollama exposes).
  ProtocolMismatch = 6,
  // The caller passed a request the provider couldn't honour
  // (empty messages, contradictory tool schemas, etc.).
  BadRequest = 7,
  // The request exceeded the provider's context limit.
  ContextLengthExceeded = 8,
};

[[nodiscard]] std::string_view to_string(ProviderErrorKind) noexcept;

struct ProviderError {
  ProviderErrorKind kind = ProviderErrorKind::ProviderHttpError;
  std::string message;
};

using ChatResult = std::variant<ChatResponse, ProviderError>;

// A tool as advertised to the model. Deliberately just the two fields a
// provider puts on the wire — carrying the full `Tool` here would drag
// its std::function handler through every request copy, and a provider
// has no business holding a callable it could invoke.
struct ToolDefinition {
  std::string name;
  std::string description;
};

// One inference request. The provider receives:
//   * the chat history (system/user/assistant/tool turns)
//   * the tool catalogue rendered from the souxmar tool registry
//   * optional sampling knobs (temperature, max_tokens, etc.).
//
// The provider does NOT see the user's BYOK secret directly — that
// is the integration code's responsibility. Each concrete
// Provider's constructor takes whatever credential / endpoint
// material it needs.
struct ChatRequest {
  std::string model;  // provider-specific model id
  std::vector<ChatMessage> messages;
  // Tool catalogue to advertise. Providers render this into their own
  // wire shape; an empty vector means "send no tools", which is the
  // correct request for a turn that must not produce a tool call.
  //
  // This replaced `tool_names`, which every provider ignored: the field
  // was populated by the eval runner and read by nobody, so no model
  // could ever be offered a souxmar tool. `tool_names` is kept below
  // only so existing callers still compile; it is not sent.
  std::vector<ToolDefinition> tools;
  // Deprecated. Populated by older callers; providers do not read it.
  std::vector<std::string> tool_names;
  // Sampling knobs. Providers that don't support a field clamp
  // silently.
  std::optional<double> temperature;
  std::optional<std::uint32_t> max_tokens;
};

// The provider interface. Implementations are not required to be
// thread-safe; the caller (eval runner, desktop app) is responsible
// for serialising chat_completion calls per Provider instance.
class Provider {
 public:
  virtual ~Provider() = default;

  // Identifier (e.g. "ollama", "anthropic", "stub"). Surfaced in
  // logs and in the compatibility-matrix doc generator.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // The set of model ids this provider recognises *as configured*.
  // For Ollama this lists the locally-installed models; for
  // StubProvider, the canned-reply table's keys. May be empty if
  // the provider couldn't enumerate (daemon unreachable, etc.) —
  // in which case `chat_completion()` would also fail with
  // LocalDaemonUnreachable. Used by `souxmar-eval --provider X
  // --list-models`.
  [[nodiscard]] virtual std::vector<std::string> available_models() const = 0;

  // Run one inference. Synchronous.
  [[nodiscard]] virtual ChatResult chat_completion(const ChatRequest& req) = 0;
};

// ---- StubProvider ----------------------------------------------------
//
// Test-only provider. Built from a programmed reply table: the eval
// runner's stub mode looks up the (model, last-user-message) tuple
// and returns the configured ChatResponse. Used to exercise the
// LLM-driven eval pipeline end-to-end on CI without a real model.

class StubProvider final : public Provider {
 public:
  StubProvider();

  void program_reply(std::string_view model,
                     std::string_view trigger_substring,
                     ChatResponse reply);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "stub";
  }

  [[nodiscard]] std::vector<std::string> available_models() const override;
  [[nodiscard]] ChatResult chat_completion(const ChatRequest& req) override;

 private:
  struct Programmed {
    std::string model;
    std::string trigger;
    ChatResponse reply;
  };

  std::vector<Programmed> table_;
};

// ---- OllamaProvider --------------------------------------------------
//
// Talks to a local Ollama daemon (default http://localhost:11434)
// by shelling out to `curl`. Subprocess-based for two reasons:
//   1. We don't link an in-process HTTP client (libcurl/cpprestsdk)
//      from libsouxmar-ai — keeps the dep tree small.
//   2. The OpenFOAM adapter (Sprint 8 push 2) already validated
//      the subprocess-harness pattern for a "local daemon you
//      expect on $PATH" use case.

struct OllamaProviderOptions {
  // HTTP endpoint of the Ollama daemon. Caller provides; the
  // shell-out flow concatenates with /api/chat.
  std::string endpoint = "http://localhost:11434";
  // Per-call timeout. Local inference on smaller hardware can take
  // tens of seconds for the first token; 120s is a comfortable
  // default. Override via SOUXMAR_OLLAMA_TIMEOUT_SECS.
  std::chrono::seconds timeout{120};
  // Path to the curl binary. Defaults to `curl` resolved via $PATH.
  std::string curl_binary;
};

class OllamaProvider final : public Provider {
 public:
  explicit OllamaProvider(OllamaProviderOptions opts = {});

  [[nodiscard]] std::string_view name() const noexcept override {
    return "ollama";
  }

  [[nodiscard]] std::vector<std::string> available_models() const override;
  [[nodiscard]] ChatResult chat_completion(const ChatRequest& req) override;

  // Render a ChatRequest to the JSON body Ollama's /api/chat
  // endpoint expects. Exposed for unit-testing the encoder without
  // a live daemon.
  [[nodiscard]] static std::string render_request_body(const ChatRequest& req,
                                                       const std::vector<Tool>& tool_definitions);

  // Parse Ollama's /api/chat JSON response into a ChatResponse.
  // Returns ProviderError on malformed input.
  [[nodiscard]] static ChatResult parse_response_body(std::string_view body);

 private:
  OllamaProviderOptions opts_;
};

// ---- OpenAICompatibleProvider -----------------------------------------
//
// One implementation for every service that speaks OpenAI's
// `POST {base_url}/chat/completions` shape — which, as of this writing,
// is most of them: xAI (Grok), OpenAI, DeepSeek, Groq, Mistral,
// OpenRouter, Together, and every local server that advertises an
// "OpenAI-compatible endpoint" (LM Studio, vLLM, llama.cpp's server,
// text-generation-webui).
//
// The point of the class is that connecting a new service is
// configuration, not code: a base URL, a model id, and an API key. The
// named presets below exist only so the common services can be selected
// by name; `base_url` always wins when set, so a service we have never
// heard of is still reachable without a patch.
//
// Anthropic is deliberately NOT covered here — its Messages API uses a
// different request and tool shape (`input_schema`, top-level `system`,
// `x-api-key` auth) and pretending otherwise would produce confusing
// 400s. It has its own provider, AnthropicProvider, at the bottom of
// this header.
//
// Transport is curl-as-subprocess for the same reasons as
// OllamaProvider. The credential is passed to curl through a config on
// *stdin* rather than as an argv element, so it never appears in `ps`
// output or /proc/<pid>/cmdline, and never touches disk.

// A known service, so `provider = "grok"` needs no base_url.
struct OpenAICompatiblePreset {
  std::string_view id;        // config value, e.g. "grok"
  std::string_view display;   // human label for the UI, e.g. "xAI (Grok)"
  std::string_view base_url;  // no trailing slash
  std::string_view key_env;   // conventional env var for this service
};

// The preset table. Ordered for display; `openai-compatible` is last
// because it is the escape hatch rather than a service.
[[nodiscard]] const std::vector<OpenAICompatiblePreset>& openai_compatible_presets() noexcept;

// Look a preset up by id. Returns nullptr when the id is not a preset —
// which is not an error, it just means the caller must supply base_url.
[[nodiscard]] const OpenAICompatiblePreset* find_openai_compatible_preset(
    std::string_view id) noexcept;

struct OpenAICompatibleOptions {
  // Identifier reported by name() and used in error messages. Usually
  // the preset id ("grok"); free-form for a self-hosted endpoint.
  std::string provider_id = "openai-compatible";
  // Root of the API, no trailing slash. "/chat/completions" is appended.
  std::string base_url;
  // Bearer credential. May be empty for local servers that don't
  // authenticate (LM Studio, llama.cpp).
  std::string api_key;
  // Tool catalogue to advertise. When empty no `tools` key is sent —
  // which is the only correct behaviour for a request that should not
  // elicit a tool call, and is why this is explicit rather than
  // implicit: a provider that silently drops the caller's tools looks
  // like a model that refuses to use them.
  std::vector<Tool> tools;
  std::chrono::seconds timeout{120};
  std::string curl_binary;
};

class OpenAICompatibleProvider final : public Provider {
 public:
  explicit OpenAICompatibleProvider(OpenAICompatibleOptions opts);

  [[nodiscard]] std::string_view name() const noexcept override {
    return name_;
  }

  // These services expose a model list at GET {base_url}/models. A
  // failure returns an empty vector rather than throwing — same
  // contract as OllamaProvider.
  [[nodiscard]] std::vector<std::string> available_models() const override;
  [[nodiscard]] ChatResult chat_completion(const ChatRequest& req) override;

  // Render the JSON body for POST {base_url}/chat/completions.
  // Exposed for unit tests.
  [[nodiscard]] static std::string render_request_body(const ChatRequest& req,
                                                       const std::vector<Tool>& tool_definitions);

  // Parse an OpenAI-shaped chat-completions response.
  [[nodiscard]] static ChatResult parse_response_body(std::string_view body);

  // Render the curl config fed to `curl -K -`. Keeping the credential
  // out of argv is the whole reason this exists, so it is a named,
  // testable function rather than an inline string build.
  [[nodiscard]] static std::string render_curl_config(std::string_view url,
                                                      std::string_view api_key,
                                                      std::string_view json_body,
                                                      std::chrono::seconds timeout);

 private:
  OpenAICompatibleOptions opts_;
  std::string name_;
};

// ---- AnthropicProvider -------------------------------------------------
//
// Claude, over the Messages API. This is a separate class rather than a
// preset on OpenAICompatibleProvider because the wire shape genuinely
// differs in four ways that cannot be papered over:
//
//   1. Auth is `x-api-key`, not `Authorization: Bearer`, and a version
//      header (`anthropic-version`) is mandatory.
//   2. The system prompt is a top-level `system` field, not a message
//      with role=system. Sending it as a message is a 400.
//   3. `max_tokens` is required, not optional. Omitting it is a 400.
//   4. Tool calls and their results are content *blocks* inside user /
//      assistant messages (`tool_use` / `tool_result`), not a parallel
//      `tool_calls` array plus a `tool` role. Tools are advertised with
//      `input_schema`, not `function.parameters`.
//
// Transport is curl-as-subprocess with the credential on stdin, exactly
// as OpenAICompatibleProvider does it and for the same reason: the key
// never reaches argv or the filesystem.

struct AnthropicProviderOptions {
  // API root, no trailing slash. "/messages" is appended. Overridable
  // so a gateway or a regional endpoint can be pointed at.
  std::string base_url = "https://api.anthropic.com/v1";
  std::string api_key;
  // Sent as the `anthropic-version` header. Pinned rather than tracking
  // "latest" so a server-side default change cannot alter our parse.
  std::string api_version = "2023-06-01";
  // Required by the API. Applied when ChatRequest::max_tokens is unset,
  // so a caller that never thought about it still gets a valid request.
  std::uint32_t default_max_tokens = 4096;
  std::vector<Tool> tools;
  std::chrono::seconds timeout{120};
  std::string curl_binary;
};

class AnthropicProvider final : public Provider {
 public:
  explicit AnthropicProvider(AnthropicProviderOptions opts);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "anthropic";
  }

  // The Messages API has no model-list endpoint that is stable across
  // account tiers, so this returns the ids we know the tool loop works
  // against rather than pretending to enumerate. Empty is never
  // returned, which keeps `--list-models` useful offline.
  [[nodiscard]] std::vector<std::string> available_models() const override;
  [[nodiscard]] ChatResult chat_completion(const ChatRequest& req) override;

  // Render the JSON body for POST {base_url}/messages. Exposed for unit
  // tests — the block-rebuilding logic is the part most likely to break.
  [[nodiscard]] static std::string render_request_body(const ChatRequest& req,
                                                       const std::vector<Tool>& tool_definitions,
                                                       std::uint32_t default_max_tokens);

  // Parse a Messages API response into a ChatResponse.
  [[nodiscard]] static ChatResult parse_response_body(std::string_view body);

  // Render the curl config fed to `curl -K -`. Same rationale as the
  // OpenAI-compatible one: keeping the credential out of argv.
  [[nodiscard]] static std::string render_curl_config(std::string_view url,
                                                      std::string_view api_key,
                                                      std::string_view api_version,
                                                      std::string_view json_body,
                                                      std::chrono::seconds timeout);

 private:
  AnthropicProviderOptions opts_;
};

}  // namespace souxmar::ai
