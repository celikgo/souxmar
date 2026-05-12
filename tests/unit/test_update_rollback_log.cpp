// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 7 — unit tests for the rollback / apply event log.

#include "souxmar/update/rollback_log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <variant>
#include <vector>

using namespace souxmar::update;
namespace fs = std::filesystem;

namespace {

fs::path tmp_dir() {
  std::random_device rd;
  auto p = fs::temp_directory_path() / ("souxmar-rollback-log-" + std::to_string(rd()));
  fs::create_directories(p);
  return p;
}

RollbackEvent apply_event() {
  RollbackEvent e;
  e.timestamp = "2026-05-12T10:00:00Z";
  e.type = RollbackEventType::Apply;
  e.from_version = "0.8.5";
  e.to_version = "0.9.0";
  e.artifact_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  e.public_key_id = "release-2026";
  return e;
}

RollbackEvent rollback_event() {
  RollbackEvent e;
  e.timestamp = "2026-05-12T11:30:00Z";
  e.type = RollbackEventType::Rollback;
  e.from_version = "0.9.0";
  e.to_version = "0.8.5";
  return e;
}

}  // namespace

TEST(RollbackLog, EmptyLogPathReturnsEmptyVector) {
  const auto dir = tmp_dir();
  auto r = load_rollback_log(dir / "does-not-exist.toml");
  ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(r));
  EXPECT_TRUE(std::get<std::vector<RollbackEvent>>(r).empty());
  fs::remove_all(dir);
}

TEST(RollbackLog, AppendsAcrossMultipleCalls) {
  const auto dir = tmp_dir();
  const auto path = dir / "rollback.log";
  ASSERT_TRUE(append_rollback_event(path, apply_event()));
  ASSERT_TRUE(append_rollback_event(path, rollback_event()));

  auto r = load_rollback_log(path);
  ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(r));
  const auto& events = std::get<std::vector<RollbackEvent>>(r);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, RollbackEventType::Apply);
  EXPECT_EQ(events[0].to_version, "0.9.0");
  EXPECT_EQ(events[1].type, RollbackEventType::Rollback);
  EXPECT_EQ(events[1].to_version, "0.8.5");
  fs::remove_all(dir);
}

TEST(RollbackLog, RenderRoundtripsWithoutLoss) {
  std::vector<RollbackEvent> in{apply_event(), rollback_event()};
  const auto text = render_rollback_log(in);
  EXPECT_NE(text.find("schema = 1"), std::string::npos);
  EXPECT_NE(text.find("type            = \"apply\""), std::string::npos);
  EXPECT_NE(text.find("type            = \"rollback\""), std::string::npos);
  EXPECT_NE(text.find("artifact_sha256 = \"0123"), std::string::npos);
}

TEST(RollbackLog, RejectsUnknownSchema) {
  const auto dir = tmp_dir();
  const auto path = dir / "rollback.log";
  std::ofstream(path) << "schema = 999\n";
  auto r = load_rollback_log(path);
  ASSERT_TRUE(std::holds_alternative<RollbackLogLoadError>(r));
  EXPECT_NE(std::get<RollbackLogLoadError>(r).message.find("999"), std::string::npos);
  fs::remove_all(dir);
}

TEST(RollbackLog, RejectsUnknownEventType) {
  const auto dir = tmp_dir();
  const auto path = dir / "rollback.log";
  std::ofstream(path) << "schema = 1\n\n"
                      << "[[event]]\n"
                      << "timestamp = \"2026-05-12T10:00:00Z\"\n"
                      << "type = \"sideways\"\n"
                      << "from_version = \"0.8.5\"\n"
                      << "to_version = \"0.9.0\"\n"
                      << "artifact_sha256 = \"\"\n"
                      << "public_key_id = \"\"\n";
  auto r = load_rollback_log(path);
  ASSERT_TRUE(std::holds_alternative<RollbackLogLoadError>(r));
  EXPECT_NE(std::get<RollbackLogLoadError>(r).message.find("sideways"), std::string::npos);
  fs::remove_all(dir);
}

TEST(RollbackLog, RejectsMalformedToml) {
  const auto dir = tmp_dir();
  const auto path = dir / "rollback.log";
  std::ofstream(path) << "this = = is = = not toml\n";
  EXPECT_TRUE(std::holds_alternative<RollbackLogLoadError>(load_rollback_log(path)));
  fs::remove_all(dir);
}

TEST(RollbackLog, AppendRefusesIfExistingLogIsCorrupt) {
  // A corrupt log must fail-loud — append must not silently
  // overwrite it. Without this rail, a malicious local actor could
  // truncate audit history by writing garbage to rollback.log.
  const auto dir = tmp_dir();
  const auto path = dir / "rollback.log";
  std::ofstream(path) << "this = = is = = not toml\n";
  EXPECT_FALSE(append_rollback_event(path, apply_event()));
  fs::remove_all(dir);
}

// ===========================================================================
// find_rollback_target
// ===========================================================================

TEST(RollbackTarget, ReturnsEmptyWhenCurrentEmpty) {
  EXPECT_EQ(find_rollback_target({apply_event()}, ""), "");
}

TEST(RollbackTarget, ReturnsApplyFromVersionForCurrent) {
  std::vector<RollbackEvent> log{apply_event()};
  EXPECT_EQ(find_rollback_target(log, "0.9.0"), "0.8.5");
}

TEST(RollbackTarget, ReturnsEmptyWhenNoApplyMatchesCurrent) {
  std::vector<RollbackEvent> log{apply_event()};
  EXPECT_EQ(find_rollback_target(log, "1.0.0"), "");
}

TEST(RollbackTarget, WalksInReverseForMostRecentApply) {
  // Two apply events ending on 0.9.0. The *more recent* one's
  // from_version is the rollback target — even if the older
  // apply event named a different from_version.
  RollbackEvent older = apply_event();  // 0.8.5 -> 0.9.0
  older.timestamp = "2026-05-10T10:00:00Z";
  older.from_version = "0.7.0";         // pretend
  RollbackEvent newer = apply_event();  // 0.8.5 -> 0.9.0 (canonical)
  newer.timestamp = "2026-05-12T10:00:00Z";

  std::vector<RollbackEvent> log{older, newer};
  EXPECT_EQ(find_rollback_target(log, "0.9.0"), "0.8.5")
      << "must take the more recent apply event's from_version";
}

TEST(RollbackTarget, IgnoresRollbackEventsAsTargets) {
  std::vector<RollbackEvent> log{apply_event(), rollback_event()};
  // After the rollback, current is 0.8.5; the most-recent apply
  // pointing *to* 0.8.5 does not exist in the log (we only see the
  // apply that put us on 0.9.0, plus the rollback). Therefore no
  // rollback target exists for 0.8.5.
  EXPECT_EQ(find_rollback_target(log, "0.8.5"), "");
}
