// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 6 — unit tests for the apply-gate state machine.
// Surface in include/souxmar/update/state_machine.h; implementation in
// src/updater/state_machine.cpp; design lock-in in ADR-0013.
//
// Every RefusalReason value gets a dedicated test; every step in the
// gate's documented ordering (header comment, "Order of checks") gets
// a test that exercises *that* step's failure with later steps known
// to pass. The combined effect is that a refactor reordering the
// checks fails at least one test (regression-prevents the rollback-
// log audit-history wire-format from drifting).

#include "souxmar/update/manifest.h"
#include "souxmar/update/state_machine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <variant>

using namespace souxmar::update;

namespace {

// Convenience: build a minimal-but-valid Manifest with a single
// linux/x86_64 artifact. Tests mutate fields per-case before invoking
// the gate.
Manifest base_manifest() {
  Manifest m;
  m.schema                       = kManifestSchemaV1;
  m.generated_at                 = "2026-05-11T14:00:00Z";
  m.channel.name                 = Channel::Stable;
  m.channel.expires_at           = "2026-08-11T14:00:00Z";
  m.release.version              = "0.9.0";
  m.release.released_at          = "2026-05-10T10:00:00Z";
  m.release.min_previous_version = "0.8.0";
  m.release.rollback_target      = "0.8.4";
  m.release.notes_url            = "https://souxmar.dev/releases/0.9.0";
  m.release.mandatory            = false;
  m.signing.algorithm            = "ed25519";
  m.signing.public_key_id        = "release-2026";

  Artifact linux_x86;
  linux_x86.os     = Os::Linux;
  linux_x86.arch   = Arch::X86_64;
  linux_x86.url    = "https://dl.souxmar.dev/0.9.0/linux-x86_64.tar.zst";
  linux_x86.sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  linux_x86.size   = 48217600;
  m.artifacts.push_back(linux_x86);
  return m;
}

CurrentInstall base_install() {
  return CurrentInstall{
      .current_version       = "0.8.5",
      .max_version_ever_seen = "0.8.5",
      .platform              = HostPlatform{Os::Linux, Arch::X86_64},
  };
}

// Clock pinned to a moment well inside the manifest's expiry window.
FixedTimeSource clock_before_expiry() {
  return FixedTimeSource{
      *parse_rfc3339_utc("2026-05-12T00:00:00Z")};
}

// Clock pinned to a moment well after the expiry window.
FixedTimeSource clock_after_expiry() {
  return FixedTimeSource{
      *parse_rfc3339_utc("2027-01-01T00:00:00Z")};
}

const UpdateApply& expect_apply(const UpdateDecision& d) {
  if (auto* r = std::get_if<UpdateRefusal>(&d)) {
    ADD_FAILURE() << "expected Apply, got refusal "
                  << to_string(r->reason) << ": " << r->detail;
  }
  return std::get<UpdateApply>(d);
}

const UpdateRefusal& expect_refusal(const UpdateDecision& d,
                                    RefusalReason         expected) {
  if (auto* a = std::get_if<UpdateApply>(&d)) {
    ADD_FAILURE() << "expected refusal " << to_string(expected)
                  << ", got Apply " << a->version;
  }
  const auto& r = std::get<UpdateRefusal>(d);
  EXPECT_EQ(r.reason, expected)
      << "wrong refusal reason: got " << to_string(r.reason)
      << " ('" << r.detail << "')";
  return r;
}

}  // namespace

// ===========================================================================
// RFC-3339 parser
// ===========================================================================

TEST(UpdateRfc3339, ParsesCanonicalShape) {
  const auto tp = parse_rfc3339_utc("2026-05-11T14:00:00Z");
  ASSERT_TRUE(tp.has_value());
  // Roundtrip through format_rfc3339_utc.
  EXPECT_EQ(format_rfc3339_utc(*tp), "2026-05-11T14:00:00Z");
}

TEST(UpdateRfc3339, RejectsLowercaseZ) {
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T14:00:00z").has_value());
}

TEST(UpdateRfc3339, RejectsOffsetSuffix) {
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T14:00:00+00:00").has_value());
}

TEST(UpdateRfc3339, RejectsFractionalSeconds) {
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T14:00:00.123Z").has_value());
}

TEST(UpdateRfc3339, RejectsWrongLength) {
  EXPECT_FALSE(parse_rfc3339_utc("").has_value());
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T14:00:00").has_value());
  EXPECT_FALSE(parse_rfc3339_utc("not a timestamp at all").has_value());
}

TEST(UpdateRfc3339, RejectsOutOfRangeFields) {
  EXPECT_FALSE(parse_rfc3339_utc("2026-13-11T14:00:00Z").has_value()) << "month 13";
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-32T14:00:00Z").has_value()) << "day 32";
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T24:00:00Z").has_value()) << "hour 24";
  EXPECT_FALSE(parse_rfc3339_utc("2026-05-11T14:60:00Z").has_value()) << "minute 60";
}

// ===========================================================================
// Version compare
// ===========================================================================

TEST(UpdateVersionCompare, WellFormed) {
  EXPECT_EQ(*compare_versions("0.9.0", "0.9.0"),  0);
  EXPECT_EQ(*compare_versions("0.9.0", "0.9.1"), -1);
  EXPECT_EQ(*compare_versions("0.9.1", "0.9.0"), +1);
  EXPECT_EQ(*compare_versions("0.9.0", "0.10.0"), -1);   // numeric, not lex
  EXPECT_EQ(*compare_versions("0.10.0", "0.9.0"), +1);
  EXPECT_EQ(*compare_versions("1.0.0", "0.99.0"), +1);
}

TEST(UpdateVersionCompare, MalformedReturnsNullopt) {
  EXPECT_FALSE(compare_versions("",        "0.9.0").has_value());
  EXPECT_FALSE(compare_versions("0.9",     "0.9.0").has_value());
  EXPECT_FALSE(compare_versions("0.9.0.0", "0.9.0").has_value());
  EXPECT_FALSE(compare_versions("v0.9.0",  "0.9.0").has_value());
  EXPECT_FALSE(compare_versions("0.9.0-beta3", "0.9.0").has_value());
}

// ===========================================================================
// Host platform
// ===========================================================================

TEST(UpdateHostPlatform, ParseRoundtrip) {
  const auto p = parse_host_platform("linux/x86_64");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->os,   Os::Linux);
  EXPECT_EQ(p->arch, Arch::X86_64);

  const auto p2 = parse_host_platform("macos/aarch64");
  ASSERT_TRUE(p2.has_value());
  EXPECT_EQ(p2->os,   Os::Macos);
  EXPECT_EQ(p2->arch, Arch::Aarch64);
}

TEST(UpdateHostPlatform, RejectsMalformed) {
  EXPECT_FALSE(parse_host_platform("linux").has_value());
  EXPECT_FALSE(parse_host_platform("linux/x86_64/extra").has_value())
      << "unexpected: should reject extra components";
  EXPECT_FALSE(parse_host_platform("haiku/x86_64").has_value());
  EXPECT_FALSE(parse_host_platform("linux/riscv64").has_value());
}

// ===========================================================================
// Apply gate — happy path
// ===========================================================================

TEST(UpdateApplyGate, HappyPathApplies) {
  const auto m       = base_manifest();
  const auto install = base_install();
  const auto clk     = clock_before_expiry();
  const auto& a = expect_apply(apply_gate(m, install, clk));
  EXPECT_EQ(a.version,         "0.9.0");
  EXPECT_EQ(a.artifact.os,     Os::Linux);
  EXPECT_EQ(a.artifact.arch,   Arch::X86_64);
  EXPECT_EQ(a.mandatory,       false);
  EXPECT_EQ(a.artifact.url,
            "https://dl.souxmar.dev/0.9.0/linux-x86_64.tar.zst");
}

TEST(UpdateApplyGate, HappyPathOnFreshInstall) {
  // Empty current_version / max_seen — the gate proceeds as if any
  // version is an upgrade.
  auto install = base_install();
  install.current_version.clear();
  install.max_version_ever_seen.clear();
  const auto clk = clock_before_expiry();
  const auto& a  = expect_apply(apply_gate(base_manifest(), install, clk));
  EXPECT_EQ(a.version, "0.9.0");
}

TEST(UpdateApplyGate, HappyPathWithEmptyExpiry) {
  auto m = base_manifest();
  m.channel.expires_at.clear();  // freshness check disabled
  const auto clk = clock_after_expiry();   // doesn't matter — empty bypasses
  expect_apply(apply_gate(m, base_install(), clk));
}

TEST(UpdateApplyGate, MandatoryFlagFlowsThrough) {
  auto m       = base_manifest();
  m.release.mandatory = true;
  const auto& a = expect_apply(apply_gate(m, base_install(), clock_before_expiry()));
  EXPECT_TRUE(a.mandatory);
}

// ===========================================================================
// Apply gate — refusals
// ===========================================================================

TEST(UpdateApplyGate, RefusesMalformedOfferedVersion) {
  auto m       = base_manifest();
  m.release.version = "v0.9.0";
  expect_refusal(apply_gate(m, base_install(), clock_before_expiry()),
                 RefusalReason::MalformedOfferedVersion);
}

TEST(UpdateApplyGate, RefusesMalformedCurrentVersion) {
  auto install = base_install();
  install.current_version = "garbage";
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::MalformedCurrentVersion);
}

TEST(UpdateApplyGate, RefusesMalformedReplayFloor) {
  auto install = base_install();
  install.max_version_ever_seen = "garbage";
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::MalformedReplayFloor);
}

TEST(UpdateApplyGate, RefusesAlreadyOnOrAheadOfOffered) {
  auto install = base_install();
  install.current_version = "0.9.0";  // equal => refuse (not below)
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::AlreadyOnOrAheadOfOffered);

  install.current_version = "0.10.0";  // ahead
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::AlreadyOnOrAheadOfOffered);
}

TEST(UpdateApplyGate, RefusesExpiredManifest) {
  expect_refusal(apply_gate(base_manifest(), base_install(),
                            clock_after_expiry()),
                 RefusalReason::Expired);
}

TEST(UpdateApplyGate, RefusesMalformedExpiresAt) {
  auto m = base_manifest();
  m.channel.expires_at = "not-a-timestamp";
  expect_refusal(apply_gate(m, base_install(), clock_before_expiry()),
                 RefusalReason::ExpiredInvalidTime);
}

TEST(UpdateApplyGate, RefusesBelowMinPrevious) {
  auto install = base_install();
  install.current_version       = "0.7.0";   // < min_previous_version 0.8.0
  install.max_version_ever_seen = "0.7.0";   // satisfy replay step
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::BelowMinPrevious);
}

TEST(UpdateApplyGate, RefusesReplayDowngrade) {
  auto install = base_install();
  install.current_version       = "0.8.5";
  install.max_version_ever_seen = "1.2.0";   // > offered 0.9.0
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::ReplayDowngrade);
}

TEST(UpdateApplyGate, RefusesNoArtifactForPlatform) {
  auto install = base_install();
  install.platform = HostPlatform{Os::Windows, Arch::X86_64};
  expect_refusal(apply_gate(base_manifest(), install, clock_before_expiry()),
                 RefusalReason::NoArtifactForPlatform);
}

// ===========================================================================
// Apply gate — ordering precedence
//
// Construct a manifest + install that fails *every* step. The
// observed refusal must be the *first* step's reason. Each test below
// pins the precedence at one boundary; together they pin the full
// chain.
// ===========================================================================

TEST(UpdateApplyGate, MalformedOfferedBeatsEverythingElse) {
  auto m       = base_manifest();
  m.release.version    = "garbage";          // step 1 fails
  m.channel.expires_at = "also-garbage";     // step 5 would fail
  auto install = base_install();
  install.current_version       = "garbage"; // step 2 would fail
  install.max_version_ever_seen = "garbage"; // step 3 would fail
  expect_refusal(apply_gate(m, install, clock_after_expiry()),
                 RefusalReason::MalformedOfferedVersion);
}

TEST(UpdateApplyGate, AlreadyUpToDateBeatsExpiryCheck) {
  // current >= offered AND manifest expired. The order in the header
  // says step 4 fires before step 5/6 — locks that in.
  auto install = base_install();
  install.current_version = "1.0.0";
  expect_refusal(apply_gate(base_manifest(), install,
                            clock_after_expiry()),
                 RefusalReason::AlreadyOnOrAheadOfOffered);
}

TEST(UpdateApplyGate, ExpiryBeatsMinPreviousAndReplay) {
  // Construct: current way below min_previous, max_seen way above
  // offered, but manifest expired. Step 5/6 fires before 7/8.
  auto install = base_install();
  install.current_version       = "0.0.1";
  install.max_version_ever_seen = "9.9.9";
  expect_refusal(apply_gate(base_manifest(), install,
                            clock_after_expiry()),
                 RefusalReason::Expired);
}

TEST(UpdateApplyGate, MinPreviousBeatsReplay) {
  // current < min_previous, AND max_seen > offered. Step 7 before
  // step 8.
  auto install = base_install();
  install.current_version       = "0.7.0";   // < min_previous 0.8.0
  install.max_version_ever_seen = "9.9.9";   // would trip replay
  expect_refusal(apply_gate(base_manifest(), install,
                            clock_before_expiry()),
                 RefusalReason::BelowMinPrevious);
}

TEST(UpdateApplyGate, ReplayBeatsArtifactMissing) {
  // max_seen > offered, AND platform absent from manifest. Step 8
  // before step 9.
  auto install = base_install();
  install.max_version_ever_seen = "9.9.9";
  install.platform              = HostPlatform{Os::Windows, Arch::X86_64};
  expect_refusal(apply_gate(base_manifest(), install,
                            clock_before_expiry()),
                 RefusalReason::ReplayDowngrade);
}

// ===========================================================================
// Apply gate — RefusalReason stringification (locks wire-format)
// ===========================================================================

TEST(UpdateRefusalReason, StringRoundtrip) {
  EXPECT_EQ(to_string(RefusalReason::AlreadyOnOrAheadOfOffered),
            "already-on-or-ahead-of-offered");
  EXPECT_EQ(to_string(RefusalReason::Expired),                   "expired");
  EXPECT_EQ(to_string(RefusalReason::ExpiredInvalidTime),        "expired-invalid-time");
  EXPECT_EQ(to_string(RefusalReason::BelowMinPrevious),          "below-min-previous");
  EXPECT_EQ(to_string(RefusalReason::ReplayDowngrade),           "replay-downgrade");
  EXPECT_EQ(to_string(RefusalReason::NoArtifactForPlatform),     "no-artifact-for-platform");
  EXPECT_EQ(to_string(RefusalReason::MalformedOfferedVersion),   "malformed-offered-version");
  EXPECT_EQ(to_string(RefusalReason::MalformedCurrentVersion),   "malformed-current-version");
  EXPECT_EQ(to_string(RefusalReason::MalformedReplayFloor),      "malformed-replay-floor");
}
