// SPDX-License-Identifier: Apache-2.0
//
// souxmar-eval-llm — Sprint 10 push 9 LLM-driven agent eval runner.
//
// The scripted eval runner (souxmar-eval, Sprint 7 push 4) plays the
// agent role itself, calling tools directly. That's the regression
// gate. This runner does the other half: an actual model picks the
// tool calls, the dispatcher executes them, the model sees the
// results, and the same assertions get checked.
//
// Why a separate binary (vs. extending souxmar-eval): the scripted
// runner is the load-bearing CI gate — every PR runs it, exit-0 is a
// merge gate. The LLM runner is for *compatibility-matrix generation*
// — runs against a local Ollama (or a stub provider in CI), produces
// a per-task / per-model pass-fail table that fills in
// docs/ai-providers/ollama-compatibility.md. The two flows share
// the eval task format but have different stability promises:
// souxmar-eval is reproducible byte-for-byte; souxmar-eval-llm is
// inherently subject to model behaviour.
//
// Scope of this push:
//   * The runner reads eval-task YAML (subset: id, llm_prompt,
//     min_steps, max_steps, must_call_tool, must_not_call_tool,
//     final_assertion_substring).
//   * Provider plumbing: --provider stub | ollama. Stub is for CI;
//     Ollama is for the manual compatibility-matrix runs.
//   * One-shot per task; no rate-limit handling (single local
//     daemon, no retry semantics worth coding before we observe a
//     real failure mode).
//   * Emits a JSON summary at --output <path> the compatibility-
//     matrix doc generator consumes.

#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "souxmar/ai/provider.h"
#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

namespace fs = std::filesystem;
namespace ai = souxmar::ai;
namespace pl = souxmar::pipeline;
namespace ph = souxmar::plugin;

namespace {

constexpr int kExitOk         = 0;
constexpr int kExitTaskFailed = 1;
constexpr int kExitUsage      = 2;
constexpr int kExitNoTasks    = 3;

struct LlmTask {
  std::string                id;
  std::string                llm_prompt;
  std::vector<std::string>   must_call_tool;
  std::vector<std::string>   must_not_call_tool;
  std::string                final_assertion_substring;
  std::uint32_t              max_steps = 5;
};

struct StepRecord {
  std::string                assistant_text;
  std::vector<std::string>   tool_calls;     // names only
};

struct TaskOutcome {
  std::string                task_id;
  std::string                model;
  std::string                provider;
  bool                       passed = false;
  std::string                reason;
  std::vector<StepRecord>    transcript;
  std::chrono::milliseconds  total_latency{0};
};

LlmTask parse_llm_task(const fs::path& yaml_path) {
  LlmTask t;
  auto root = YAML::LoadFile(yaml_path.string());
  if (root["id"])                        t.id = root["id"].as<std::string>();
  if (root["llm_prompt"])                t.llm_prompt = root["llm_prompt"].as<std::string>();
  if (root["max_steps"])                 t.max_steps  = root["max_steps"].as<std::uint32_t>();
  if (root["final_assertion_substring"]) {
    t.final_assertion_substring =
        root["final_assertion_substring"].as<std::string>();
  }
  if (auto n = root["must_call_tool"]; n && n.IsSequence()) {
    for (std::size_t i = 0; i < n.size(); ++i) {
      t.must_call_tool.push_back(n[i].as<std::string>());
    }
  }
  if (auto n = root["must_not_call_tool"]; n && n.IsSequence()) {
    for (std::size_t i = 0; i < n.size(); ++i) {
      t.must_not_call_tool.push_back(n[i].as<std::string>());
    }
  }
  return t;
}

std::vector<LlmTask> discover_tasks(const fs::path& dir) {
  std::vector<LlmTask> tasks;
  if (!fs::is_directory(dir)) return tasks;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const auto p = entry.path();
    if (p.extension() != ".yaml" && p.extension() != ".yml") continue;
    try {
      auto t = parse_llm_task(p);
      if (t.id.empty() || t.llm_prompt.empty()) continue;
      tasks.push_back(std::move(t));
    } catch (const std::exception& e) {
      fmt::print(stderr, "warning: {} did not parse: {}\n",
                 p.string(), e.what());
    }
  }
  std::sort(tasks.begin(), tasks.end(),
            [](const LlmTask& a, const LlmTask& b) { return a.id < b.id; });
  return tasks;
}

std::unique_ptr<ai::Provider> make_provider(const std::string& name) {
  if (name == "stub") {
    auto s = std::make_unique<ai::StubProvider>();
    // Program a deterministic reply for the smoke task — exercises
    // the LLM-driven flow on CI without a real model.
    ai::ChatResponse r;
    r.text = "I'll list the plugins to see what's available.";
    ai::ToolCall tc;
    tc.id             = "call_001";
    tc.name           = "list_plugins";
    tc.arguments_json = "{}";
    r.tool_calls.push_back(tc);
    s->program_reply("stub-model", "list", r);

    // After tool result, a final assistant-only reply.
    ai::ChatResponse final;
    final.text = "Found the in-tree plugins. Done.";
    s->program_reply("stub-model", "tool result", final);
    return s;
  }
  if (name == "ollama") {
    return std::make_unique<ai::OllamaProvider>();
  }
  return nullptr;
}

TaskOutcome run_one(ai::Provider&            provider,
                    const std::string&       model,
                    const LlmTask&           task,
                    ai::ToolContext&         ctx,
                    const ai::ToolRegistry&  registry,
                    const ai::ConfirmationPolicy& policy) {
  TaskOutcome out;
  out.task_id  = task.id;
  out.model    = model;
  out.provider = std::string(provider.name());

  std::vector<ai::ChatMessage> messages;
  messages.push_back({ai::ChatMessage::Role::System,
                      "You are the souxmar agent. Use the provided tools to "
                      "satisfy the user request. Call tools by name from the "
                      "souxmar v1 tool catalogue.",
                      {}});
  messages.push_back({ai::ChatMessage::Role::User, task.llm_prompt, {}});

  std::vector<std::string> tool_names = registry.list();

  std::vector<std::string> tools_actually_called;

  for (std::uint32_t step = 0; step < task.max_steps; ++step) {
    ai::ChatRequest req;
    req.model      = model;
    req.messages   = messages;
    req.tool_names = tool_names;
    req.temperature = 0.0;

    const auto started = std::chrono::steady_clock::now();
    auto result = provider.chat_completion(req);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
    out.total_latency += elapsed;

    if (auto* err = std::get_if<ai::ProviderError>(&result)) {
      out.reason = "provider error: " +
                   std::string(ai::to_string(err->kind)) + ": " + err->message;
      return out;
    }
    auto& resp = std::get<ai::ChatResponse>(result);

    StepRecord sr;
    sr.assistant_text = resp.text;
    for (const auto& tc : resp.tool_calls) sr.tool_calls.push_back(tc.name);
    out.transcript.push_back(sr);

    // If no tool calls + non-empty text => final assistant reply.
    if (resp.tool_calls.empty()) {
      messages.push_back({ai::ChatMessage::Role::Assistant, resp.text, {}});
      break;
    }

    // Echo the assistant turn (for chat history) + dispatch each tool
    // call + append tool-result messages.
    messages.push_back({ai::ChatMessage::Role::Assistant, resp.text, {}});
    for (const auto& tc : resp.tool_calls) {
      tools_actually_called.push_back(tc.name);
      // Parse arguments — minimal: YAML can read JSON, so treat the
      // arguments string as a Value map. For unstructured / not-JSON
      // arguments, we send an empty map.
      pl::Value inputs = pl::Value::map({});
      try {
        if (!tc.arguments_json.empty()) {
          // pipeline::parse_value_yaml accepts JSON too (YAML 1.2 superset).
          inputs = pl::parse_value_yaml(tc.arguments_json);
        }
      } catch (...) { /* leave as empty map */ }

      const auto td = ai::dispatch_tool(registry, tc.name, inputs, ctx, policy);
      std::string tool_text;
      if (td.error) {
        tool_text = "error: " + td.error->message;
      } else {
        tool_text = td.summary;
      }
      messages.push_back({ai::ChatMessage::Role::Tool,
                          "tool result: " + tool_text,
                          tc.id});
    }
  }

  // Assertions.
  for (const auto& must : task.must_call_tool) {
    if (std::find(tools_actually_called.begin(),
                  tools_actually_called.end(), must) ==
        tools_actually_called.end()) {
      out.reason = "must_call_tool '" + must + "' was never called";
      return out;
    }
  }
  for (const auto& must_not : task.must_not_call_tool) {
    if (std::find(tools_actually_called.begin(),
                  tools_actually_called.end(), must_not) !=
        tools_actually_called.end()) {
      out.reason = "must_not_call_tool '" + must_not +
                   "' was called by the model";
      return out;
    }
  }
  if (!task.final_assertion_substring.empty()) {
    const std::string final_text =
        out.transcript.empty() ? "" : out.transcript.back().assistant_text;
    if (final_text.find(task.final_assertion_substring) == std::string::npos) {
      out.reason = "final assistant text did not contain '" +
                   task.final_assertion_substring + "'";
      return out;
    }
  }
  out.passed = true;
  out.reason = "ok";
  return out;
}

void json_escape(std::string& out, std::string_view s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
  }
  out += '"';
}

std::string render_outcomes_json(const std::vector<TaskOutcome>& outs) {
  std::string s = "{\"results\":[";
  for (std::size_t i = 0; i < outs.size(); ++i) {
    if (i > 0) s += ',';
    const auto& o = outs[i];
    s += "{";
    s += "\"task_id\":"; json_escape(s, o.task_id);
    s += ",\"model\":"; json_escape(s, o.model);
    s += ",\"provider\":"; json_escape(s, o.provider);
    s += ",\"passed\":"; s += o.passed ? "true" : "false";
    s += ",\"reason\":"; json_escape(s, o.reason);
    s += ",\"latency_ms\":"; s += std::to_string(o.total_latency.count());
    s += ",\"steps\":";  s += std::to_string(o.transcript.size());
    s += "}";
  }
  s += "]}\n";
  return s;
}

void print_usage() {
  fmt::print(stderr,
      "souxmar-eval-llm — LLM-driven agent eval runner (Sprint 10 push 9)\n"
      "\n"
      "Usage:\n"
      "  souxmar-eval-llm <evals-dir> --provider <stub|ollama> --model <id>\n"
      "    [--plugin-path <dir>]...\n"
      "    [--output <path>]    JSON summary the compatibility-matrix doc consumes\n"
      "    [--only <task-id>]   run only the named task\n"
      "\n"
      "Each YAML under <evals-dir> with `llm_prompt:` is one task.\n"
      "Tasks without `llm_prompt:` are skipped (those are scripted-only).\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { print_usage(); return kExitUsage; }
  std::vector<std::string> args(argv + 1, argv + argc);
  std::string                provider_name;
  std::string                model;
  fs::path                   evals_dir;
  fs::path                   output_path;
  std::string                only_id;
  std::vector<fs::path>      plugin_paths;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--provider"    && i + 1 < args.size()) provider_name = args[++i];
    else if (a == "--model"  && i + 1 < args.size()) model         = args[++i];
    else if (a == "--output" && i + 1 < args.size()) output_path   = args[++i];
    else if (a == "--only"   && i + 1 < args.size()) only_id       = args[++i];
    else if (a == "--plugin-path" && i + 1 < args.size())
      plugin_paths.emplace_back(args[++i]);
    else if (!a.empty() && a.front() != '-' && evals_dir.empty())
      evals_dir = a;
    else if (a == "--help" || a == "-h") { print_usage(); return kExitOk; }
    else {
      fmt::print(stderr, "error: unknown flag '{}'\n", a);
      return kExitUsage;
    }
  }
  if (evals_dir.empty() || provider_name.empty() || model.empty()) {
    print_usage();
    return kExitUsage;
  }

  auto provider = make_provider(provider_name);
  if (!provider) {
    fmt::print(stderr, "error: unknown --provider '{}'\n", provider_name);
    return kExitUsage;
  }

  auto tasks = discover_tasks(evals_dir);
  if (!only_id.empty()) {
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                               [&](const LlmTask& t) {
                                 return t.id != only_id;
                               }),
                tasks.end());
  }
  if (tasks.empty()) {
    fmt::print(stderr, "no LLM-driven tasks under {}\n", evals_dir.string());
    return kExitNoTasks;
  }

  // Discover + load plugins so dispatch_tool has a populated registry.
  ph::DiscoveryOptions opts;
  opts.include_env_path = true;
  auto search = ph::default_search_paths(opts);
  search.insert(search.begin(), plugin_paths.begin(), plugin_paths.end());
  const auto report = ph::discover_plugins(search);
  ph::Registry registry;
  ph::PluginLoader loader(registry, "souxmar-eval-llm");
  for (const auto& d : report.loaded) {
    (void)loader.load(d);
  }

  pl::RegistryDispatcher dispatcher(registry);
  pl::Cache cache;
  pl::Value session = pl::Value::map({});
  ai::ToolContext ctx;
  ctx.registry      = &registry;
  ctx.dispatcher    = &dispatcher;
  ctx.cache         = &cache;
  ctx.session_state = &session;

  ai::ConfirmationPolicy policy;
  for (const auto& tn : ai::default_v1_tools().list()) {
    policy.overrides[tn] = ai::Confirmation::Auto;
  }
  const auto tool_registry = ai::default_v1_tools();

  std::vector<TaskOutcome> outcomes;
  outcomes.reserve(tasks.size());
  int failures = 0;
  for (const auto& t : tasks) {
    auto o = run_one(*provider, model, t, ctx, tool_registry, policy);
    fmt::print("[{}] {}  ({}, {}ms)\n",
               o.passed ? "PASS" : "FAIL",
               t.id, o.reason, o.total_latency.count());
    if (!o.passed) ++failures;
    outcomes.push_back(std::move(o));
  }

  if (!output_path.empty()) {
    std::ofstream sink(output_path);
    sink << render_outcomes_json(outcomes);
  }
  fmt::print("\n{} task(s), {} pass, {} fail\n",
             tasks.size(), tasks.size() - failures, failures);
  return failures == 0 ? kExitOk : kExitTaskFailed;
}
