// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 7 — unit tests for apply_update + rollback. The
// signature path is exercised by the integration test
// (test_update_apply_rollback_cli.cpp); here we drive the orchestrator
// directly with hand-built manifests + payloads + state, focusing on
// the per-outcome branches.

#include "souxmar/update/apply.h"
#include "souxmar/update/install_layout.h"
#include "souxmar/update/manifest.h"
#include "souxmar/update/rollback_log.h"
#include "souxmar/update/state_machine.h"
#include "souxmar/update/update_state.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace souxmar::update;
namespace fs = std::filesystem;

namespace {

fs::path tmp_dir(std::string_view tag) {
  std::random_device rd;
  auto p = fs::temp_directory_path()
           / ("souxmar-apply-" + std::string(tag) + "-" + std::to_string(rd()));
  fs::create_directories(p);
  return p;
}

std::vector<std::uint8_t> bytes_of(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data() + s.size())};
}

// Build a manifest that names a single linux/x86_64 artifact bound to
// the given payload's sha256 + size.
Manifest manifest_for(const std::string& version,
                      std::span<const std::uint8_t> payload,
                      const std::string& expires_at = "2026-12-31T00:00:00Z") {
  Manifest m;
  m.schema = kManifestSchemaV1;
  m.channel.name = Channel::Stable;
  m.channel.expires_at = expires_at;
  m.release.version = version;
  m.release.min_previous_version = "0.0.0";
  m.release.rollback_target = "0.0.0";
  m.release.mandatory = false;
  m.signing.algorithm = "ed25519";
  m.signing.public_key_id = "release-test";

  Artifact a;
  a.os = Os::Linux;
  a.arch = Arch::X86_64;
  a.url = "https://dl.souxmar.dev/" + version + "/linux-x86_64.tar.zst";
  a.sha256 = sha256_hex(payload);
  a.size = payload.size();
  m.artifacts.push_back(a);
  return m;
}

FixedTimeSource clock_t(const char* rfc3339) {
  return FixedTimeSource{*parse_rfc3339_utc(rfc3339)};
}

}  // namespace

TEST(ApplyUpdate, HappyPathStagesSwitchesAppendsAndBumpsState) {
  const auto root = tmp_dir("happy");
  InstallLayout layout(root);
  const auto payload = bytes_of("payload 0.9.0");
  const auto m = manifest_for("0.9.0", payload);

  UpdateState st;
  st.current_installed_version = "0.8.5";
  st.max_version_ever_seen = "0.8.5";
  auto clk = clock_t("2026-05-12T00:00:00Z");

  // Pretend the previous install is already on disk (we don't
  // actually need it for the happy path; just makes the from_version
  // tracking realistic).
  ASSERT_TRUE(layout.stage_version("0.8.5", bytes_of("v0.8.5")));
  ASSERT_TRUE(layout.atomic_switch_to("", "0.8.5"));

  ApplyContext ctx;
  ctx.manifest = &m;
  ctx.artifact_bytes = payload;
  ctx.layout = &layout;
  ctx.state = &st;
  ctx.clock = &clk;
  ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
  ctx.current_version = "0.8.5";

  const auto r = apply_update(ctx);
  EXPECT_EQ(r.outcome, ApplyOutcome::Applied) << r.detail;
  EXPECT_EQ(r.applied_version, "0.9.0");

  EXPECT_EQ(layout.read_current_version(), "0.9.0");
  EXPECT_EQ(layout.read_previous_version(), "0.8.5");
  EXPECT_TRUE(layout.has_version_payload("0.9.0"));

  EXPECT_EQ(st.current_installed_version, "0.9.0");
  EXPECT_EQ(st.max_version_ever_seen, "0.9.0");
  EXPECT_EQ(st.last_apply_at, "2026-05-12T00:00:00Z");

  auto log = load_rollback_log(layout.rollback_log_path());
  ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(log));
  const auto& events = std::get<std::vector<RollbackEvent>>(log);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, RollbackEventType::Apply);
  EXPECT_EQ(events[0].from_version, "0.8.5");
  EXPECT_EQ(events[0].to_version, "0.9.0");
  EXPECT_EQ(events[0].artifact_sha256, m.artifacts[0].sha256);
  EXPECT_EQ(events[0].public_key_id, "release-test");
  fs::remove_all(root);
}

TEST(ApplyUpdate, RefusesArtifactHashMismatch) {
  const auto root = tmp_dir("hash-mismatch");
  InstallLayout layout(root);
  const auto payload = bytes_of("right");
  auto m = manifest_for("0.9.0", payload);
  m.artifacts[0].sha256 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");
  ApplyContext ctx;
  ctx.manifest = &m;
  ctx.artifact_bytes = payload;
  ctx.layout = &layout;
  ctx.state = &st;
  ctx.clock = &clk;
  ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
  ctx.current_version = "0.8.5";

  EXPECT_EQ(apply_update(ctx).outcome, ApplyOutcome::ArtifactHashMismatch);
  EXPECT_TRUE(layout.read_current_version().empty()) << "must not switch";
  EXPECT_TRUE(st.current_installed_version.empty()) << "must not mutate state";
  fs::remove_all(root);
}

TEST(ApplyUpdate, RefusesArtifactSizeMismatch) {
  const auto root = tmp_dir("size-mismatch");
  InstallLayout layout(root);
  const auto payload = bytes_of("right size");
  auto m = manifest_for("0.9.0", payload);
  m.artifacts[0].size = payload.size() + 1;

  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");
  ApplyContext ctx;
  ctx.manifest = &m;
  ctx.artifact_bytes = payload;
  ctx.layout = &layout;
  ctx.state = &st;
  ctx.clock = &clk;
  ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
  ctx.current_version = "0.8.5";

  EXPECT_EQ(apply_update(ctx).outcome, ApplyOutcome::ArtifactSizeMismatch);
  fs::remove_all(root);
}

TEST(ApplyUpdate, RefusesViaGateWhenAlreadyAhead) {
  const auto root = tmp_dir("ahead");
  InstallLayout layout(root);
  const auto payload = bytes_of("payload");
  const auto m = manifest_for("0.9.0", payload);

  UpdateState st;
  st.current_installed_version = "1.0.0";
  auto clk = clock_t("2026-05-12T00:00:00Z");
  ApplyContext ctx;
  ctx.manifest = &m;
  ctx.artifact_bytes = payload;
  ctx.layout = &layout;
  ctx.state = &st;
  ctx.clock = &clk;
  ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
  ctx.current_version = "1.0.0";

  const auto r = apply_update(ctx);
  EXPECT_EQ(r.outcome, ApplyOutcome::RefusedByGate);
  EXPECT_EQ(r.refusal, RefusalReason::AlreadyOnOrAheadOfOffered);
  fs::remove_all(root);
}

TEST(ApplyUpdate, GcReapsOldVersionsAfterSuccessfulApply) {
  const auto root = tmp_dir("gc-after-apply");
  InstallLayout layout(root);
  // Pre-stage 0.7.0 (stale; not current, not previous after the
  // apply). It should be reaped at the end of apply_update.
  ASSERT_TRUE(layout.stage_version("0.7.0", bytes_of("v0.7.0")));
  ASSERT_TRUE(layout.stage_version("0.8.5", bytes_of("v0.8.5")));
  ASSERT_TRUE(layout.atomic_switch_to("", "0.8.5"));

  const auto payload = bytes_of("payload 0.9.0");
  const auto m = manifest_for("0.9.0", payload);

  UpdateState st;
  st.current_installed_version = "0.8.5";
  auto clk = clock_t("2026-05-12T00:00:00Z");
  ApplyContext ctx;
  ctx.manifest = &m;
  ctx.artifact_bytes = payload;
  ctx.layout = &layout;
  ctx.state = &st;
  ctx.clock = &clk;
  ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
  ctx.current_version = "0.8.5";

  ASSERT_EQ(apply_update(ctx).outcome, ApplyOutcome::Applied);
  EXPECT_FALSE(layout.has_version_payload("0.7.0")) << "should be gc'd";
  EXPECT_TRUE(layout.has_version_payload("0.8.5")) << "previous preserved";
  EXPECT_TRUE(layout.has_version_payload("0.9.0")) << "current preserved";
  fs::remove_all(root);
}

// ============================================================================
// rollback
// ============================================================================

TEST(Rollback, FlipsCurrentToPrevious) {
  const auto root = tmp_dir("rb-happy");
  InstallLayout layout(root);
  const auto payload_v1 = bytes_of("v0.8.5");
  const auto payload_v2 = bytes_of("v0.9.0");

  // Set up: apply 0.8.5 then 0.9.0 so the rollback log + the layout
  // both record a real history.
  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");

  // Step 1: apply 0.8.5 onto a fresh root.
  {
    const auto m = manifest_for("0.8.5", payload_v1);
    ApplyContext ctx;
    ctx.manifest = &m;
    ctx.artifact_bytes = payload_v1;
    ctx.layout = &layout;
    ctx.state = &st;
    ctx.clock = &clk;
    ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
    ASSERT_EQ(apply_update(ctx).outcome, ApplyOutcome::Applied);
  }
  // Step 2: apply 0.9.0.
  {
    const auto m = manifest_for("0.9.0", payload_v2);
    ApplyContext ctx;
    ctx.manifest = &m;
    ctx.artifact_bytes = payload_v2;
    ctx.layout = &layout;
    ctx.state = &st;
    ctx.clock = &clk;
    ctx.platform = HostPlatform{Os::Linux, Arch::X86_64};
    ctx.current_version = "0.8.5";
    ASSERT_EQ(apply_update(ctx).outcome, ApplyOutcome::Applied);
  }
  ASSERT_EQ(layout.read_current_version(), "0.9.0");

  // Rollback.
  RollbackContext rctx{&layout, &st, &clk};
  const auto r = rollback(rctx);
  EXPECT_EQ(r.outcome, RollbackOutcome::RolledBack);
  EXPECT_EQ(r.from_version, "0.9.0");
  EXPECT_EQ(r.to_version, "0.8.5");

  EXPECT_EQ(layout.read_current_version(), "0.8.5");
  EXPECT_EQ(st.current_installed_version, "0.8.5");
  // Replay defence floor must NOT drop.
  EXPECT_EQ(st.max_version_ever_seen, "0.9.0");

  auto log = load_rollback_log(layout.rollback_log_path());
  ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(log));
  const auto& events = std::get<std::vector<RollbackEvent>>(log);
  ASSERT_EQ(events.size(), 3u);  // apply, apply, rollback
  EXPECT_EQ(events.back().type, RollbackEventType::Rollback);
  EXPECT_EQ(events.back().from_version, "0.9.0");
  EXPECT_EQ(events.back().to_version, "0.8.5");
  fs::remove_all(root);
}

TEST(Rollback, NoCurrentInstallReturnsNoCurrent) {
  const auto root = tmp_dir("rb-no-current");
  InstallLayout layout(root);
  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");
  RollbackContext rctx{&layout, &st, &clk};
  EXPECT_EQ(rollback(rctx).outcome, RollbackOutcome::NoCurrentInstall);
  fs::remove_all(root);
}

TEST(Rollback, NoTargetReturnsNoTarget) {
  // Fresh install on 0.9.0; no apply event in the log; previous.txt
  // also empty. Nothing to roll back to.
  const auto root = tmp_dir("rb-no-target");
  InstallLayout layout(root);
  ASSERT_TRUE(layout.stage_version("0.9.0", bytes_of("x")));
  ASSERT_TRUE(layout.atomic_switch_to("", "0.9.0"));

  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");
  RollbackContext rctx{&layout, &st, &clk};
  EXPECT_EQ(rollback(rctx).outcome, RollbackOutcome::NoRollbackTarget);
  fs::remove_all(root);
}

TEST(Rollback, TargetPayloadMissingReturnsThatOutcome) {
  // Build a layout where current.txt says 0.9.0, previous.txt says
  // 0.8.5, but the 0.8.5 payload directory was removed (gc'd
  // manually by a user).
  const auto root = tmp_dir("rb-missing-payload");
  InstallLayout layout(root);
  ASSERT_TRUE(layout.stage_version("0.8.5", bytes_of("v0.8.5")));
  ASSERT_TRUE(layout.stage_version("0.9.0", bytes_of("v0.9.0")));
  ASSERT_TRUE(layout.atomic_switch_to("0.8.5", "0.9.0"));
  // Now blow away 0.8.5's payload directly.
  fs::remove_all(root / "versions" / "0.8.5");
  ASSERT_FALSE(layout.has_version_payload("0.8.5"));

  UpdateState st;
  auto clk = clock_t("2026-05-12T00:00:00Z");
  RollbackContext rctx{&layout, &st, &clk};
  EXPECT_EQ(rollback(rctx).outcome, RollbackOutcome::TargetPayloadMissing);
  // current.txt unchanged.
  EXPECT_EQ(layout.read_current_version(), "0.9.0");
  fs::remove_all(root);
}
