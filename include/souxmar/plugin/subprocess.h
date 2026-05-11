// SPDX-License-Identifier: Apache-2.0
//
// Subprocess harness for plugin authors that drive external binaries.
//
// Sprint 8 push 1 lands this as the foundation for Sprint 8 push 2
// (OpenFOAM adapter — ADR-0009) and Sprint 8 push 3 (Blender
// importer). Future GPL'd or otherwise-isolated tools — paraFoam,
// LS-DYNA, Salome's mesh tools — use the same primitive.
//
// This is a host-side C++ utility. It is NOT part of the stable C
// ABI (`souxmar-c/*` is unchanged). The contract here is the
// internal one in-tree plugins use; out-of-tree plugins are welcome
// to copy/vendor the implementation rather than link against this
// header.
//
// Design notes:
//   - The harness is process-spawn-only. We do not expose long-lived
//     pipes, shared memory, or signal-handler protocols. Plugins that
//     need streaming should layer them on top.
//   - Stdout / stderr are captured into in-memory buffers. The caller
//     truncates at SubprocessOptions::max_capture_bytes; the default
//     keeps the audit-log entry at ≤ 64 KiB per stream per call.
//   - Timeouts are wall-clock. We do not attempt to enforce CPU-time
//     bounds (cgroups / setrlimit are platform-specific in ways that
//     don't translate cleanly to Windows).
//   - Crash isolation is what the OS gives us: a SIGSEGV in the
//     child is a non-zero exit + populated `fatal_signal` field. The
//     host process is never touched.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace souxmar::plugin {

struct SubprocessOptions {
  // argv[0] is the program. Resolved via $PATH if not absolute.
  std::vector<std::string>            argv;

  // Working directory for the child. Empty → inherit from parent.
  std::filesystem::path               work_dir;

  // Environment overrides. The child inherits the parent's environment;
  // each entry here adds-or-replaces. To clear a parent variable,
  // pass an empty string value.
  std::map<std::string, std::string>  env;

  // Wall-clock timeout. 0 (default) means "no timeout" — the harness
  // waits forever. Plugins targeting potentially-runaway tools should
  // always set a value; ADR-0009 makes this a hard rule for the
  // OpenFOAM adapter.
  std::chrono::milliseconds           timeout{0};

  // Bytes to feed on the child's stdin. Empty → close stdin immediately.
  std::string                         stdin_bytes;

  // Cap on per-stream output captured into memory. Excess is
  // dropped after this many bytes (and `stdout_truncated` /
  // `stderr_truncated` flag is set in the result). 0 means
  // unlimited; the default keeps the audit-log entry tractable.
  std::size_t                         max_capture_bytes = 64 * 1024;
};

struct SubprocessResult {
  // True iff the process was spawned successfully and ran to
  // completion (whether by clean exit, signal, or timeout). False
  // means the spawn itself failed (e.g. binary not found, fork
  // refused) — in which case `error_message` carries the reason and
  // every other field is zero/empty.
  bool                            ok                 = false;

  // Clean exit code, valid when `fatal_signal == 0 && !timed_out`.
  // 0 is "success"; anything else is the child's own failure
  // semantic, which the plugin is responsible for interpreting
  // (OpenFOAM uses 1 for "case file error," 137 for OOM, etc.).
  int                             exit_code          = 0;

  // POSIX signal that terminated the child, 0 if it exited cleanly.
  // SIGSEGV / SIGABRT / SIGBUS surface plugin crashes through here.
  // On Windows we map structured exception codes (STATUS_ACCESS_VIOLATION
  // etc.) into a synthetic signal-like value so the consumer doesn't
  // have to fork its error-handling.
  int                             fatal_signal       = 0;

  // True if the harness killed the child after `timeout` elapsed.
  bool                            timed_out          = false;

  // Captured streams. `stdout_truncated` / `stderr_truncated` is set
  // when the stream exceeded `max_capture_bytes`; the kept bytes are
  // the leading prefix (truncation tells the caller "look at the
  // child's log file for the full story").
  std::string                     stdout_bytes;
  std::string                     stderr_bytes;
  bool                            stdout_truncated   = false;
  bool                            stderr_truncated   = false;

  // Populated when `ok == false` (the spawn itself failed). Format
  // is the platform's strerror / GetLastError message.
  std::string                     error_message;

  // Wall-clock duration of the child, regardless of exit reason.
  std::chrono::milliseconds       duration{0};

  // Convenience: did the child exit cleanly with status 0? The
  // matching pattern most adapters write.
  [[nodiscard]] bool succeeded() const noexcept {
    return ok && exit_code == 0 && fatal_signal == 0 && !timed_out;
  }
};

// Run a subprocess to completion. Blocks the caller thread; the
// reentrancy guard from Sprint 4 push 2 is the cross-stage
// synchroniser, not this function.
//
// This call cannot throw — failures are reported via SubprocessResult.
// (We catch std::bad_alloc + every OS-level failure mode at the
// boundary so the plugin host never sees an exception from here.)
[[nodiscard]] SubprocessResult
run_subprocess(const SubprocessOptions& opts) noexcept;

// Optional helper: resolve the absolute path of an executable on
// $PATH, returning std::nullopt if not found. The Sprint 8 push 2
// OpenFOAM adapter uses this to probe for `simpleFoam` at plugin-
// load time, refusing to register the capability when the
// underlying binary is missing.
[[nodiscard]] std::optional<std::filesystem::path>
find_executable_on_path(std::string_view program) noexcept;

}  // namespace souxmar::plugin
