// SPDX-License-Identifier: Apache-2.0

#include "souxmar/ai/tool.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace souxmar::ai {

// ---- ToolRegistry --------------------------------------------------------

void ToolRegistry::add(Tool tool) {
  // insert_or_assign — overriding an existing tool is the documented v1
  // behaviour (lets tests swap a stub in for a real tool).
  auto name = tool.name;
  tools_.insert_or_assign(std::move(name), std::move(tool));
}

const Tool* ToolRegistry::find(std::string_view name) const noexcept {
  auto it = tools_.find(std::string(name));
  return it == tools_.end() ? nullptr : &it->second;
}

std::vector<std::string> ToolRegistry::list() const {
  std::vector<std::string> out;
  out.reserve(tools_.size());
  for (const auto& [k, _] : tools_) out.push_back(k);
  std::sort(out.begin(), out.end());
  return out;
}

std::size_t ToolRegistry::size() const noexcept {
  return tools_.size();
}

// ---- dispatch_tool -------------------------------------------------------

namespace {

Confirmation effective_confirmation(const Tool&                tool,
                                    const ConfirmationPolicy&  policy) {
  if (auto it = policy.overrides.find(tool.name); it != policy.overrides.end()) {
    return it->second;
  }
  return tool.confirmation;
}

ToolResult make_error(std::string code,
                      std::string message,
                      std::string suggestion = {}) {
  ToolResult r;
  r.error = ToolError{std::move(code), std::move(message), std::move(suggestion)};
  r.summary = "error: " + r.error->message;
  return r;
}

}  // namespace

ToolResult dispatch_tool(const ToolRegistry&     registry,
                         std::string_view        tool_name,
                         const pipeline::Value&  inputs,
                         ToolContext&            context,
                         ConfirmationPolicy&     policy) {
  const auto* tool = registry.find(tool_name);
  if (tool == nullptr) {
    return make_error("NOT_FOUND",
        "no tool named '" + std::string(tool_name) + "' is registered",
        "call `souxmar agent list` (or ToolRegistry.list()) to see available tools");
  }

  // Confirmation gate. The order matches the contract in the header
  // comment: overrides → tool default → confirmed_once short-circuit →
  // prompter → reject if no prompter.
  const auto required = effective_confirmation(*tool, policy);
  if (required != Confirmation::Auto) {
    const bool already_confirmed_once =
        required == Confirmation::ConfirmOnce &&
        policy.confirmed_once.contains(tool->name);
    if (!already_confirmed_once) {
      if (!policy.prompter) {
        return make_error("NOT_CONFIRMED",
            "tool '" + tool->name + "' requires user confirmation but no prompter "
            "was supplied",
            "either set ConfirmationPolicy.prompter or add an override mapping "
            "this tool to Confirmation::Auto");
      }
      const bool approved = policy.prompter(*tool, inputs);
      if (!approved) {
        return make_error("DENIED",
            "user declined to run '" + tool->name + "'");
      }
      if (required == Confirmation::ConfirmOnce) {
        policy.confirmed_once.insert(tool->name);
      }
    }
  }

  // Invoke handler. Catch every exception type to keep the agent
  // runtime from ever seeing a raw throw — model recovery only works
  // against structured ToolError.
  try {
    auto result = tool->handler(inputs, context);
    return result;
  } catch (const std::exception& e) {
    return make_error("INTERNAL",
        "tool '" + tool->name + "' threw an exception: " + e.what());
  } catch (...) {
    return make_error("INTERNAL",
        "tool '" + tool->name + "' threw an unknown exception");
  }
}

}  // namespace souxmar::ai
