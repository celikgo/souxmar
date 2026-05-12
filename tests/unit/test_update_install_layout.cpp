// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 7 — unit tests for InstallLayout. Coverage:
//   * Marker files (current.txt / previous.txt) round-trip cleanly,
//     missing files map to empty strings, whitespace-tolerant.
//   * stage_version writes versions/<v>/payload atomically; a partial
//     write doesn't pollute versions/.
//   * atomic_switch_to refuses a missing payload, shuffles
//     previous.txt correctly, and is observable as "current.txt
//     contains exactly the new version name".
//   * remove_version + gc_unreferenced respect the current/previous
//     protection rails.
//   * sha256_hex matches libsodium's reference value.

#include "souxmar/update/install_layout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace souxmar::update;
namespace fs = std::filesystem;

namespace {

fs::path tmp_root(std::string_view tag) {
  std::random_device rd;
  auto base = fs::temp_directory_path()
              / ("souxmar-install-layout-" + std::string(tag) + "-" + std::to_string(rd()));
  fs::create_directories(base);
  return base;
}

std::vector<std::uint8_t> bytes_of(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data() + s.size())};
}

std::string slurp(const fs::path& p) {
  std::ifstream src(p, std::ios::binary);
  std::ostringstream buf;
  buf << src.rdbuf();
  return buf.str();
}

}  // namespace

TEST(InstallLayout, FreshRootHasNothing) {
  const auto root = tmp_root("fresh");
  InstallLayout l(root);
  EXPECT_TRUE(l.read_current_version().empty());
  EXPECT_TRUE(l.read_previous_version().empty());
  EXPECT_FALSE(l.has_current());
  EXPECT_FALSE(l.has_previous());
  EXPECT_TRUE(l.list_versions().empty());
  fs::remove_all(root);
}

TEST(InstallLayout, StageVersionMaterialisesPayload) {
  const auto root = tmp_root("stage");
  InstallLayout l(root);
  const auto bytes = bytes_of("hello souxmar 0.9.0 payload");
  ASSERT_TRUE(l.stage_version("0.9.0", bytes));
  EXPECT_TRUE(l.has_version_payload("0.9.0"));

  const auto written = slurp(root / "versions" / "0.9.0" / "payload");
  EXPECT_EQ(written, "hello souxmar 0.9.0 payload");

  // staging/ should be empty after the rename succeeds.
  std::error_code ec;
  if (fs::exists(root / "staging", ec)) {
    int count = 0;
    for (const auto& _ : fs::directory_iterator(root / "staging", ec)) {
      (void)_;
      ++count;
    }
    EXPECT_EQ(count, 0) << "staging/ should be empty after a successful stage";
  }
  fs::remove_all(root);
}

TEST(InstallLayout, StageVersionRejectsEmptyName) {
  const auto root = tmp_root("empty-name");
  InstallLayout l(root);
  EXPECT_FALSE(l.stage_version("", bytes_of("x")));
  fs::remove_all(root);
}

TEST(InstallLayout, AtomicSwitchToRefusesMissingPayload) {
  const auto root = tmp_root("missing");
  InstallLayout l(root);
  EXPECT_FALSE(l.atomic_switch_to("", "0.9.0"));
  EXPECT_TRUE(l.read_current_version().empty());
  fs::remove_all(root);
}

TEST(InstallLayout, AtomicSwitchToFlipsMarkerAndShufflesPrevious) {
  const auto root = tmp_root("flip");
  InstallLayout l(root);
  ASSERT_TRUE(l.stage_version("0.8.5", bytes_of("v0.8.5")));
  ASSERT_TRUE(l.stage_version("0.9.0", bytes_of("v0.9.0")));

  // First-install path — no from_version, no previous to record.
  ASSERT_TRUE(l.atomic_switch_to("", "0.8.5"));
  EXPECT_EQ(l.read_current_version(), "0.8.5");
  EXPECT_TRUE(l.read_previous_version().empty());

  // Upgrade — previous gets the outgoing version.
  ASSERT_TRUE(l.atomic_switch_to("0.8.5", "0.9.0"));
  EXPECT_EQ(l.read_current_version(), "0.9.0");
  EXPECT_EQ(l.read_previous_version(), "0.8.5");
  fs::remove_all(root);
}

TEST(InstallLayout, ListVersionsEnumeratesEverything) {
  const auto root = tmp_root("list");
  InstallLayout l(root);
  ASSERT_TRUE(l.stage_version("0.8.5", bytes_of("a")));
  ASSERT_TRUE(l.stage_version("0.9.0", bytes_of("b")));
  ASSERT_TRUE(l.stage_version("1.0.0", bytes_of("c")));
  auto vs = l.list_versions();
  std::sort(vs.begin(), vs.end());
  ASSERT_EQ(vs.size(), 3u);
  EXPECT_EQ(vs[0], "0.8.5");
  EXPECT_EQ(vs[1], "0.9.0");
  EXPECT_EQ(vs[2], "1.0.0");
  fs::remove_all(root);
}

TEST(InstallLayout, RemoveVersionRefusesCurrentAndPrevious) {
  const auto root = tmp_root("remove-protect");
  InstallLayout l(root);
  ASSERT_TRUE(l.stage_version("0.8.5", bytes_of("a")));
  ASSERT_TRUE(l.stage_version("0.9.0", bytes_of("b")));
  ASSERT_TRUE(l.atomic_switch_to("0.8.5", "0.9.0"));

  EXPECT_FALSE(l.remove_version("0.9.0")) << "current must not be removed";
  EXPECT_FALSE(l.remove_version("0.8.5")) << "previous must not be removed";
  EXPECT_TRUE(l.has_version_payload("0.8.5"));
  EXPECT_TRUE(l.has_version_payload("0.9.0"));

  // A non-current/previous version *can* be removed.
  ASSERT_TRUE(l.stage_version("0.7.0", bytes_of("c")));
  EXPECT_TRUE(l.remove_version("0.7.0"));
  EXPECT_FALSE(l.has_version_payload("0.7.0"));
  fs::remove_all(root);
}

TEST(InstallLayout, GcUnreferencedReapsOnlyStaleVersions) {
  const auto root = tmp_root("gc");
  InstallLayout l(root);
  ASSERT_TRUE(l.stage_version("0.7.0", bytes_of("a")));
  ASSERT_TRUE(l.stage_version("0.8.5", bytes_of("b")));
  ASSERT_TRUE(l.stage_version("0.9.0", bytes_of("c")));
  ASSERT_TRUE(l.atomic_switch_to("0.8.5", "0.9.0"));
  // current = 0.9.0, previous = 0.8.5, 0.7.0 is stale.

  auto reaped = l.gc_unreferenced();
  ASSERT_EQ(reaped.size(), 1u);
  EXPECT_EQ(reaped[0], "0.7.0");
  EXPECT_FALSE(l.has_version_payload("0.7.0"));
  EXPECT_TRUE(l.has_version_payload("0.8.5"));
  EXPECT_TRUE(l.has_version_payload("0.9.0"));
  fs::remove_all(root);
}

TEST(InstallLayout, GcRespectsProtectList) {
  // A caller can opt to preserve additional versions (e.g., a build
  // currently being staged by another process).
  const auto root = tmp_root("gc-protect");
  InstallLayout l(root);
  ASSERT_TRUE(l.stage_version("0.7.0", bytes_of("a")));
  ASSERT_TRUE(l.stage_version("0.9.0", bytes_of("b")));
  ASSERT_TRUE(l.atomic_switch_to("", "0.9.0"));

  const std::vector<std::string> protect{"0.7.0"};
  auto reaped = l.gc_unreferenced(protect);
  EXPECT_TRUE(reaped.empty());
  EXPECT_TRUE(l.has_version_payload("0.7.0"));
  fs::remove_all(root);
}

TEST(InstallLayout, MarkerFilesTolerateTrailingWhitespace) {
  const auto root = tmp_root("ws");
  fs::create_directories(root);
  std::ofstream(root / "current.txt") << "0.9.0\r\n\n";
  InstallLayout l(root);
  EXPECT_EQ(l.read_current_version(), "0.9.0");
  fs::remove_all(root);
}

// ============================================================================
// sha256_hex
// ============================================================================

TEST(Sha256Hex, KnownVector) {
  // Empty string => the standard NIST sha256 digest of "".
  const std::array<std::uint8_t, 0> empty{};
  EXPECT_EQ(sha256_hex({empty.data(), 0u}),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Hex, AbcVector) {
  const auto b = bytes_of("abc");
  EXPECT_EQ(sha256_hex(b), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
