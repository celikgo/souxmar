// SPDX-License-Identifier: Apache-2.0

#include "souxmar/ai/audit_log.h"
#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/cache.h"  // hash_inputs / ContentHash
#include "souxmar/plugin/heap_accountant.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <string_view>
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
  for (const auto& [k, _] : tools_)
    out.push_back(k);
  std::sort(out.begin(), out.end());
  return out;
}

std::size_t ToolRegistry::size() const noexcept {
  return tools_.size();
}

// ---- dispatch_tool -------------------------------------------------------

namespace {

Confirmation effective_confirmation(const Tool& tool, const ConfirmationPolicy& policy) {
  if (auto it = policy.overrides.find(tool.name); it != policy.overrides.end()) {
    return it->second;
  }
  return tool.confirmation;
}

ToolResult make_error(std::string code, std::string message, std::string suggestion = {}) {
  ToolResult r;
  r.error = ToolError{std::move(code), std::move(message), std::move(suggestion)};
  r.summary = "error: " + r.error->message;
  return r;
}

// Map a ToolResult to the canonical audit `outcome` token. Stable
// vocabulary so external log analyzers can group / count without
// guessing.
std::string outcome_token(const ToolResult& r) {
  if (!r.error)
    return "ok";
  const auto& code = r.error->code;
  if (code == "DENIED")
    return "denied";
  if (code == "NOT_CONFIRMED")
    return "not_confirmed";
  if (code == "NOT_FOUND")
    return "not_found";
  return "fail";  // INVALID_ARGUMENT / PLUGIN_NOT_FOUND / INTERNAL / NOT_AVAILABLE / ...
}

void record_audit(ToolContext& ctx,
                  std::string_view tool_name,
                  const pipeline::Value& inputs,
                  const ToolResult& result,
                  std::chrono::milliseconds duration,
                  std::int64_t heap_bytes_delta,
                  bool heap_supported) {
  if (ctx.audit_log == nullptr)
    return;
  AuditLog::Entry e;
  e.tool_name = std::string(tool_name);
  e.outcome = outcome_token(result);
  e.summary = result.summary;
  // Same SHA-256 used for the pipeline cache key. Context prefix
  // ("audit:") namespaces it away from cache keys so an audit hash can
  // never accidentally collide with a stage cache key.
  std::string ctx_key = "audit:" + std::string(tool_name);
  e.input_hash = pipeline::hash_inputs(ctx_key, inputs, {}).hex();
  e.duration = duration;
  e.budget = ctx.budget;
  e.heap_bytes_delta = heap_bytes_delta;
  e.heap_supported = heap_supported;
  ctx.audit_log->append(e);
}

}  // namespace

ToolResult dispatch_tool(const ToolRegistry& registry,
                         std::string_view tool_name,
                         const pipeline::Value& inputs,
                         ToolContext& context,
                         ConfirmationPolicy& policy) {
  const auto start = std::chrono::steady_clock::now();
  auto elapsed = [&] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                                 - start);
  };

  const auto* tool = registry.find(tool_name);
  if (tool == nullptr) {
    auto r =
        make_error("NOT_FOUND",
                   "no tool named '" + std::string(tool_name) + "' is registered",
                   "call `souxmar agent list` (or ToolRegistry.list()) to see available tools");
    // Early-exit paths don't invoke a handler, so the heap delta is
    // omitted from the audit entry (heap_supported=false). Recording a
    // 0 here would muddy the leak-detection signal.
    record_audit(context,
                 tool_name,
                 inputs,
                 r,
                 elapsed(),
                 /*heap_bytes_delta=*/0,
                 /*heap_supported=*/false);
    return r;
  }

  // Confirmation gate. The order matches the contract in the header
  // comment: overrides → tool default → confirmed_once short-circuit →
  // prompter → reject if no prompter.
  const auto required = effective_confirmation(*tool, policy);
  if (required != Confirmation::Auto) {
    const bool already_confirmed_once =
        required == Confirmation::ConfirmOnce && policy.confirmed_once.contains(tool->name);
    if (!already_confirmed_once) {
      if (!policy.prompter) {
        auto r = make_error("NOT_CONFIRMED",
            "tool '" + tool->name + "' requires user confirmation but no prompter "
            "was supplied",
            "either set ConfirmationPolicy.prompter or add an override mapping "
            "this tool to Confirmation::Auto");
        record_audit(context,
                     tool_name,
                     inputs,
                     r,
                     elapsed(),
                     /*heap_bytes_delta=*/0,
                     /*heap_supported=*/false);
        return r;
      }
      const bool approved = policy.prompter(*tool, inputs);
      if (!approved) {
        auto r = make_error("DENIED", "user declined to run '" + tool->name + "'");
        record_audit(context,
                     tool_name,
                     inputs,
                     r,
                     elapsed(),
                     /*heap_bytes_delta=*/0,
                     /*heap_supported=*/false);
        return r;
      }
      if (required == Confirmation::ConfirmOnce) {
        policy.confirmed_once.insert(tool->name);
      }
    }
  }

  // Sprint 9 push 9 — bracket the handler call with a HeapAccountant
  // snapshot pair. The delta lands in the audit log on platforms where
  // accounting is supported (Linux + glibc ≥ 2.33 today). Cheap enough
  // (< 1 µs on the reference hardware; pinned by bench_heap_accountant)
  // to leave always-on.
  const auto heap_before = ::souxmar::plugin::HeapAccountant::snapshot();

  // Invoke handler. Catch every exception type to keep the agent
  // runtime from ever seeing a raw throw — model recovery only works
  // against structured ToolError.
  ToolResult result;
  try {
    result = tool->handler(inputs, context);
  } catch (const std::exception& e) {
    result = make_error("INTERNAL", "tool '" + tool->name + "' threw an exception: " + e.what());
  } catch (...) {
    result = make_error("INTERNAL", "tool '" + tool->name + "' threw an unknown exception");
  }

  const std::int64_t heap_delta = ::souxmar::plugin::HeapAccountant::delta_since(heap_before);
  record_audit(context, tool_name, inputs, result, elapsed(), heap_delta, heap_before.supported);
  return result;
}

}  // namespace souxmar::ai
