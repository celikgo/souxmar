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
//   * AnthropicProvider, OpenAIProvider — sketched in
//                        AI_INTEGRATION.md; lands in Sprint 14
//                        (managed-AI proxy MVP). The interface
//                        here is set up to take them.
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

// One turn in a chat history. Order is preserved; the provider sends
// every message it receives.
struct ChatMessage {
  enum class Role : std::uint8_t {
    System    = 0,
    User      = 1,
    Assistant = 2,
    Tool      = 3,   // tool-result message, replying to a prior tool call
  };
  Role         role = Role::User;
  std::string  content;
  // For Role::Tool: the id of the tool call this message responds to.
  // Empty for non-Tool roles.
  std::string  tool_call_id;
};

[[nodiscard]] std::string_view to_string(ChatMessage::Role) noexcept;

// A tool call emitted by the assistant. The dispatcher resolves
// `name` against the tool registry and parses `arguments_json`
// against the tool's input schema. Multiple tool calls per
// assistant turn are allowed.
struct ToolCall {
  std::string  id;              // provider-assigned identifier
  std::string  name;            // matches Tool::name
  std::string  arguments_json;  // raw JSON; dispatcher parses
};

// Response from one chat_completion call. Either the assistant
// produced a text reply (`text` non-empty, `tool_calls` empty), one
// or more tool calls (`text` may be empty), or both (some providers
// emit a "thinking" prefix alongside the call list).
struct ChatResponse {
  std::string             text;
  std::vector<ToolCall>   tool_calls;
  // Provider-reported token counts; zero when the provider doesn't
  // expose them (Ollama, today).
  std::uint64_t           input_tokens  = 0;
  std::uint64_t           output_tokens = 0;
  // Wall-clock latency the *provider* took. The eval-suite's
  // dispatcher records this end-to-end for the latency gate.
  std::chrono::milliseconds latency{0};
  // True iff the response was truncated (max-tokens reached).
  bool                    truncated     = false;
};

enum class ProviderErrorKind : std::uint8_t {
  // Provider returned a non-2xx HTTP status that wasn't a typed
  // rate-limit. The detail string carries the body.
  ProviderHttpError       = 0,
  RateLimited             = 1,
  // The local Ollama daemon refused the connection (not running).
  LocalDaemonUnreachable  = 2,
  // The model name doesn't exist in the daemon's library; Ollama
  // returns this as a 404 with a typed body.
  ModelNotFound           = 3,
  // The HTTP client (curl subprocess) failed to spawn or returned
  // a non-zero exit unrelated to the HTTP status. Diagnostic only.
  HttpClientFailed        = 4,
  // The provider's response body couldn't be parsed (malformed
  // JSON, missing required field).
  MalformedResponse       = 5,
  // The provider returned a response shape we don't know how to
  // map onto ChatResponse (e.g. a model that doesn't speak the
  // function-calling protocol Ollama exposes).
  ProtocolMismatch        = 6,
  // The caller passed a request the provider couldn't honour
  // (empty messages, contradictory tool schemas, etc.).
  BadRequest              = 7,
  // The request exceeded the provider's context limit.
  ContextLengthExceeded   = 8,
};

[[nodiscard]] std::string_view to_string(ProviderErrorKind) noexcept;

struct ProviderError {
  ProviderErrorKind  kind = ProviderErrorKind::ProviderHttpError;
  std::string        message;
};

using ChatResult = std::variant<ChatResponse, ProviderError>;

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
  std::string                  model;        // provider-specific model id
  std::vector<ChatMessage>     messages;
  // Tool catalogue. Each entry is a tool::Tool name; the provider
  // resolves it against the registry it was constructed with.
  std::vector<std::string>     tool_names;
  // Sampling knobs. Providers that don't support a field clamp
  // silently.
  std::optional<double>        temperature;
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
  [[nodiscard]] virtual std::vector<std::string>
  available_models() const = 0;

  // Run one inference. Synchronous.
  [[nodiscard]] virtual ChatResult
  chat_completion(const ChatRequest& req) = 0;
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
                     ChatResponse     reply);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "stub";
  }
  [[nodiscard]] std::vector<std::string> available_models() const override;
  [[nodiscard]] ChatResult chat_completion(const ChatRequest& req) override;

 private:
  struct Programmed {
    std::string  model;
    std::string  trigger;
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
  std::string                   endpoint = "http://localhost:11434";
  // Per-call timeout. Local inference on smaller hardware can take
  // tens of seconds for the first token; 120s is a comfortable
  // default. Override via SOUXMAR_OLLAMA_TIMEOUT_SECS.
  std::chrono::seconds          timeout{120};
  // Path to the curl binary. Defaults to `curl` resolved via $PATH.
  std::string                   curl_binary;
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
  [[nodiscard]] static std::string render_request_body(
      const ChatRequest& req,
      const std::vector<Tool>& tool_definitions);

  // Parse Ollama's /api/chat JSON response into a ChatResponse.
  // Returns ProviderError on malformed input.
  [[nodiscard]] static ChatResult parse_response_body(std::string_view body);

 private:
  OllamaProviderOptions opts_;
};

}  // namespace souxmar::ai
