// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/discovery.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace souxmar::plugin;
namespace fs = std::filesystem;

namespace {

// Holds a temporary directory that is removed on destruction. Keeps tests
// from polluting the developer's plugin search paths.
class TempDir {
 public:
  TempDir() {
    std::random_device rd;
    auto name = "souxmar-discovery-test-" + std::to_string(rd());
    path_ = fs::temp_directory_path() / name;
    fs::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void write_file(const fs::path& p, std::string_view content) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << content;
}

void touch(const fs::path& p) { write_file(p, ""); }

constexpr std::string_view kValidManifest = R"toml(
[plugin]
id            = "com.example.test-mesher"
name          = "Test Mesher"
version       = "0.1.0"
abi           = 1
license       = "Apache-2.0"

[plugin.binary]
file          = "libtest_mesher.so"

[plugin.capabilities]
provides      = ["mesher.tetra.test"]
)toml";

TEST(Discovery, EmptyPathYieldsEmptyReport) {
  auto report = discover_plugins(std::vector<fs::path>{});
  EXPECT_TRUE(report.loaded.empty());
  EXPECT_TRUE(report.rejected.empty());
}

TEST(Discovery, NonExistentSearchPathSilentlySkipped) {
  TempDir td;
  auto missing = td.path() / "does-not-exist";
  auto report = discover_plugins({missing});
  EXPECT_TRUE(report.loaded.empty());
  EXPECT_TRUE(report.rejected.empty());
}

TEST(Discovery, FindsValidPlugin) {
  TempDir td;
  auto plugin_dir = td.path() / "test-plugin";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  touch(plugin_dir / "libtest_mesher.so");

  auto report = discover_plugins({td.path()});
  ASSERT_EQ(report.loaded.size(), 1u);
  EXPECT_TRUE(report.rejected.empty());
  EXPECT_EQ(report.loaded[0].manifest.id, "com.example.test-mesher");
  EXPECT_EQ(report.loaded[0].binary_path, plugin_dir / "libtest_mesher.so");
}

TEST(Discovery, MissingBinaryRejected) {
  TempDir td;
  auto plugin_dir = td.path() / "no-binary";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  // Note: no libtest_mesher.so on disk.

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  ASSERT_EQ(report.rejected.size(), 1u);
  EXPECT_NE(report.rejected[0].reason.find("does not exist"), std::string::npos);
}

TEST(Discovery, InvalidExtensionRejected) {
  TempDir td;
  auto plugin_dir = td.path() / "wrong-ext";
  fs::create_directories(plugin_dir);
  std::string manifest = R"toml(
[plugin]
id = "x"
name = "x"
version = "0.1.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "wrongext.bin"

[plugin.capabilities]
provides = ["mesher.x"]
)toml";
  write_file(plugin_dir / "souxmar-plugin.toml", manifest);
  touch(plugin_dir / "wrongext.bin");

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  ASSERT_EQ(report.rejected.size(), 1u);
  EXPECT_NE(report.rejected[0].reason.find("extension"), std::string::npos);
}

TEST(Discovery, MalformedManifestRejectedWithReason) {
  TempDir td;
  auto plugin_dir = td.path() / "bad-toml";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", "[plugin\nname=");

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  ASSERT_EQ(report.rejected.size(), 1u);
  EXPECT_FALSE(report.rejected[0].reason.empty());
}

TEST(Discovery, DirectoryWithoutManifestSilentlyIgnored) {
  TempDir td;
  fs::create_directories(td.path() / "empty-dir");

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  EXPECT_TRUE(report.rejected.empty());
}

TEST(Discovery, MultipleSearchRootsAggregated) {
  TempDir td1, td2;
  for (auto* td : {&td1, &td2}) {
    auto p = td->path() / "p";
    fs::create_directories(p);
    write_file(p / "souxmar-plugin.toml", kValidManifest);
    touch(p / "libtest_mesher.so");
  }
  auto report = discover_plugins({td1.path(), td2.path()});
  EXPECT_EQ(report.loaded.size(), 2u);
}

TEST(DefaultSearchPaths, AllSlotsOff) {
  DiscoveryOptions opts;
  opts.include_env_path       = false;
  opts.include_install_prefix = false;
  opts.include_user_prefix    = false;
  opts.include_cwd            = false;
  EXPECT_TRUE(default_search_paths(opts).empty());
}

}  // namespace
