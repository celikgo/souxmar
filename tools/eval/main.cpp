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

#include <algorithm>
#include <chrono>
#include <cmath>
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

constexpr int kExitOk            = 0;
constexpr int kExitTaskFailed    = 1;
constexpr int kExitUsage         = 2;
constexpr int kExitNoTasks       = 3;
constexpr int kExitLatencyFailed = 4;  // Sprint 9 push 10 — p95 gate breach
constexpr int kExitPassRateGate  = 5;  // Sprint 11 push 3 — --min-pass-rate breach

void print_usage() {
  fmt::print(stderr,
      "souxmar-eval — run the agent eval suite (Sprint 7 push 4)\n"
      "\n"
      "Usage:\n"
      "  souxmar-eval <evals-dir>\n"
      "    [--plugin-path <dir>]...\n"
      "    [--only <task-id>]\n"
      "    [--quiet]\n"
      "    [--latency-output <path>]   write per-step latency summary JSON\n"
      "    [--max-p95-ms <number>]     fail if aggregate p95 > this many ms\n"
      "    [--min-pass-rate <0..1>]    fail if tasks_passed / tasks_total < this\n"
      "\n"
      "Each YAML under <evals-dir> is one task. The runner loads every\n"
      "discoverable plugin, runs each task's steps through dispatch_tool,\n"
      "and reports pass/fail. Exit 0 iff every task passes.\n"
      "\n"
      "Sprint 9 push 10 — per-step wall-clock latency is captured for\n"
      "every dispatched step. --latency-output emits a JSON aggregate\n"
      "(p50 / p95 / p99 / mean / max in ms) the perf dashboard / release\n"
      "notes consume. --max-p95-ms is the gate behind the\n"
      "ENGINEERING_PRACTICES.md § Performance budgets line "
      "'First chat token (BYOK direct) < 800 ms p95' — for the scripted\n"
      "eval surface this is dispatcher overhead today; the same number\n"
      "carries first-token latency once the LLM provider integration\n"
      "lands.\n"
      "\n"
      "Authoring docs: evals/v1/README.md\n");
}

// Sprint 9 push 10 — percentile helpers. Sort once on entry, then
// read the requested quantiles directly. For tiny eval suites
// (30 tasks today) the nearest-rank pick is the right shape — finer
// interpolation adds complexity without changing the regression
// signal at this scale.
double percentile(std::vector<double>& sorted_ms, double q) {
  if (sorted_ms.empty()) return 0.0;
  // nearest-rank, 1-indexed: rank = ceil(q * n); clamp to [1, n].
  const auto n = static_cast<double>(sorted_ms.size());
  std::size_t rank = static_cast<std::size_t>(std::ceil(q * n));
  if (rank == 0) rank = 1;
  if (rank > sorted_ms.size()) rank = sorted_ms.size();
  return sorted_ms[rank - 1];
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
  std::string                    id;
  std::string                    category;
  bool                           passed = false;
  std::string                    failure_reason;     // first failed assertion's message
  // Sprint 9 push 10 — per-step wall-clock latency. One entry per
  // dispatched step in the same order the task declared them. Used
  // by the aggregate p50 / p95 / p99 surfacing below; once the LLM
  // provider integration lands these become first-token latencies
  // and feed the ENGINEERING_PRACTICES.md § Performance budgets
  // "First chat token (BYOK direct) < 800 ms p95" target directly.
  std::vector<double>            step_durations_ms;
  std::vector<std::string>       step_tool_names;
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
  r.step_durations_ms.reserve(task.steps.size());
  r.step_tool_names.reserve(task.steps.size());
  for (const auto& step : task.steps) {
    const auto t0 = std::chrono::steady_clock::now();
    auto res = ai::dispatch_tool(tools, step.tool, step.inputs, ctx, policy);
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.step_durations_ms.push_back(ms);
    r.step_tool_names.push_back(step.tool);
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
  std::optional<fs::path>      latency_output;       // --latency-output <path>
  std::optional<double>        max_p95_ms;           // --max-p95-ms <N>
  // Sprint 11 push 3 — pass-rate gate. The aggregate pass rate
  // (tasks_passed / tasks_total) must be ≥ this fraction or the
  // runner exits with kExitPassRateGate. The default soft target
  // (90 %) matches the Sprint 11 exit criterion in SPRINT_PLAN.md.
  std::optional<double>        min_pass_rate;        // --min-pass-rate <N>, e.g. 0.90

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
    } else if (a == "--latency-output") {
      if (i + 1 >= args.size()) {
        fmt::print(stderr, "error: --latency-output requires a value\n");
        return kExitUsage;
      }
      latency_output = args[++i];
    } else if (a == "--max-p95-ms") {
      if (i + 1 >= args.size()) {
        fmt::print(stderr, "error: --max-p95-ms requires a value\n");
        return kExitUsage;
      }
      try {
        max_p95_ms = std::stod(args[++i]);
      } catch (...) {
        fmt::print(stderr, "error: --max-p95-ms takes a numeric value\n");
        return kExitUsage;
      }
    } else if (a == "--min-pass-rate") {
      if (i + 1 >= args.size()) {
        fmt::print(stderr, "error: --min-pass-rate requires a value\n");
        return kExitUsage;
      }
      try {
        const double v = std::stod(args[++i]);
        if (v < 0.0 || v > 1.0) {
          fmt::print(stderr,
              "error: --min-pass-rate must be in [0.0, 1.0] (got {})\n", v);
          return kExitUsage;
        }
        min_pass_rate = v;
      } catch (...) {
        fmt::print(stderr,
            "error: --min-pass-rate takes a numeric value in [0.0, 1.0]\n");
        return kExitUsage;
      }
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

  // Sprint 9 push 10 — per-step latency accumulator. Every step
  // across every task contributes one number; aggregates compute
  // off this vector at the end. Per-tool slowest-step is captured
  // too so the dashboard / summary can surface "tool X dominates
  // p95" without re-running anything.
  std::vector<double>                       all_step_ms;
  std::map<std::string, std::vector<double>> per_tool_ms;

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

    // Accumulate the per-step latencies regardless of pass/fail —
    // a failed assertion doesn't make the step's dispatch latency
    // less real, and the gate's job is to catch dispatcher slowdowns
    // even when the catalogue's coverage isn't 100%.
    for (std::size_t i = 0; i < r.step_durations_ms.size(); ++i) {
      all_step_ms.push_back(r.step_durations_ms[i]);
      per_tool_ms[r.step_tool_names[i]].push_back(r.step_durations_ms[i]);
    }
  }

  fmt::print("\n--- eval summary ---\n");
  fmt::print("{} passed / {} total\n", passed, total);
  for (const auto& [cat, counts] : per_cat) {
    fmt::print("  {:<14}  {} / {}\n", cat, counts.first, counts.second);
  }

  // ---- Latency aggregate (Sprint 9 push 10) ----
  double p50 = 0, p95 = 0, p99 = 0, mean = 0, max_ms = 0;
  if (!all_step_ms.empty()) {
    auto sorted = all_step_ms;
    std::sort(sorted.begin(), sorted.end());
    p50    = percentile(sorted, 0.50);
    p95    = percentile(sorted, 0.95);
    p99    = percentile(sorted, 0.99);
    max_ms = sorted.back();
    double sum = 0.0;
    for (double v : sorted) sum += v;
    mean = sum / static_cast<double>(sorted.size());

    fmt::print("\n--- step latency (ms) ---\n");
    fmt::print("  n={:<6}  p50={:>7.2f}  p95={:>7.2f}  p99={:>7.2f}  mean={:>7.2f}  max={:>7.2f}\n",
               sorted.size(), p50, p95, p99, mean, max_ms);
  } else {
    fmt::print("\n--- step latency ---\n  (no steps dispatched — nothing to time)\n");
  }

  // --latency-output: per-tool + aggregate JSON written to disk for
  // the perf dashboard / release-notes attachment.
  if (latency_output) {
    std::ofstream out(*latency_output);
    if (!out.is_open()) {
      fmt::print(stderr, "error: cannot open --latency-output '{}' for writing\n",
                 latency_output->string());
      return kExitUsage;
    }
    out << "{\n";
    out << "  \"unit\": \"ms\",\n";
    out << "  \"n_steps\": " << all_step_ms.size() << ",\n";
    out << "  \"aggregate\": {\n";
    out << "    \"p50\":  " << p50    << ",\n";
    out << "    \"p95\":  " << p95    << ",\n";
    out << "    \"p99\":  " << p99    << ",\n";
    out << "    \"mean\": " << mean   << ",\n";
    out << "    \"max\":  " << max_ms << "\n";
    out << "  },\n";
    out << "  \"per_tool\": {\n";
    std::size_t printed = 0;
    for (auto& [tool, samples] : per_tool_ms) {
      auto sorted = samples;
      std::sort(sorted.begin(), sorted.end());
      const double tp50  = percentile(sorted, 0.50);
      const double tp95  = percentile(sorted, 0.95);
      const double tmax  = sorted.back();
      double sum = 0.0;
      for (double v : sorted) sum += v;
      const double tmean = sum / static_cast<double>(sorted.size());
      out << "    \"" << tool << "\": {"
          << "\"n\": "    << sorted.size()
          << ", \"p50\":  " << tp50
          << ", \"p95\":  " << tp95
          << ", \"mean\": " << tmean
          << ", \"max\":  " << tmax
          << "}";
      if (++printed < per_tool_ms.size()) out << ",";
      out << "\n";
    }
    out << "  }\n";
    out << "}\n";
    out.flush();
    fmt::print("  latency JSON: {}\n", latency_output->string());
  }

  // --max-p95-ms: hard gate. We exit non-zero with a distinct code
  // (kExitLatencyFailed = 4) so the workflow can tell a latency
  // regression apart from a task-correctness failure (kExitTaskFailed
  // = 1) in its on-failure routing.
  if (max_p95_ms && !all_step_ms.empty() && p95 > *max_p95_ms) {
    fmt::print(stderr,
        "\nERROR: step latency p95 = {:.2f} ms exceeds budget {:.2f} ms\n",
        p95, *max_p95_ms);
    if (passed == total) return kExitLatencyFailed;
    // Combined failure: surface both, but the latency exit code wins
    // (correctness is still reported above).
    return kExitLatencyFailed;
  }

  // --min-pass-rate: Sprint 11 push 3 soft gate. Allows the eval
  // workflow to ratchet a "≥ X% pass" target without flipping
  // every-task-must-pass. The Sprint 11 exit criterion in
  // SPRINT_PLAN.md is 90%; the workflow can tighten over time.
  // Distinct exit code (kExitPassRateGate = 5) lets CI tell the
  // routes apart.
  if (min_pass_rate && total > 0) {
    const double rate = static_cast<double>(passed) /
                        static_cast<double>(total);
    if (rate < *min_pass_rate) {
      // fmt 11's stricter compile-time format-spec parser rejects
      // `{:.1%}`; multiply * 100 and use `{:.1f}%` instead.
      fmt::print(stderr,
          "\nERROR: pass rate {:.1f}% below gate {:.1f}% ({} / {} passed)\n",
          rate * 100.0, *min_pass_rate * 100.0, passed, total);
      return kExitPassRateGate;
    }
    fmt::print("  pass-rate gate: {:.1f}% ≥ {:.1f}% (ok)\n",
               rate * 100.0, *min_pass_rate * 100.0);
  }

  return passed == total ? kExitOk : kExitTaskFailed;
}
