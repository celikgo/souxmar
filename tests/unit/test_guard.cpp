// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/guard.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace souxmar::plugin;

namespace {

TEST(Guard, OkPathReturnsOk) {
  int counter = 0;
  auto result = guard_call([&] { ++counter; });
  EXPECT_EQ(result.outcome, GuardOutcome::Ok);
  EXPECT_TRUE(result.detail.empty());
  EXPECT_EQ(counter, 1);
}

TEST(Guard, StdExceptionCaught) {
  auto result = guard_call([] { throw std::runtime_error("boom"); });
  EXPECT_EQ(result.outcome, GuardOutcome::CppException);
  EXPECT_NE(result.detail.find("boom"), std::string::npos);
}

TEST(Guard, NonStdExceptionCaught) {
  auto result = guard_call([] { throw 42; });
  EXPECT_EQ(result.outcome, GuardOutcome::Unknown);
  EXPECT_FALSE(result.detail.empty());
}

TEST(Guard, NestedGuardCalls) {
  // Inner exception caught by inner guard; outer guard sees Ok.
  auto outer = guard_call([] {
    auto inner = guard_call([] { throw std::runtime_error("inner"); });
    EXPECT_EQ(inner.outcome, GuardOutcome::CppException);
  });
  EXPECT_EQ(outer.outcome, GuardOutcome::Ok);
}

}  // namespace
