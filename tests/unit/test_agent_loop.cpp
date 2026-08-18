// SPDX-License-Identifier: Apache-2.0
//
// Agent-loop tests. The provider is a StubProvider programmed to emit
// tool calls, so the loop, the dispatcher and the real tool registry all
// run for real — only the model is simulated.

#include "souxmar/ai/agent.h"

#include "souxmar/ai/audit_log.h"
#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/value.h"

#include <gtest/gtest.h>

#include <string>

using namespace souxmar::ai;
namespace pl = souxmar::pipeline;

namespace {

// A registry with two tools: one that always succeeds and records that
// it ran, one that needs confirmation.
struct Probe {
  int calls = 0;
  std::string last_arg;
};

ToolRegistry make_registry(Probe& probe) {
  ToolRegistry reg;

  Tool echo;
  echo.name = "probe_echo";
  echo.description = "Echo a value back. Test tool.";
  echo.confirmation = Confirmation::Auto;
  echo.handler = [&probe](const pl::Value& inputs, ToolContext&) -> ToolResult {
    probe.calls++;
    ToolResult r;
    if (const auto* v = inputs.find("text"); v != nullptr) {
      if (const auto* text = v->try_string(); text != nullptr) {
        probe.last_arg = *text;
      }
    }
    r.summary = "echoed: " + probe.last_arg;
    return r;
  };
  reg.add(std::move(echo));

  Tool guarded;
  guarded.name = "probe_guarded";
  guarded.description = "A tool that requires confirmation. Test tool.";
  guarded.confirmation = Confirmation::ConfirmAlways;
  guarded.handler = [&probe](const pl::Value&, ToolContext&) -> ToolResult {
    probe.calls++;
    ToolResult r;
    r.summary = "guarded ran";
    return r;
  };
  reg.add(std::move(guarded));

  Tool failing;
  failing.name = "probe_fails";
  failing.description = "Always fails. Test tool.";
  failing.handler = [](const pl::Value&, ToolContext&) -> ToolResult {
    ToolResult r;
    r.error = ToolError{"INVALID_ARGUMENT", "target_size must be positive", "try 0.05"};
    return r;
  };
  reg.add(std::move(failing));

  return reg;
}

// StubProvider matches on the last *user* message, so it cannot change
// its mind between steps — useless for testing a loop. This returns a
// queued sequence instead, and keeps every request it was given so a
// test can assert what the model actually saw on the next turn.
class ScriptedProvider final : public Provider {
 public:
  void push(ChatResult r) {
    queue_.push_back(std::move(r));
  }

  [[nodiscard]] std::string_view name() const noexcept override {
    return "scripted";
  }
  [[nodiscard]] std::vector<std::string> available_models() const override {
    return {"m"};
  }
  ChatResult chat_completion(const ChatRequest& req) override {
    seen.push_back(req);
    if (index_ >= queue_.size()) {
      return ProviderError{ProviderErrorKind::ProtocolMismatch, "scripted: ran out of replies"};
    }
    return queue_[index_++];
  }

  std::vector<ChatRequest> seen;

 private:
  std::vector<ChatResult> queue_;
  std::size_t index_ = 0;
};

ChatResponse tool_call(std::string name, std::string args) {
  ChatResponse r;
  ToolCall tc;
  tc.id = "call_1";
  tc.name = std::move(name);
  tc.arguments_json = std::move(args);
  r.tool_calls.push_back(std::move(tc));
  return r;
}

ChatResponse final_text(std::string text) {
  ChatResponse r;
  r.text = std::move(text);
  return r;
}

}  // namespace

TEST(AgentLoop, DispatchesAToolCallAndFeedsTheResultBack) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  ScriptedProvider provider;
  provider.push(tool_call("probe_echo", R"({"text":"hello"})"));
  provider.push(final_text("The probe echoed hello."));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 4;

  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "run the probe", {}, {}}}, opts);

  // The tool really ran, with the arguments the model chose.
  EXPECT_EQ(probe.calls, 1);
  EXPECT_EQ(probe.last_arg, "hello");

  // Two round-trips: the call, then the answer.
  ASSERT_EQ(out.steps.size(), 2u);
  ASSERT_EQ(out.steps[0].tool_calls.size(), 1u);
  EXPECT_EQ(out.steps[0].tool_calls[0].name, "probe_echo");
  EXPECT_TRUE(out.steps[0].tool_calls[0].ok);
  EXPECT_EQ(out.steps[0].tool_calls[0].result_summary, "echoed: hello");
  EXPECT_EQ(out.stop_reason, AgentStopReason::FinalAnswer);
  EXPECT_EQ(out.final_text, "The probe echoed hello.");
  EXPECT_EQ(out.tools_called(), std::vector<std::string>{"probe_echo"});

  // The load-bearing assertion: on the second turn the model was shown
  // the tool's actual output, as a Tool-role message tied to the call
  // id. Without this the loop is just two unrelated completions.
  ASSERT_EQ(provider.seen.size(), 2u);
  const auto& second = provider.seen[1].messages;
  bool found = false;
  for (const auto& m : second) {
    if (m.role == ChatMessage::Role::Tool && m.content.find("echoed: hello") != std::string::npos) {
      found = true;
      EXPECT_EQ(m.tool_call_id, "call_1");
    }
  }
  EXPECT_TRUE(found) << "the tool result never reached the model";

  // And the catalogue was advertised on every turn.
  for (const auto& req : provider.seen) {
    EXPECT_FALSE(req.tools.empty()) << "no tools offered to the model";
  }
}

TEST(AgentLoop, StopsOnAProseAnswer) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  StubProvider provider;
  provider.program_reply("m", "just answer", final_text("The beam is 2 m long."));

  AgentOptions opts;
  opts.model = "m";
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "just answer", {}, {}}}, opts);

  EXPECT_EQ(out.stop_reason, AgentStopReason::FinalAnswer);
  EXPECT_EQ(out.final_text, "The beam is 2 m long.");
  EXPECT_EQ(probe.calls, 0);
  EXPECT_TRUE(out.tools_called().empty());
}

TEST(AgentLoop, HonoursTheStepBudgetWhenTheModelLoops) {
  // A model that keeps calling the same tool must not run forever —
  // every extra step costs the user money.
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  StubProvider provider;
  provider.program_reply("m", "", tool_call("probe_echo", R"({"text":"again"})"));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 3;
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "loop", {}, {}}}, opts);

  EXPECT_EQ(out.stop_reason, AgentStopReason::MaxStepsReached);
  EXPECT_EQ(out.steps.size(), 3u);
  EXPECT_EQ(probe.calls, 3);
}

TEST(AgentLoop, ToolErrorsGoBackToTheModelRatherThanAborting) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  StubProvider provider;
  provider.program_reply("m", "", tool_call("probe_fails", "{}"));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 1;
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "break it", {}, {}}}, opts);

  ASSERT_EQ(out.steps.size(), 1u);
  ASSERT_EQ(out.steps[0].tool_calls.size(), 1u);
  EXPECT_FALSE(out.steps[0].tool_calls[0].ok);
  // The model must receive the code, the message and the suggestion —
  // that is what lets it correct the call instead of retrying blindly.
  const auto& summary = out.steps[0].tool_calls[0].result_summary;
  EXPECT_NE(summary.find("INVALID_ARGUMENT"), std::string::npos);
  EXPECT_NE(summary.find("must be positive"), std::string::npos);
  EXPECT_NE(summary.find("try 0.05"), std::string::npos);
}

TEST(AgentLoop, ConfirmationIsEnforcedAndReportedAsRefusal) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;  // no prompter → confirmation must fail closed

  StubProvider provider;
  provider.program_reply("m", "", tool_call("probe_guarded", "{}"));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 1;
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "do it", {}, {}}}, opts);

  EXPECT_EQ(probe.calls, 0) << "a guarded tool ran without confirmation";
  ASSERT_EQ(out.steps.size(), 1u);
  ASSERT_EQ(out.steps[0].tool_calls.size(), 1u);
  EXPECT_FALSE(out.steps[0].tool_calls[0].ok);
  EXPECT_TRUE(out.steps[0].tool_calls[0].refused);
}

TEST(AgentLoop, ConfirmationPrompterLetsTheToolThrough) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;
  bool prompted = false;
  policy.prompter = [&prompted](const Tool&, const pl::Value&) {
    prompted = true;
    return true;
  };

  StubProvider provider;
  provider.program_reply("m", "", tool_call("probe_guarded", "{}"));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 1;
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "do it", {}, {}}}, opts);

  EXPECT_TRUE(prompted);
  EXPECT_EQ(probe.calls, 1);
  EXPECT_TRUE(out.steps[0].tool_calls[0].ok);
}

TEST(AgentLoop, AllowedToolsRestrictsWhatTheModelIsOffered) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  // Capture what the request actually advertised by rendering it the
  // way a provider would.
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "x", {}, {}}};
  req.tools = {{"probe_echo", "Echo"}};
  const auto body = OpenAICompatibleProvider::render_request_body(req, {});
  EXPECT_NE(body.find("\"name\":\"probe_echo\""), std::string::npos);
  EXPECT_EQ(body.find("probe_guarded"), std::string::npos);
}

TEST(AgentLoop, ProviderFailureIsReportedNotSwallowed) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  StubProvider provider;  // nothing programmed → ProtocolMismatch

  AgentOptions opts;
  opts.model = "m";
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "hi", {}, {}}}, opts);

  EXPECT_EQ(out.stop_reason, AgentStopReason::ProviderFailed);
  EXPECT_FALSE(out.error.empty());
  EXPECT_TRUE(out.final_text.empty()) << "a failed turn must not present text as an answer";
}

TEST(AgentLoop, RecordsTokensAgainstTheSessionBudget) {
  // The budget was parsed, applied and logged but never incremented,
  // because nothing called record(). The loop is where that belongs.
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  SessionBudget budget;
  budget.max_total_tokens = 1000;
  ToolContext ctx;
  ctx.session_state = &state;
  ctx.budget = &budget;
  ConfirmationPolicy policy;

  StubProvider provider;
  ChatResponse reply = final_text("done");
  reply.input_tokens = 30;
  reply.output_tokens = 12;
  provider.program_reply("m", "", reply);

  AgentOptions opts;
  opts.model = "m";
  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "hi", {}, {}}}, opts);

  EXPECT_EQ(budget.consumed_input, 30u);
  EXPECT_EQ(budget.consumed_output, 12u);
  EXPECT_EQ(out.input_tokens, 30u);
  EXPECT_EQ(out.output_tokens, 12u);
}

TEST(AgentLoop, AdvertisesTheRealCatalogueToTheModel) {
  // The v1 catalogue must reach the wire; this is the regression that
  // made every published tool-calling result unobtainable.
  Probe probe;
  auto registry = make_registry(probe);
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "x", {}, {}}};
  for (const auto& name : registry.list()) {
    if (const Tool* t = registry.find(name); t != nullptr) {
      req.tools.push_back({t->name, t->description});
    }
  }
  const auto ollama = OllamaProvider::render_request_body(req, {});
  const auto openai = OpenAICompatibleProvider::render_request_body(req, {});
  for (const auto* body : {&ollama, &openai}) {
    EXPECT_NE(body->find("\"tools\":["), std::string::npos);
    EXPECT_NE(body->find("probe_echo"), std::string::npos);
  }
}

// ---- Suspend / resume --------------------------------------------------
//
// A GUI cannot answer a blocking prompter from inside a synchronous
// call, so the loop hands the question back and is resumed once the
// user decides. These pin that the pause loses nothing.

TEST(AgentLoop, SuspendsWhenAToolNeedsTheUser) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;  // no prompter: a GUI answers out of band

  ScriptedProvider provider;
  provider.push(tool_call("probe_guarded", "{}"));

  AgentOptions opts;
  opts.model = "m";
  opts.suspend_on_confirmation = true;

  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "do it", {}, {}}}, opts);

  EXPECT_EQ(out.stop_reason, AgentStopReason::AwaitingConfirmation);
  EXPECT_EQ(out.pending.tool_name, "probe_guarded");
  EXPECT_EQ(out.pending.call_id, "call_1");
  EXPECT_EQ(probe.calls, 0) << "the tool ran before the user was asked";
  EXPECT_FALSE(out.history.empty()) << "history must survive the pause";
}

TEST(AgentLoop, ResumingWithApprovalRunsTheToolAndContinues) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  ScriptedProvider provider;
  provider.push(tool_call("probe_guarded", "{}"));
  provider.push(final_text("Done — the guarded tool ran."));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 4;
  opts.suspend_on_confirmation = true;

  const auto paused = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "do it", {}, {}}}, opts);
  ASSERT_EQ(paused.stop_reason, AgentStopReason::AwaitingConfirmation);

  AgentOptions resumed_opts = opts;
  resumed_opts.steps_already_used = paused.steps_used;
  const auto out = resume_agent_turn(provider,
                                     registry,
                                     ctx,
                                     policy,
                                     paused.history,
                                     paused.pending,
                                     /*approved=*/true,
                                     resumed_opts);

  EXPECT_EQ(probe.calls, 1) << "approval did not actually run the tool";
  EXPECT_EQ(out.stop_reason, AgentStopReason::FinalAnswer);
  EXPECT_EQ(out.final_text, "Done — the guarded tool ran.");
  ASSERT_FALSE(out.steps.empty());
  EXPECT_TRUE(out.steps[0].tool_calls[0].ok);

  // The model must have been shown the tool's real output on the turn
  // after the pause — otherwise the resume dropped the result.
  ASSERT_GE(provider.seen.size(), 2u);
  const auto& after = provider.seen.back().messages;
  bool saw_result = false;
  for (const auto& m : after) {
    if (m.role == ChatMessage::Role::Tool && m.content.find("guarded ran") != std::string::npos) {
      saw_result = true;
    }
  }
  EXPECT_TRUE(saw_result);
}

TEST(AgentLoop, ResumingWithDenialTellsTheModelRatherThanAborting) {
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  ScriptedProvider provider;
  provider.push(tool_call("probe_guarded", "{}"));
  provider.push(final_text("Understood — I did not change anything."));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 4;
  opts.suspend_on_confirmation = true;

  const auto paused = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "do it", {}, {}}}, opts);
  ASSERT_EQ(paused.stop_reason, AgentStopReason::AwaitingConfirmation);

  AgentOptions resumed_opts = opts;
  resumed_opts.steps_already_used = paused.steps_used;
  const auto out = resume_agent_turn(
      provider, registry, ctx, policy, paused.history, paused.pending, false, resumed_opts);

  EXPECT_EQ(probe.calls, 0) << "a denied tool ran anyway";
  ASSERT_FALSE(out.steps.empty());
  EXPECT_TRUE(out.steps[0].tool_calls[0].refused);
  EXPECT_EQ(out.stop_reason, AgentStopReason::FinalAnswer)
      << "a denial should let the model respond, not abort the turn";

  const auto& after = provider.seen.back().messages;
  bool told = false;
  for (const auto& m : after) {
    if (m.role == ChatMessage::Role::Tool && m.content.find("DENIED") != std::string::npos) {
      told = true;
    }
  }
  EXPECT_TRUE(told) << "the model was never told the user declined";
}

TEST(AgentLoop, AutoToolsNeverSuspend) {
  // The read-only catalogue must run start to finish with no prompting,
  // which is what makes a default-safe chat panel possible.
  Probe probe;
  auto registry = make_registry(probe);
  pl::Value state = pl::Value::map({});
  ToolContext ctx;
  ctx.session_state = &state;
  ConfirmationPolicy policy;

  ScriptedProvider provider;
  provider.push(tool_call("probe_echo", R"({"text":"x"})"));
  provider.push(final_text("done"));

  AgentOptions opts;
  opts.model = "m";
  opts.max_steps = 4;
  opts.suspend_on_confirmation = true;

  const auto out = run_agent_turn(
      provider, registry, ctx, policy, {{ChatMessage::Role::User, "go", {}, {}}}, opts);
  EXPECT_EQ(out.stop_reason, AgentStopReason::FinalAnswer);
  EXPECT_EQ(probe.calls, 1);
}
