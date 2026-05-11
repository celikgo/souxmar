// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/guard.h"

#include <exception>

namespace souxmar::plugin {

GuardResult guard_call(const std::function<void()>& fn) noexcept {
  // Sprint 2 push 2 (this push) implements the C++ exception leg only. The
  // signal/SEH leg lands in Sprint 5 hardening per SPRINT_PLAN.md and
  // ENGINEERING_PRACTICES.md. Adding it later is purely additive — the
  // returned outcome enum already has a `Signal` value reserved for it,
  // and callers branching on Outcome::Ok vs everything-else need not change.
  try {
    fn();
    return {GuardOutcome::Ok, {}};
  } catch (const std::exception& e) {
    return {GuardOutcome::CppException, e.what() ? e.what() : "(empty what())"};
  } catch (...) {
    return {GuardOutcome::Unknown, "non-std exception"};
  }
}

}  // namespace souxmar::plugin
