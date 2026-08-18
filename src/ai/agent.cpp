// SPDX-License-Identifier: Apache-2.0
//
// Agent loop implementation. See include/souxmar/ai/agent.h.

#include "souxmar/ai/agent.h"

#include "souxmar/ai/audit_log.h"  // SessionBudget::record
#include "souxmar/pipeline/value.h"

#include <algorithm>
#include <utility>

namespace souxmar::ai {

namespace pl = souxmar::pipeline;

std::string_view to_string(AgentStopReason r) noexcept {
  switch (r) {
    case AgentStopReason::FinalAnswer:
      return "final-answer";
    case AgentStopReason::MaxStepsReached:
      return "max-steps-reached";
    case AgentStopReason::ProviderFailed:
      return "provider-failed";
    case AgentStopReason::Aborted:
      return "aborted";
    case AgentStopReason::AwaitingConfirmation:
      return "awaiting-confirmation";
  }
  return "unknown";
}

std::vector<std::string> AgentOutcome::tools_called() const {
  std::vector<std::string> names;
  for (const auto& step : steps) {
    for (const auto& call : step.tool_calls) {
      names.push_back(call.name);
    }
  }
  return names;
}

std::string default_agent_system_prompt() {
  // Deliberately short. Long prompts that restate the tool catalogue in
  // prose drift from the catalogue, and the catalogue is already on the
  // wire with a description per tool.
  return
      "You are the souxmar agent, working inside a CAD/FEM/CFD workbench. "
      "Use the provided tools to inspect and act on the user's project; "
      "prefer calling a tool over guessing. Tool results are facts about "
      "the real project — never invent a number that a tool could have "
      "told you. If a tool fails, read the error and either correct the "
      "arguments or explain the problem; do not retry the same call "
      "unchanged. When you have what you need, answer in plain prose "
      "without calling further tools.";
}

namespace {

// Advertise the catalogue, honouring an allow-list.
std::vector<ToolDefinition> build_tool_definitions(const ToolRegistry& registry,
                                                   const std::vector<std::string>& allowed) {
  std::vector<ToolDefinition> defs;
  for (const auto& name : registry.list()) {
    if (!allowed.empty()
        && std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
      continue;
    }
    if (const Tool* t = registry.find(name); t != nullptr) {
      // Append the input-schema prose to the description. Every
      // provider advertises a permissive `{"type":"object"}` schema —
      // the v1 tool contract documents inputs as prose, not JSON
      // Schema, and adding a machine-readable field to Tool is a
      // ratcheted change to a frozen header (ADR-0011).
      //
      // Without this the model is told a tool exists and nothing about
      // its arguments, so it invents names and every call fails
      // validation. Putting the documented shape in the description is
      // the one place it can reach the model without touching the
      // contract.
      std::string description = t->description;
      if (!t->input_schema_doc.empty()) {
        description += "\n\nInput (JSON object with these fields):\n";
        description += t->input_schema_doc;
      }
      defs.push_back({t->name, std::move(description)});
    }
  }
  return defs;
}

// Models emit arguments as a JSON string. parse_value_yaml reads JSON
// (YAML 1.2 is a superset), so this is the same path pipeline inputs
// take. A model that emits malformed arguments gets an empty map and
// the tool's own "missing required field" error, which is a better
// message than anything this layer could synthesise.
pl::Value parse_arguments(const std::string& json) {
  if (json.empty()) {
    return pl::Value::map({});
  }
  try {
    return pl::parse_value_yaml(json);
  } catch (const std::exception&) {
    return pl::Value::map({});
  }
}

// What the model sees as the result of its call. Errors are handed back
// verbatim, including the tool's suggestion, because recovering from a
// typed error is exactly what the catalogue's error surface is for.
std::string render_tool_result(const ToolResult& result) {
  if (!result.error) {
    return result.summary.empty() ? "ok" : result.summary;
  }
  std::string text = "error [" + result.error->code + "]: " + result.error->message;
  if (!result.error->suggestion.empty()) {
    text += " — " + result.error->suggestion;
  }
  return text;
}

// True when dispatching `name` would need a user decision that the
// current policy cannot supply. Mirrors dispatch_tool's resolution
// order so the loop and the dispatcher never disagree about whether a
// prompt is required.
bool needs_user_decision(const ToolRegistry& registry,
                         const ConfirmationPolicy& policy,
                         const std::string& name) {
  const Tool* tool = registry.find(name);
  if (tool == nullptr) {
    return false;  // unknown tool: let dispatch_tool produce NOT_FOUND
  }
  Confirmation level = tool->confirmation;
  if (const auto it = policy.overrides.find(name); it != policy.overrides.end()) {
    level = it->second;
  }
  if (level == Confirmation::Auto) {
    return false;
  }
  if (level == Confirmation::ConfirmOnce && policy.confirmed_once.count(name) > 0) {
    return false;
  }
  return policy.prompter == nullptr;
}

// Execute one tool call and append its result to the history. Shared by
// the initial run and the resumed continuation so both produce the same
// transcript shape.
AgentToolCall execute_call(const ToolRegistry& registry,
                           ToolContext& context,
                           ConfirmationPolicy& policy,
                           const ToolCall& call,
                           std::vector<ChatMessage>& history) {
  AgentToolCall executed;
  executed.name = call.name;
  executed.arguments_json = call.arguments_json;

  const auto inputs = parse_arguments(call.arguments_json);
  const auto dispatched = dispatch_tool(registry, call.name, inputs, context, policy);

  executed.ok = !dispatched.error.has_value();
  executed.refused = dispatched.error
                     && (dispatched.error->code == "NOT_CONFIRMED"
                         || dispatched.error->code == "DENIED");
  executed.result_summary = render_tool_result(dispatched);

  // No "tool result:" prefix — the Tool role already says what this
  // is, and a model that quotes the message back was echoing the
  // prefix into its prose.
  history.push_back({ChatMessage::Role::Tool, executed.result_summary, call.id, {}});
  return executed;
}

}  // namespace

// The loop proper. `history` is already complete, system turn included,
// which is what lets a resumed turn re-enter it without prepending a
// second system message.
AgentOutcome run_loop(Provider& provider,
                      const ToolRegistry& registry,
                      ToolContext& context,
                      ConfirmationPolicy& policy,
                      std::vector<ChatMessage> history,
                      const AgentOptions& options) {
  AgentOutcome outcome;

  const auto tool_defs = build_tool_definitions(registry, options.allowed_tools);
  const std::uint32_t max_steps = options.max_steps == 0 ? 1 : options.max_steps;

  for (std::uint32_t step = 0; step < max_steps; ++step) {
    ChatRequest req;
    req.model = options.model;
    req.messages = history;
    req.tools = tool_defs;
    req.temperature = options.temperature;

    auto result = provider.chat_completion(req);
    if (auto* err = std::get_if<ProviderError>(&result)) {
      outcome.stop_reason = AgentStopReason::ProviderFailed;
      outcome.error = std::string(to_string(err->kind)) + ": " + err->message;
      outcome.error_kind = err->kind;
      return outcome;
    }
    auto& response = std::get<ChatResponse>(result);

    AgentStep record;
    record.assistant_text = response.text;
    record.latency = response.latency;
    outcome.latency += response.latency;
    outcome.input_tokens += response.input_tokens;
    outcome.output_tokens += response.output_tokens;

    // Feed the provider's own accounting into the session budget. The
    // budget was previously parsed, applied and logged but never
    // incremented, because nothing called record().
    if (context.budget != nullptr && (response.input_tokens || response.output_tokens)) {
      context.budget->record(static_cast<std::size_t>(response.input_tokens),
                             static_cast<std::size_t>(response.output_tokens));
    }

    // No tool calls: the model is done talking.
    if (response.tool_calls.empty()) {
      outcome.final_text = response.text;
      outcome.steps.push_back(std::move(record));
      outcome.stop_reason = AgentStopReason::FinalAnswer;
      history.push_back({ChatMessage::Role::Assistant, response.text, {}, {}});
      outcome.history = std::move(history);
      outcome.steps_used = options.steps_already_used + step + 1;
      return outcome;
    }

    // Carry the calls, not just the prose. Both OpenAI and Anthropic
    // reject a tool result whose originating call is absent from the
    // history, so dropping them here made every multi-step turn
    // malformed on the wire.
    history.push_back({ChatMessage::Role::Assistant, response.text, {}, response.tool_calls});

    for (std::size_t i = 0; i < response.tool_calls.size(); ++i) {
      const auto& call = response.tool_calls[i];

      // A tool needing the user's approval, with no way to ask from
      // here: hand the question back to the caller with everything
      // required to carry on afterwards.
      if (options.suspend_on_confirmation && needs_user_decision(registry, policy, call.name)) {
        outcome.stop_reason = AgentStopReason::AwaitingConfirmation;
        outcome.pending.tool_name = call.name;
        outcome.pending.arguments_json = call.arguments_json;
        outcome.pending.call_id = call.id;
        outcome.pending.queued.assign(response.tool_calls.begin() + static_cast<long>(i) + 1,
                                      response.tool_calls.end());
        outcome.history = std::move(history);
        outcome.steps.push_back(std::move(record));
        outcome.steps_used = options.steps_already_used + step + 1;
        return outcome;
      }

      // The result goes back to the model whether it succeeded or not.
      // A refusal is information the model can act on — it can explain
      // what it wanted to do — and swallowing it would leave the user
      // with a silent stall.
      record.tool_calls.push_back(execute_call(registry, context, policy, call, history));
    }

    outcome.steps.push_back(std::move(record));
  }

  // Fell out of the loop still calling tools.
  outcome.stop_reason = AgentStopReason::MaxStepsReached;
  if (!outcome.steps.empty()) {
    outcome.final_text = outcome.steps.back().assistant_text;
  }
  outcome.history = std::move(history);
  outcome.steps_used = options.steps_already_used + max_steps;
  return outcome;
}

AgentOutcome run_agent_turn(Provider& provider,
                            const ToolRegistry& registry,
                            ToolContext& context,
                            ConfirmationPolicy& policy,
                            std::vector<ChatMessage> messages,
                            const AgentOptions& options) {
  const auto system_prompt =
      options.system_prompt.empty() ? default_agent_system_prompt() : options.system_prompt;
  std::vector<ChatMessage> history;
  history.reserve(messages.size() + 1);
  history.push_back({ChatMessage::Role::System, system_prompt, {}, {}});
  for (auto& m : messages) {
    history.push_back(std::move(m));
  }
  return run_loop(provider, registry, context, policy, std::move(history), options);
}

AgentOutcome resume_agent_turn(Provider& provider,
                               const ToolRegistry& registry,
                               ToolContext& context,
                               ConfirmationPolicy& policy,
                               std::vector<ChatMessage> history,
                               const PendingConfirmation& pending,
                               bool approved,
                               const AgentOptions& options) {
  AgentOutcome outcome;

  // Settle the call the user was asked about. Approval is recorded on
  // the policy so a ConfirmOnce tool is not asked about twice, and so
  // dispatch_tool's own gate agrees with the decision just made.
  AgentStep step;
  ToolCall call;
  call.id = pending.call_id;
  call.name = pending.tool_name;
  call.arguments_json = pending.arguments_json;

  if (approved) {
    policy.overrides[pending.tool_name] = Confirmation::Auto;
    policy.confirmed_once.insert(pending.tool_name);
    step.tool_calls.push_back(execute_call(registry, context, policy, call, history));
    // Undo the blanket override so a later ConfirmAlways call asks
    // again. ConfirmOnce stays satisfied via confirmed_once, which is
    // exactly the distinction the two levels exist to make.
    policy.overrides.erase(pending.tool_name);
  } else {
    AgentToolCall denied;
    denied.name = pending.tool_name;
    denied.arguments_json = pending.arguments_json;
    denied.ok = false;
    denied.refused = true;
    denied.result_summary =
        "error [DENIED]: the user declined to run '" + pending.tool_name + "'";
    history.push_back({ChatMessage::Role::Tool, denied.result_summary, pending.call_id, {}});
    step.tool_calls.push_back(std::move(denied));
  }

  // Any remaining calls from the same assistant turn.
  for (const auto& queued : pending.queued) {
    if (options.suspend_on_confirmation && needs_user_decision(registry, policy, queued.name)) {
      outcome.stop_reason = AgentStopReason::AwaitingConfirmation;
      outcome.pending.tool_name = queued.name;
      outcome.pending.arguments_json = queued.arguments_json;
      outcome.pending.call_id = queued.id;
      // Everything after this one stays queued.
      bool seen = false;
      for (const auto& rest : pending.queued) {
        if (seen)
          outcome.pending.queued.push_back(rest);
        if (rest.id == queued.id)
          seen = true;
      }
      outcome.history = std::move(history);
      outcome.steps.push_back(std::move(step));
      outcome.steps_used = options.steps_already_used;
      return outcome;
    }
    step.tool_calls.push_back(execute_call(registry, context, policy, queued, history));
  }

  outcome.steps.push_back(std::move(step));

  // Carry on where the suspended turn left off. The history already has
  // the system turn, so it is passed through untouched.
  AgentOptions continued = options;
  continued.steps_already_used = options.steps_already_used;
  if (continued.max_steps > options.steps_already_used) {
    continued.max_steps -= options.steps_already_used;
  } else {
    continued.max_steps = 1;
  }

  auto rest = run_loop(provider, registry, context, policy, std::move(history), continued);

  outcome.stop_reason = rest.stop_reason;
  outcome.final_text = rest.final_text;
  outcome.error = rest.error;
  outcome.error_kind = rest.error_kind;
  outcome.input_tokens = rest.input_tokens;
  outcome.output_tokens = rest.output_tokens;
  outcome.latency = rest.latency;
  outcome.pending = rest.pending;
  outcome.history = rest.history;
  outcome.steps_used = rest.steps_used;
  for (auto& s : rest.steps) {
    outcome.steps.push_back(std::move(s));
  }
  return outcome;
}

}  // namespace souxmar::ai
