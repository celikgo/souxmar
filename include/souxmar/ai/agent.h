// SPDX-License-Identifier: Apache-2.0
//
// The agent loop.
//
// This is the piece the product is named for: the model receives the
// souxmar tool catalogue, emits a tool call, the dispatcher executes it
// against the real engine, the result goes back, and the model decides
// what to do next — until it answers in prose or hits a step budget.
//
// Before this header existed the loop lived inside tools/eval-llm's
// run_one(), tangled with eval assertions, and it set ChatRequest's
// `tool_names` field, which no provider read. So there was a loop, it
// was not reusable, and it could never actually elicit a tool call. The
// shipped surfaces — the desktop chat panel and `souxmar agent invoke`
// — had no loop at all: chat replied in prose, and `agent invoke` ran
// exactly one tool per process with no model involved.
//
// What this is NOT:
//   * Not streaming. One synchronous provider call per step; the caller
//     sees the transcript when the turn finishes. A streaming surface
//     needs a different Provider contract.
//   * Not parallel. Tool calls within a step execute in the order the
//     model emitted them, because souxmar tools mutate shared session
//     state and reordering them changes results.
//   * Not a sandbox. Tool handlers run in-process with host privileges,
//     exactly as dispatch_tool() documents. The confirmation policy is
//     the only gate, and it is the caller's job to supply a prompter.

#pragma once

#include "souxmar/ai/provider.h"
#include "souxmar/ai/tool.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace souxmar::ai {

// Why a turn ended. Anything other than FinalAnswer means the model did
// not finish on its own terms, and the caller should say so rather than
// present the last text as a conclusion.
enum class AgentStopReason : std::uint8_t {
  // The model replied with prose and no tool calls. The normal ending.
  FinalAnswer = 0,
  // The step budget ran out with the model still calling tools. The
  // work is half-done; `final_text` is the last thing it said, which is
  // not an answer.
  MaxStepsReached = 1,
  // The provider failed (network, auth, rate limit, malformed reply).
  ProviderFailed = 2,
  // A tool required confirmation and the policy refused or had no
  // prompter, and the model gave up rather than route around it.
  Aborted = 3,
  // The model asked for a tool that needs the user's approval and the
  // caller opted into suspension rather than a blocking prompt. The
  // turn is paused, not finished: `pending` says what was asked for and
  // `history` carries everything needed to resume once the user
  // decides. A GUI cannot answer a blocking prompter from inside a
  // synchronous call, so this is how the desktop asks.
  AwaitingConfirmation = 4,
};

[[nodiscard]] std::string_view to_string(AgentStopReason) noexcept;

// One executed tool call, as it happened.
struct AgentToolCall {
  std::string name;
  std::string arguments_json;
  // The dispatcher's human-readable summary, or the error message.
  std::string result_summary;
  bool ok = true;
  // Set when the tool was refused by the confirmation policy rather
  // than failing on its own merits — a distinction the UI should show,
  // since the fix is a click rather than a different prompt.
  bool refused = false;
};

// One provider round-trip plus whatever it caused.
struct AgentStep {
  std::string assistant_text;
  std::vector<AgentToolCall> tool_calls;
  std::chrono::milliseconds latency{0};
};

// A tool call waiting on the user, when stop_reason is
// AwaitingConfirmation.
struct PendingConfirmation {
  std::string tool_name;
  std::string arguments_json;
  // Provider-assigned id of the call, needed to attach the eventual
  // tool-result message to the right request.
  std::string call_id;
  // Remaining calls from the same assistant turn, not yet executed.
  // They run after this one is decided, in the order the model emitted
  // them, because souxmar tools mutate shared state.
  std::vector<ToolCall> queued;
};

struct AgentOutcome {
  AgentStopReason stop_reason = AgentStopReason::FinalAnswer;
  // The model's closing prose. Empty when it never produced any.
  std::string final_text;
  std::vector<AgentStep> steps;
  // Set when stop_reason == ProviderFailed. `error_kind` is kept
  // alongside the message so callers can react to a rate limit or a
  // missing key differently from a generic failure, instead of
  // string-matching the text.
  std::string error;
  ProviderErrorKind error_kind = ProviderErrorKind::ProviderHttpError;
  std::uint64_t input_tokens = 0;
  std::uint64_t output_tokens = 0;
  std::chrono::milliseconds latency{0};

  // Populated iff stop_reason == AwaitingConfirmation.
  PendingConfirmation pending;
  // The conversation as the model has seen it, including tool results.
  // Pass this back to resume_agent_turn() to continue a suspended turn;
  // it is the loop's entire state apart from ToolContext, which the
  // caller owns and must keep alive across the pause.
  std::vector<ChatMessage> history;
  // Steps already completed before the pause, so a resumed turn can be
  // presented as one continuous transcript.
  std::uint32_t steps_used = 0;

  // Convenience for callers that just want to know what ran.
  [[nodiscard]] std::vector<std::string> tools_called() const;
};

struct AgentOptions {
  // Provider-specific model id. Required.
  std::string model;
  // Prepended as the system turn. A default is supplied by
  // default_agent_system_prompt() when this is empty.
  std::string system_prompt;
  // Hard ceiling on provider round-trips. A model that loops calling
  // the same tool forever is a real failure mode, and an unbounded loop
  // spends the user's money doing it.
  std::uint32_t max_steps = 8;
  // Restrict the advertised catalogue. Empty means every registered
  // tool. Naming a subset is how a caller keeps a read-only turn
  // read-only.
  std::vector<std::string> allowed_tools;
  std::optional<double> temperature = 0.0;
  // Suspend instead of dispatching when a tool needs confirmation and
  // the policy has no prompter. Off by default so the CLI keeps its
  // blocking [y/N] prompt; the desktop turns it on because it has to
  // return to the UI thread to ask.
  bool suspend_on_confirmation = false;
  // Steps already spent by an earlier segment of this turn. Set by
  // resume_agent_turn(); callers do not touch it.
  std::uint32_t steps_already_used = 0;
};

// The system prompt used when AgentOptions::system_prompt is empty.
// Exposed so callers can extend rather than reinvent it.
[[nodiscard]] std::string default_agent_system_prompt();

// Run one agent turn to completion.
//
// `messages` is the conversation so far, without a system turn — the
// loop prepends one. It is taken by value and returned untouched;
// callers wanting the grown history should read it from the outcome's
// steps, because what the model saw includes tool-result turns the user
// never typed.
[[nodiscard]] AgentOutcome run_agent_turn(Provider& provider,
                                          const ToolRegistry& registry,
                                          ToolContext& context,
                                          ConfirmationPolicy& policy,
                                          std::vector<ChatMessage> messages,
                                          const AgentOptions& options);

// Continue a turn that stopped with AwaitingConfirmation.
//
// `history` and `pending` come from the suspended outcome; `approved`
// is the user's answer. A denial is not an abort: the model is told the
// user declined, in the same shape as any other tool error, so it can
// explain itself or take a different route.
//
// `context` must be the same one the suspended turn used — the session
// carries mesh and field handles that later tools depend on.
[[nodiscard]] AgentOutcome resume_agent_turn(Provider& provider,
                                             const ToolRegistry& registry,
                                             ToolContext& context,
                                             ConfirmationPolicy& policy,
                                             std::vector<ChatMessage> history,
                                             const PendingConfirmation& pending,
                                             bool approved,
                                             const AgentOptions& options);

}  // namespace souxmar::ai
