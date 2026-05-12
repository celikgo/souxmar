// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge auto_updater_menu surface smoke test.
// Sprint 15 push 4.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "souxmar-c-bridge/pipeline.h"
#include "souxmar-c-bridge/updater.h"

namespace fs = std::filesystem;

namespace {

fs::path scratch_root(std::string_view tag) {
  std::random_device rd;
  fs::path dir = fs::temp_directory_path() /
                 ("souxmar-bridge-update-" + std::string(tag) + "-" +
                  std::to_string(rd()));
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST(CBridgeUpdater, AbiVersionBumpedToV3) {
  EXPECT_EQ(souxmar_bridge_abi_version(), 3u);
}

TEST(CBridgeUpdater, NonexistentTargetRootReturnsUnknown) {
  char* err = nullptr;
  auto* s = souxmar_bridge_update_status_read("/no/such/path/souxmar-test", &err);
  ASSERT_NE(s, nullptr) << "expected an Unknown status, not NULL";
  EXPECT_EQ(err, nullptr);
  EXPECT_EQ(souxmar_bridge_update_state(s), SOUXMAR_BRIDGE_US_UNKNOWN);
  EXPECT_STREQ(souxmar_bridge_update_current_version(s),   "");
  EXPECT_STREQ(souxmar_bridge_update_available_version(s), "");
  EXPECT_NE(std::strlen(souxmar_bridge_update_detail(s)), 0u);
  souxmar_bridge_update_status_free(s);
}

TEST(CBridgeUpdater, NullTargetRootIsRejectedNotCrash) {
  char* err = nullptr;
  auto* s = souxmar_bridge_update_status_read(nullptr, &err);
  EXPECT_EQ(s, nullptr);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("target_root"), std::string::npos);
  souxmar_bridge_free_string(err);
}

TEST(CBridgeUpdater, EmptyTargetRootIsRejected) {
  char* err = nullptr;
  auto* s = souxmar_bridge_update_status_read("", &err);
  EXPECT_EQ(s, nullptr);
  ASSERT_NE(err, nullptr);
  souxmar_bridge_free_string(err);
}

TEST(CBridgeUpdater, TargetRootWithCurrentTxtReportsUpToDate) {
  auto root = scratch_root("happy");
  // Layout: current.txt = "v0.9.2".
  {
    std::ofstream f(root / "current.txt");
    f << "v0.9.2";
  }
  char* err = nullptr;
  auto* s = souxmar_bridge_update_status_read(root.string().c_str(), &err);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(err, nullptr);
  EXPECT_EQ(souxmar_bridge_update_state(s), SOUXMAR_BRIDGE_US_UP_TO_DATE);
  EXPECT_STREQ(souxmar_bridge_update_current_version(s), "v0.9.2");
  EXPECT_NE(std::strlen(souxmar_bridge_update_detail(s)), 0u);
  souxmar_bridge_update_status_free(s);
  fs::remove_all(root);
}

TEST(CBridgeUpdater, NullHandleAccessorsReturnSafeDefaults) {
  EXPECT_EQ(souxmar_bridge_update_state(nullptr), SOUXMAR_BRIDGE_US_UNKNOWN);
  EXPECT_STREQ(souxmar_bridge_update_current_version(nullptr),   "");
  EXPECT_STREQ(souxmar_bridge_update_available_version(nullptr), "");
  EXPECT_STREQ(souxmar_bridge_update_detail(nullptr),            "");
}

TEST(CBridgeUpdater, FreeNullIsSafe) {
  souxmar_bridge_update_status_free(nullptr);
}
