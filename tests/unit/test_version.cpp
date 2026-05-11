// SPDX-License-Identifier: Apache-2.0
//
// Smoke tests for souxmar::version() / ::version_string() / ::abi_version().
// These are the canary tests for the build system itself: if they pass on
// every CI matrix entry, the toolchain + vcpkg + CMake wiring is healthy.

#include "souxmar/version.h"

#include <gtest/gtest.h>

namespace {

TEST(Version, ComponentsAreFiniteIntegers) {
  const auto v = souxmar::version();
  // major/minor/patch are unsigned, so non-negative is guaranteed by the type.
  // The sanity check we want is "they were initialised" — non-equal to garbage.
  EXPECT_LT(v.major, 1000u);
  EXPECT_LT(v.minor, 1000u);
  EXPECT_LT(v.patch, 1000u);
}

TEST(Version, StringMatchesComponents) {
  const auto v = souxmar::version();
  const auto s = souxmar::version_string();

  // The string form should at least begin with the major version's decimal form.
  ASSERT_FALSE(s.empty());
  const std::string expected_prefix = std::to_string(v.major) + ".";
  EXPECT_EQ(s.substr(0, expected_prefix.size()), expected_prefix);
}

TEST(Version, AbiIsOneForV1Series) {
  // ADR-0001: ABI v1 is frozen for the entire 1.x release series.
  EXPECT_EQ(souxmar::abi_version(), 1u);
}

}  // namespace
