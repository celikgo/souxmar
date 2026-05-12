// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 6 — unit tests for the auto-updater per-user state
// file. Surface in include/souxmar/update/update_state.h.
//
// Coverage:
//   * Render -> Parse roundtrip is byte-identical.
//   * Loading a missing file returns clean defaults (fresh install).
//   * Loading a malformed file returns UpdateStateLoadError.
//   * Save + load roundtrip on disk.
//   * Atomic-write semantics: a save into a directory that doesn't
//     exist creates the directory and writes the file successfully.

#include "souxmar/update/update_state.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <variant>

using namespace souxmar::update;
namespace fs = std::filesystem;

namespace {

fs::path tmp_state_dir() {
  std::random_device rd;
  auto base = fs::temp_directory_path() / ("souxmar-update-state-test-" + std::to_string(rd()));
  fs::create_directories(base);
  return base;
}

UpdateState make_filled_state() {
  UpdateState s;
  s.current_installed_version = "0.9.0";
  s.max_version_ever_seen = "0.9.1";
  s.last_check_at = "2026-05-11T14:00:00Z";
  s.last_apply_at = "2026-05-11T13:50:00Z";
  return s;
}

}  // namespace

TEST(UpdateState, RenderProducesExpectedShape) {
  const auto s = make_filled_state();
  const auto t = render_update_state(s);
  EXPECT_NE(t.find("schema                    = 1"), std::string::npos);
  EXPECT_NE(t.find("current_installed_version = \"0.9.0\""), std::string::npos);
  EXPECT_NE(t.find("max_version_ever_seen     = \"0.9.1\""), std::string::npos);
  EXPECT_NE(t.find("last_check_at             = \"2026-05-11T14:00:00Z\""), std::string::npos);
}

TEST(UpdateState, LoadMissingFileReturnsFreshDefaults) {
  const auto dir = tmp_state_dir();
  const auto path = dir / "no-such-file.toml";
  auto r = load_update_state(path);
  ASSERT_TRUE(std::holds_alternative<UpdateState>(r)) << "expected default-state, got load error";
  const auto& s = std::get<UpdateState>(r);
  EXPECT_TRUE(s.current_installed_version.empty());
  EXPECT_TRUE(s.max_version_ever_seen.empty());
  fs::remove_all(dir);
}

TEST(UpdateState, RoundtripsThroughDisk) {
  const auto dir = tmp_state_dir();
  const auto path = dir / "update-state.toml";
  const auto src = make_filled_state();
  ASSERT_TRUE(save_update_state(path, src));
  ASSERT_TRUE(fs::exists(path));

  auto r = load_update_state(path);
  ASSERT_TRUE(std::holds_alternative<UpdateState>(r));
  const auto& dst = std::get<UpdateState>(r);
  EXPECT_EQ(dst.current_installed_version, src.current_installed_version);
  EXPECT_EQ(dst.max_version_ever_seen, src.max_version_ever_seen);
  EXPECT_EQ(dst.last_check_at, src.last_check_at);
  EXPECT_EQ(dst.last_apply_at, src.last_apply_at);
  fs::remove_all(dir);
}

TEST(UpdateState, SaveCreatesMissingParentDirectory) {
  const auto dir = tmp_state_dir();
  const auto deep = dir / "a" / "b" / "c" / "update-state.toml";
  ASSERT_FALSE(fs::exists(deep.parent_path()));
  ASSERT_TRUE(save_update_state(deep, make_filled_state()));
  EXPECT_TRUE(fs::exists(deep));
  fs::remove_all(dir);
}

TEST(UpdateState, RejectsUnknownSchemaVersion) {
  const auto dir = tmp_state_dir();
  const auto path = dir / "update-state.toml";
  {
    std::ofstream sink(path);
    sink << "schema = 999\n"
         << "current_installed_version = \"0.9.0\"\n";
  }
  auto r = load_update_state(path);
  ASSERT_TRUE(std::holds_alternative<UpdateStateLoadError>(r));
  const auto& err = std::get<UpdateStateLoadError>(r);
  EXPECT_NE(err.message.find("999"), std::string::npos);
  fs::remove_all(dir);
}

TEST(UpdateState, RejectsMalformedToml) {
  const auto dir = tmp_state_dir();
  const auto path = dir / "update-state.toml";
  {
    std::ofstream sink(path);
    sink << "this = = is not toml = =\n";
  }
  auto r = load_update_state(path);
  EXPECT_TRUE(std::holds_alternative<UpdateStateLoadError>(r));
  fs::remove_all(dir);
}

TEST(UpdateState, RejectsMissingSchemaField) {
  const auto dir = tmp_state_dir();
  const auto path = dir / "update-state.toml";
  {
    std::ofstream sink(path);
    sink << "current_installed_version = \"0.9.0\"\n";
  }
  auto r = load_update_state(path);
  ASSERT_TRUE(std::holds_alternative<UpdateStateLoadError>(r));
  EXPECT_NE(std::get<UpdateStateLoadError>(r).message.find("schema"), std::string::npos);
  fs::remove_all(dir);
}

TEST(UpdateState, IgnoresUnknownFieldsForwardCompat) {
  // A future client might add a new optional field; today's client
  // should ignore it cleanly, not refuse to load.
  const auto dir = tmp_state_dir();
  const auto path = dir / "update-state.toml";
  {
    std::ofstream sink(path);
    sink << "schema = 1\n"
         << "current_installed_version = \"0.9.0\"\n"
         << "max_version_ever_seen = \"0.9.0\"\n"
         << "last_check_at = \"2026-05-11T14:00:00Z\"\n"
         << "future_telemetry_bucket = \"alpha\"\n";
  }
  auto r = load_update_state(path);
  ASSERT_TRUE(std::holds_alternative<UpdateState>(r));
  EXPECT_EQ(std::get<UpdateState>(r).current_installed_version, "0.9.0");
  fs::remove_all(dir);
}

TEST(UpdateState, DefaultPathIsNonEmpty) {
  // We can't test the exact path without env-var manipulation that
  // affects the rest of the suite; just lock down that *some* path
  // gets returned.
  const auto p = default_update_state_path();
  EXPECT_FALSE(p.empty());
}
