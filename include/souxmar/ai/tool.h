// SPDX-License-Identifier: Apache-2.0
//
// Agent tool surface — the typed contract between the souxmar agent
// runtime and individual tools. The v1 tools land in Sprint 4 push 3
// per docs/AI_INTEGRATION.md; this header defines the framework they
// register with.
//
// Key design decisions:
//   * Tool inputs + outputs are `souxmar::pipeline::Value` trees. Same
//     in-memory representation the pipeline runner uses, so tools that
//     return mesh / field summaries can hand them to a follow-up tool
//     without a separate serialization step.
//   * `ToolContext` carries the runtime services a tool may reach into
//     (registry, dispatcher, cache) and the per-session state bag
//     (boundary conditions, materials, current mesh handle, etc.).
//   * Confirmation policy is structural: each tool declares its own
//     default Confirmation level; the caller's `ConfirmationPolicy` may
//     override per-tool and threads a prompter callback so the
//     mechanism is reusable across CLI / GUI / headless agent runs.
//
// What this is NOT:
//   * Not an MCP / JSON-RPC server. Wire-format serialization (Value ↔
//     JSON over stdio for AI providers) lands in Sprint 5 alongside the
//     desktop app's tool stream.
//   * Not a sandbox. Tool handlers run in-process with full host
//     privileges. Sandboxing is a Sprint 7+ hardening item.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "souxmar/pipeline/value.h"

namespace souxmar::core    { class Mesh; class Geometry; class Field; }
namespace souxmar::plugin  { class Registry; }
namespace souxmar::pipeline { class Cache; class IDispatcher; }

namespace souxmar::ai {

class AuditLog;
struct SessionBudget;

// User-confirmation requirement for a tool. Read tools default to Auto
// (no prompt); destructive / network-using tools default to higher
// levels. Match the surface described in docs/AI_INTEGRATION.md.
enum class Confirmation : std::uint8_t {
  Auto          = 0,   // executes without prompting
  ConfirmOnce   = 1,   // first call per session prompts; subsequent allowed
  ConfirmAlways = 2,   // every call prompts
};

// Structured failure surface. The model is supposed to be able to recover
// from these rather than retry blindly, so we always populate the
// human-readable `message` plus an optional `suggestion`.
struct ToolError {
  std::string code;        // e.g. "INVALID_ARGUMENT" / "NOT_AVAILABLE" / "PLUGIN_NOT_FOUND"
  std::string message;     // one-line human-readable explanation
  std::string suggestion;  // optional remediation hint
};

// What a tool returns to the dispatcher (which forwards it to the agent
// runtime).
struct ToolResult {
  pipeline::Value          data;     // structured result; consumed by follow-up tools
  std::string              summary;  // ≤ 3-line human-readable summary
  std::optional<ToolError> error;    // populated iff the tool failed
};

// Runtime services + per-session state a tool may consult / mutate.
// Constructed by the agent runtime (or the CLI's `agent invoke` shim)
// and passed to every tool the session dispatches.
struct ToolContext {
  // Plugin host services. Tools that dispatch into plugins (`mesh`,
  // `solve`) reach into these. May be null for tools that don't need them.
  plugin::Registry*               registry   = nullptr;
  pipeline::IDispatcher*          dispatcher = nullptr;
  pipeline::Cache*                cache      = nullptr;

  // Per-session metadata bag. Tools that record state ("set_bc" appends
  // to `boundary_conditions`) and tools that read state
  // ("read_geometry_summary" inspects `geometry`) agree on key names.
  // C++ callers typically point this at a local Value they own.
  pipeline::Value*                session_state = nullptr;

  // Optional owning storage for session_state. When `take_session_state`
  // is called, ToolContext takes ownership of the Value and updates the
  // raw pointer to point at the owned copy. Used by the Python binding
  // (and any embedded caller that wants to bundle state with context).
  // C++ callers with their own Value lifetime can ignore this field.
  std::unique_ptr<pipeline::Value> owned_session_state;
  void take_session_state(pipeline::Value v) {
    owned_session_state = std::make_unique<pipeline::Value>(std::move(v));
    session_state       = owned_session_state.get();
  }

  // Current focus handles. mesh / solve set these as a side effect so
  // downstream tools can consume them. Sprint 5 supersedes this with a
  // proper project state model + plugin-side serialization, but for
  // push 3 the slots make the 5-tool flow runnable.
  std::shared_ptr<core::Geometry> geometry_handle;
  std::shared_ptr<core::Mesh>     mesh_handle;
  std::shared_ptr<core::Field>    field_handle;

  // Optional audit + budget plumbing (Sprint 5 push 2). When set,
  // dispatch_tool() appends one entry to `audit_log` per invocation and
  // consults `budget` for the snapshot it records. Tools that talk to an
  // AI provider call `budget->record(input, output)` themselves; the
  // dispatcher does not synthesise token counts.
  AuditLog*           audit_log = nullptr;
  SessionBudget*      budget    = nullptr;
};

// Declaration of a single tool. The handler is the only mandatory field
// beyond `name`; the rest are documentation the agent runtime surfaces
// to the LLM at tool-selection time.
struct Tool {
  std::string                                                          name;
  std::string                                                          description;
  std::string                                                          category;          // e.g. "Read", "Mesh", "Solve", "Pipeline"
  Confirmation                                                         confirmation = Confirmation::Auto;
  std::string                                                          input_schema_doc;   // free-form for v1; JSON Schema in Sprint 5
  std::string                                                          output_schema_doc;
  std::function<ToolResult(const pipeline::Value& inputs, ToolContext& ctx)> handler;
};

// Catalogue of registered tools. Lookup is O(1) by name; lists are
// sorted alphabetically for deterministic output.
class ToolRegistry {
 public:
  ToolRegistry()  = default;
  ~ToolRegistry() = default;

  ToolRegistry(ToolRegistry&&) noexcept            = default;
  ToolRegistry& operator=(ToolRegistry&&) noexcept = default;

  ToolRegistry(const ToolRegistry&)            = delete;
  ToolRegistry& operator=(const ToolRegistry&) = delete;

  // Register a tool. Replaces any existing tool with the same name —
  // the registry is mutable so tests can override v1 defaults.
  void add(Tool tool);

  [[nodiscard]] const Tool*               find(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<std::string>  list() const;            // sorted by name
  [[nodiscard]] std::size_t               size() const noexcept;

 private:
  std::unordered_map<std::string, Tool> tools_;
};

// Per-session confirmation state + prompter. dispatch_tool() consults
// this before invoking a tool's handler. The prompter is the only
// channel through which an integration (CLI / GUI / web) surfaces a
// user prompt; if no prompter is set, ConfirmOnce / ConfirmAlways tools
// are rejected with a NOT_CONFIRMED error.
struct ConfirmationPolicy {
  // Override a tool's default Confirmation level. Useful for the
  // `--yes` CLI flag (override every tool to Auto for the current run).
  std::unordered_map<std::string, Confirmation> overrides;

  // Tools the user already confirmed in this session — short-circuits
  // ConfirmOnce on subsequent calls.
  std::unordered_set<std::string> confirmed_once;

  // The prompter: returns true to allow the tool to run, false to deny.
  // Called only when a confirmation is required (Confirmation > Auto and
  // not already in confirmed_once). May be null for headless contexts
  // where any required prompt is a hard failure.
  std::function<bool(const Tool& tool, const pipeline::Value& inputs)> prompter;
};

// Dispatch a tool by name. The contract:
//   1. Look up the tool; missing → ToolError{code="NOT_FOUND"}.
//   2. Resolve effective confirmation: policy.overrides → tool.confirmation.
//   3. If confirmation > Auto and not already confirmed_once, call
//      policy.prompter. Reject if null or returns false.
//   4. Invoke handler. Exceptions are caught and surfaced as
//      ToolError{code="INTERNAL"} so the agent never sees a raw throw.
//   5. For ConfirmOnce, mark the tool in policy.confirmed_once on success.
[[nodiscard]] ToolResult
dispatch_tool(const ToolRegistry&     registry,
              std::string_view        tool_name,
              const pipeline::Value&  inputs,
              ToolContext&            context,
              ConfirmationPolicy&     policy);

// Build the default v1 tool registry per docs/AI_INTEGRATION.md:
//   read_geometry_summary, mesh, set_bc, solve, screenshot_viewport.
// More tools join the catalogue with future RFCs.
[[nodiscard]] ToolRegistry default_v1_tools();

}  // namespace souxmar::ai
