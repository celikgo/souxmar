// SPDX-License-Identifier: Apache-2.0
//
// Agent audit log + per-session token budget.
//
// Per docs/AI_INTEGRATION.md every tool invocation is auditable: a line
// is appended to `.souxmar/chat/audit.log` (or a caller-specified path)
// recording the tool, an input fingerprint, the outcome, runtime, and
// the running token budget at the time of the call. The log is
// append-only — never truncated by souxmar itself. Rotation, if needed,
// is the operator's responsibility.
//
// SessionBudget is the runtime structure tools update when they call an
// AI provider. dispatch_tool reads its `consumed_*` fields when writing
// the audit entry, and fires `on_threshold` once when each of the 50%
// / 80% / 100% boundaries is crossed. Tools that don't talk to a
// provider (the Sprint 5 catalogue today) leave the counters at zero
// and produce audit lines with `budget: {input: 0, output: 0, ...}`.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace souxmar::pipeline { class Value; }

namespace souxmar::ai {

// Per-session token accounting. `max_*_tokens == 0` means "unlimited"
// for that axis (the threshold callback is suppressed). Callers update
// `consumed_*` after a provider call; the dispatcher reads them when
// writing the audit entry.
struct SessionBudget {
  std::size_t max_input_tokens   = 0;
  std::size_t max_output_tokens  = 0;
  std::size_t max_total_tokens   = 0;

  std::size_t consumed_input     = 0;
  std::size_t consumed_output    = 0;

  // Fired once per crossed threshold per axis. `pct` is the threshold
  // percentage just reached (50 / 80 / 100). `axis` is "input", "output",
  // or "total". Callbacks should NOT throw — the dispatcher invokes them
  // in line with audit writes.
  std::function<void(int pct, std::string_view axis,
                     const SessionBudget& current)>  on_threshold;

  [[nodiscard]] std::size_t consumed_total() const noexcept {
    return consumed_input + consumed_output;
  }

  // Increment the counters and fire on_threshold for any new crossings.
  // Idempotent on the threshold side: each (axis, threshold) pair fires
  // at most once per SessionBudget instance. Returns the consumed_total
  // after the update.
  std::size_t record(std::size_t input_delta, std::size_t output_delta);

 private:
  // Bitset of "we've already fired this threshold for this axis", stored
  // as a small contiguous array so the dispatcher's hot path stays cheap.
  // Layout: [input.50, input.80, input.100,
  //          output.50, output.80, output.100,
  //          total.50, total.80, total.100].
  std::uint16_t fired_thresholds_ = 0;
};

// Append-only log of tool dispatches. Thread-safe — each `append` call
// takes the internal mutex around the underlying ofstream. The log
// file is opened in append mode, so multiple souxmar processes writing
// to the same path interleave at line granularity (POSIX append +
// PIPE_BUF guarantees, Windows is best-effort).
class AuditLog {
 public:
  // Open `path` for append. Creates parent directories as needed.
  // Throws std::filesystem::filesystem_error on permission / IO failure.
  explicit AuditLog(std::filesystem::path path);
  ~AuditLog();

  AuditLog(const AuditLog&)            = delete;
  AuditLog& operator=(const AuditLog&) = delete;
  AuditLog(AuditLog&&) noexcept;
  AuditLog& operator=(AuditLog&&) noexcept;

  // One audit entry. Caller fills `outcome` ("ok" / "fail" / "denied" /
  // "not_confirmed" / "not_found" / "internal") and `summary` (the
  // human-readable line the agent UI would render). Other fields are
  // computed by the dispatcher.
  struct Entry {
    std::string                 tool_name;
    std::string                 outcome;
    std::string                 summary;
    std::string                 input_hash;     // hex SHA-256 over inputs
    std::chrono::milliseconds   duration{};
    const SessionBudget*        budget = nullptr;  // optional snapshot

    // Sprint 9 push 9 — net change in process-wide in-use heap bytes
    // across this tool's dispatch (via `souxmar::plugin::HeapAccountant`).
    // Signed: negative for net-freeing tools. Defaults to 0; the
    // dispatcher populates it on platforms where HeapAccountant is
    // supported and skips serialising the field otherwise.
    std::int64_t                heap_bytes_delta = 0;
    bool                        heap_supported   = false;
  };

  // Append one entry. Failure to write is silently ignored — the audit
  // log is best-effort, not a write barrier on the tool dispatch.
  void append(const Entry& entry);

  // Default path: `.souxmar/chat/audit.log` under `project_root` (or the
  // current working directory if `project_root` is empty). The directory
  // structure is created lazily on first append.
  [[nodiscard]] static std::filesystem::path
  default_path(const std::filesystem::path& project_root = {});

  [[nodiscard]] const std::filesystem::path& path() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::ai
