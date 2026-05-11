// SPDX-License-Identifier: Apache-2.0
//
// souxmar-eval — Sprint 7 push 4 agent eval runner.
//
// Each YAML file under `evals/v1/` declares one canonical task:
//   * setup    — initial session state
//   * steps    — sequence of agent-tool dispatches
//   * assertions — deterministic checks on per-step outputs
//
// The runner instantiates a Registry + Dispatcher + ToolContext,
// loads every discoverable plugin, executes the steps via
// dispatch_tool, and evaluates the assertions. A task passes iff
// every assertion holds.
//
// This is the agent eval suite v1 surface: scripted tool-call eval,
// not LLM-driven. The BYOK provider integration that lets an actual
// model drive the same task catalogue lands in Sprint 8+ alongside
// the desktop app; the assertion language + per-task contract here
// is the foundation.

#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
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

constexpr int kExitOk          = 0;
constexpr int kExitTaskFailed  = 1;
constexpr int kExitUsage       = 2;
constexpr int kExitNoTasks     = 3;

void print_usage() {
  fmt::print(stderr,
      "souxmar-eval — run the agent eval suite (Sprint 7 push 4)\n"
      "\n"
      "Usage:\n"
      "  souxmar-eval <evals-dir> [--plugin-path <dir>]... [--only <task-id>] [--quiet]\n"
      "\n"
      "Each YAML under <evals-dir> is one task. The runner loads every\n"
      "discoverable plugin, runs each task's steps through dispatch_tool,\n"
      "and reports pass/fail. Exit 0 iff every task passes.\n"
      "\n"
      "Authoring docs: evals/v1/README.md\n");
}

// ---------------------------------------------------------------------
// YAML → Value tree. yaml-cpp gives us a structural view; we collapse
// to the souxmar Value tree the dispatch_tool API expects.
// ---------------------------------------------------------------------
pl::Value yaml_to_value(const YAML::Node& n) {
  if (!n || n.IsNull()) return pl::Value::null_value();
  if (n.IsScalar()) {
    // yaml-cpp's scalar can be anything; try in order: bool, number,
    // then string (the StageRef `{from: id}` is handled at the map
    // path below, so we don't need to detect it here).
    try {
      const auto s = n.as<std::string>();
      if (s == "true")  return pl::Value::boolean(true);
      if (s == "false") return pl::Value::boolean(false);
    } catch (const YAML::Exception&) {}
    try {
      const auto d = n.as<double>();
      return pl::Value::number(d);
    } catch (const YAML::Exception&) {}
    return pl::Value::string(n.as<std::string>());
  }
  if (n.IsSequence()) {
    std::vector<pl::Value> items;
    items.reserve(n.size());
    for (const auto& it : n) items.push_back(yaml_to_value(it));
    return pl::Value::list(std::move(items));
  }
  if (n.IsMap()) {
    // Recognise the StageRef shorthand the pipeline parser already uses.
    if (n.size() == 1 && n["from"] && n["from"].IsScalar()) {
      return pl::Value::stage_ref(n["from"].as<std::string>());
    }
    std::map<std::string, pl::Value> m;
    for (const auto& kv : n) {
      m.emplace(kv.first.as<std::string>(), yaml_to_value(kv.second));
    }
    return pl::Value::map(std::move(m));
  }
  return pl::Value::null_value();
}

// ---------------------------------------------------------------------
// Assertion language. Tagged-union by `kind` keeps the schema small
// + the evaluator easy to debug. Each assertion targets either the
// last step (default) or an explicit `step` index.
// ---------------------------------------------------------------------
struct Assertion {
  std::string  kind;     // tool_outcome / tool_error_code / tool_data_equals /
                         // tool_data_gte / tool_data_present /
                         // tool_summary_contains
  std::size_t  step    = SIZE_MAX;   // SIZE_MAX → last step
  std::string  path;                  // dotted path into result.data, optional
  pl::Value    value{};               // expected value (if any)
  std::string  note;                  // free-form annotation for failure msg
};

Assertion parse_assertion(const YAML::Node& n, std::size_t default_step) {
  Assertion a;
  a.kind = n["kind"].as<std::string>();
  a.step = n["step"] ? n["step"].as<std::size_t>() : default_step;
  if (n["path"])  a.path  = n["path"].as<std::string>();
  if (n["value"]) a.value = yaml_to_value(n["value"]);
  if (n["note"])  a.note  = n["note"].as<std::string>();
  return a;
}

// Dotted-path lookup into a Value tree. Supports `a.b.c` for Map and
// `a.b.0` for List indices.
const pl::Value* lookup(const pl::Value& root, std::string_view path) {
  if (path.empty()) return &root;
  const pl::Value* cur = &root;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find('.', start);
    const auto seg = path.substr(
        start, end == std::string_view::npos ? path.size() - start : end - start);
    if (!cur) return nullptr;
    if (cur->kind() == pl::Value::Kind::Map) {
      const auto* child = cur->find(std::string(seg));
      if (!child) return nullptr;
      cur = child;
    } else if (cur->kind() == pl::Value::Kind::List) {
      try {
        const auto idx = std::stoul(std::string(seg));
        const auto list = cur->as_list();
        if (idx >= list.size()) return nullptr;
        cur = &list[idx];
      } catch (const std::exception&) {
        return nullptr;
      }
    } else {
      return nullptr;
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return cur;
}

bool values_equal(const pl::Value& a, const pl::Value& b) {
  if (a.kind() != b.kind()) {
    // Be lenient on number-vs-string for ergonomic assertions:
    // `value: 8` parses as Number; if the tool returned a Number we
    // compare; if it returned a String we lexically compare.
    return false;
  }
  switch (a.kind()) {
    case pl::Value::Kind::Null:   return true;
    case pl::Value::Kind::Bool:   return a.as_bool()   == b.as_bool();
    case pl::Value::Kind::Number: return a.as_number() == b.as_number();
    case pl::Value::Kind::String: return a.as_string() == b.as_string();
    default: return false;  // structural comparison not needed for v1
  }
}

// ---------------------------------------------------------------------
// Parsed eval task.
// ---------------------------------------------------------------------
struct Step {
  std::string                 tool;
  pl::Value                   inputs = pl::Value::null_value();
};

struct EvalTask {
  fs::path                source;
  std::string             id;
  std::string             description;
  std::string             category;
  pl::Value               setup_session_state = pl::Value::map({});
  bool                    auto_confirm        = true;
  std::vector<Step>       steps;
  std::vector<Assertion>  assertions;
};

EvalTask load_task(const fs::path& path) {
  EvalTask t;
  t.source = path;
  YAML::Node root = YAML::LoadFile(path.string());

  t.id          = root["id"].as<std::string>();
  t.description = root["description"] ? root["description"].as<std::string>() : "";
  t.category    = root["category"]    ? root["category"].as<std::string>()    : "general";
  if (root["auto_confirm"]) t.auto_confirm = root["auto_confirm"].as<bool>();

  if (root["setup"] && root["setup"]["session_state"]) {
    t.setup_session_state = yaml_to_value(root["setup"]["session_state"]);
  }
  for (const auto& sn : root["steps"]) {
    Step s;
    s.tool = sn["tool"].as<std::string>();
    if (sn["inputs"]) s.inputs = yaml_to_value(sn["inputs"]);
    t.steps.push_back(std::move(s));
  }
  for (const auto& an : root["assertions"]) {
    t.assertions.push_back(
        parse_assertion(an, t.steps.empty() ? 0 : t.steps.size() - 1));
  }
  return t;
}

// ---------------------------------------------------------------------
// Run one task. Returns true if every assertion held.
// ---------------------------------------------------------------------
struct TaskRunResult {
  std::string id;
  std::string category;
  bool        passed = false;
  std::string failure_reason;          // first failed assertion's message
};

TaskRunResult run_task(const EvalTask&            task,
                       ph::Registry&              registry,
                       pl::IDispatcher&           dispatcher,
                       pl::Cache&                 cache) {
  TaskRunResult r;
  r.id       = task.id;
  r.category = task.category;

  pl::Value session = task.setup_session_state;
  ai::ToolContext ctx;
  ctx.registry      = &registry;
  ctx.dispatcher    = &dispatcher;
  ctx.cache         = &cache;
  ctx.session_state = &session;

  ai::ConfirmationPolicy policy;
  if (task.auto_confirm) {
    // Always-yes prompter so ConfirmOnce / ConfirmAlways tools run
    // without bailing out at NOT_CONFIRMED.
    policy.prompter = [](const ai::Tool&, const pl::Value&) { return true; };
  }

  const auto tools = ai::default_v1_tools();
  std::vector<ai::ToolResult> results;
  results.reserve(task.steps.size());
  for (const auto& step : task.steps) {
    auto res = ai::dispatch_tool(tools, step.tool, step.inputs, ctx, policy);
    results.push_back(std::move(res));
  }

  // Evaluate assertions. Bail on the first failure with a clear msg.
  for (const auto& a : task.assertions) {
    if (a.step >= results.size()) {
      r.failure_reason = fmt::format("assertion targets step {} but the task ran {} step(s)",
                                     a.step, results.size());
      return r;
    }
    const auto& result = results[a.step];

    if (a.kind == "tool_outcome") {
      const std::string want = a.value.kind() == pl::Value::Kind::String
                                   ? std::string(a.value.as_string())
                                   : std::string{"ok"};
      const std::string got  = result.error ? "fail" : "ok";
      // We treat a populated error as "not ok"; specific codes are
      // checked via tool_error_code.
      const bool ok = (want == "ok"   && !result.error) ||
                      (want == "fail" &&  result.error);
      if (!ok) {
        r.failure_reason = fmt::format(
            "tool_outcome (step {}): expected '{}', got '{}' — {}",
            a.step, want, got, result.summary);
        return r;
      }
    }
    else if (a.kind == "tool_error_code") {
      const std::string want = std::string(a.value.as_string());
      const std::string got  = result.error ? result.error->code : "";
      if (got != want) {
        r.failure_reason = fmt::format(
            "tool_error_code (step {}): expected '{}', got '{}'",
            a.step, want, got);
        return r;
      }
    }
    else if (a.kind == "tool_data_equals") {
      const auto* v = lookup(result.data, a.path);
      if (!v) {
        r.failure_reason = fmt::format(
            "tool_data_equals (step {}): path '{}' missing in result.data",
            a.step, a.path);
        return r;
      }
      if (!values_equal(*v, a.value)) {
        r.failure_reason = fmt::format(
            "tool_data_equals (step {}, path '{}'): expected != actual",
            a.step, a.path);
        return r;
      }
    }
    else if (a.kind == "tool_data_gte") {
      const auto* v = lookup(result.data, a.path);
      if (!v || v->kind() != pl::Value::Kind::Number ||
          a.value.kind() != pl::Value::Kind::Number) {
        r.failure_reason = fmt::format(
            "tool_data_gte (step {}, path '{}'): both sides must be Number",
            a.step, a.path);
        return r;
      }
      if (!(v->as_number() >= a.value.as_number())) {
        r.failure_reason = fmt::format(
            "tool_data_gte (step {}, path '{}'): {} < {}",
            a.step, a.path, v->as_number(), a.value.as_number());
        return r;
      }
    }
    else if (a.kind == "tool_data_present") {
      if (lookup(result.data, a.path) == nullptr) {
        r.failure_reason = fmt::format(
            "tool_data_present (step {}, path '{}'): missing",
            a.step, a.path);
        return r;
      }
    }
    else if (a.kind == "tool_summary_contains") {
      const std::string needle = std::string(a.value.as_string());
      if (result.summary.find(needle) == std::string::npos) {
        r.failure_reason = fmt::format(
            "tool_summary_contains (step {}): '{}' not in summary '{}'",
            a.step, needle, result.summary);
        return r;
      }
    }
    else {
      r.failure_reason = fmt::format("unknown assertion kind '{}'", a.kind);
      return r;
    }
  }

  r.passed = true;
  return r;
}

// ---------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------
ph::DiscoveryReport discover_with_overrides(const std::vector<fs::path>& extra_paths) {
  std::vector<fs::path> all_paths = extra_paths;
  for (auto& p : ph::default_search_paths({})) all_paths.push_back(std::move(p));
  return ph::discover_plugins(all_paths);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
    print_usage();
    return args.empty() ? kExitUsage : kExitOk;
  }

  fs::path                     evals_dir;
  std::vector<fs::path>        extra_paths;
  std::optional<std::string>   only_task_id;
  bool                         quiet = false;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--plugin-path") {
      if (i + 1 >= args.size()) { fmt::print(stderr, "error: --plugin-path requires a value\n"); return kExitUsage; }
      extra_paths.emplace_back(args[++i]);
    } else if (a == "--only") {
      if (i + 1 >= args.size()) { fmt::print(stderr, "error: --only requires a value\n"); return kExitUsage; }
      only_task_id = args[++i];
    } else if (a == "--quiet") {
      quiet = true;
    } else if (!a.empty() && a.front() == '-') {
      fmt::print(stderr, "error: unknown flag '{}'\n", a);
      return kExitUsage;
    } else if (evals_dir.empty()) {
      evals_dir = a;
    } else {
      fmt::print(stderr, "error: unexpected positional '{}'\n", a);
      return kExitUsage;
    }
  }
  if (evals_dir.empty() || !fs::exists(evals_dir) || !fs::is_directory(evals_dir)) {
    fmt::print(stderr, "error: evals directory missing or not a directory\n");
    return kExitUsage;
  }

  // Plugin discovery + load.
  const auto discovery = discover_with_overrides(extra_paths);
  ph::Registry      registry;
  ph::PluginLoader  loader(registry, "souxmar-eval/0.1");
  std::vector<ph::LoadedPlugin> live;
  for (const auto& d : discovery.loaded) {
    auto r = loader.load(d);
    if (auto* err = std::get_if<ph::LoadError>(&r)) {
      fmt::print(stderr, "warning: failed to load {}: {}\n",
                 d.manifest.id, err->message);
      continue;
    }
    live.push_back(std::move(std::get<ph::LoadedPlugin>(r)));
  }
  pl::RegistryDispatcher dispatcher(registry);
  pl::Cache              cache;

  // Walk the evals dir.
  std::vector<fs::path> task_files;
  for (const auto& entry : fs::directory_iterator(evals_dir)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension().string();
    if (ext != ".yaml" && ext != ".yml") continue;
    task_files.push_back(entry.path());
  }
  std::sort(task_files.begin(), task_files.end());
  if (task_files.empty()) {
    fmt::print(stderr, "error: no .yaml tasks found under {}\n", evals_dir.string());
    return kExitNoTasks;
  }

  std::size_t total   = 0;
  std::size_t passed  = 0;
  std::map<std::string, std::pair<std::size_t, std::size_t>> per_cat;  // {pass, total}

  for (const auto& f : task_files) {
    EvalTask task;
    try {
      task = load_task(f);
    } catch (const std::exception& e) {
      fmt::print(stderr, "  [LOAD-FAIL] {}: {}\n", f.string(), e.what());
      ++total;
      continue;
    }
    if (only_task_id && task.id != *only_task_id) continue;
    ++total;

    auto r = run_task(task, registry, dispatcher, cache);
    auto& bucket = per_cat[r.category];
    bucket.second += 1;
    if (r.passed) {
      ++passed;
      bucket.first += 1;
      if (!quiet) fmt::print("  [PASS] {:<40}  {}\n", r.id, task.description);
    } else {
      fmt::print(stderr, "  [FAIL] {:<40}  {}\n", r.id, r.failure_reason);
    }
  }

  fmt::print("\n--- eval summary ---\n");
  fmt::print("{} passed / {} total\n", passed, total);
  for (const auto& [cat, counts] : per_cat) {
    fmt::print("  {:<14}  {} / {}\n", cat, counts.first, counts.second);
  }

  return passed == total ? kExitOk : kExitTaskFailed;
}
