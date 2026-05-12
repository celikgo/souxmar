// SPDX-License-Identifier: Apache-2.0
//
// Crash-isolation frame around plugin calls.
//
// At Sprint 2 (this push) the implementation is a C++ try/catch wrapper:
// a plugin throwing across the ABI is caught and reported as
// SOUXMAR_E_PLUGIN_FAULT instead of unwinding through the host.
//
// The hardening Sprint 5 deliverable adds POSIX sigaction + sigsetjmp/
// siglongjmp (and Windows __try/__except via SEH on MSVC) so synchronous
// hardware signals (SIGSEGV / SIGBUS / SIGFPE / SIGILL / EXCEPTION_*) are
// also caught. The public API here will not change when that lands —
// only the implementation expands. See SPRINT_PLAN.md and
// `triaging-plugin-crash` skill.

#pragma once

#include <functional>
#include <string>

namespace souxmar::plugin {

enum class GuardOutcome {
  Ok = 0,
  CppException = 1,
  Signal = 2,  // unused at Sprint 2; reserved for the Sprint 5 hardening
  Unknown = 99,
};

struct GuardResult {
  GuardOutcome outcome;
  std::string detail;  // exception what(), signal name, or empty on Ok
};

// Run `fn` inside the crash-isolation frame. Returns Ok if `fn` returned
// normally; returns a non-Ok outcome with a human-readable detail otherwise.
//
// The frame is per-call, not per-thread; nested calls are supported.
GuardResult guard_call(const std::function<void()>& fn) noexcept;

}  // namespace souxmar::plugin
