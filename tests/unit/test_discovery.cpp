// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/discovery.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

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

  const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

void write_file(const fs::path& p, std::string_view content) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << content;
}

void touch(const fs::path& p) {
  write_file(p, "");
}

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

// The host platform's own shared-library extension, and the extension the
// fallback is expected to settle on when only the two non-native ones are
// present (kBinaryExtensions in discovery.cpp puts the host's first, then
// .so / .dylib / .dll in that order).
#if defined(_WIN32)
constexpr std::string_view kHostExtension = ".dll";
constexpr std::string_view kPreferredOfDylibAndDll = ".dll";
#elif defined(__APPLE__)
constexpr std::string_view kHostExtension = ".dylib";
constexpr std::string_view kPreferredOfDylibAndDll = ".dylib";
#else
constexpr std::string_view kHostExtension = ".so";
// .so is host-native but absent in that test, so the ordered scan lands on
// .dylib (kBinaryExtensions == {.so, .dylib, .dll} on ELF platforms).
constexpr std::string_view kPreferredOfDylibAndDll = ".dylib";
#endif

// Every in-tree manifest declares the ELF name `lib<target>.so` because the
// manifest ships once for all platforms, but CMake emits .dylib on macOS and
// .dll on Windows. Discovery must retry the stem with the host extension
// instead of rejecting the plugin as binary_not_found.
TEST(Discovery, DeclaredSoResolvesToHostExtension) {
  TempDir td;
  auto plugin_dir = td.path() / "host-ext";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  // Manifest says libtest_mesher.so; only the host artefact exists.
  touch(plugin_dir / (std::string("libtest_mesher") + std::string(kHostExtension)));

  auto report = discover_plugins({td.path()});
  ASSERT_EQ(report.loaded.size(), 1u) << (report.rejected.empty() ? "" : report.rejected[0].reason);
  EXPECT_TRUE(report.rejected.empty());
  EXPECT_EQ(report.loaded[0].binary_path.extension().string(), std::string(kHostExtension));
  // The manifest itself is not rewritten — only the resolved path changes.
  EXPECT_EQ(report.loaded[0].manifest.binary_file, "libtest_mesher.so");
}

// The declared name stays authoritative: when it is on disk it is used even
// though a host-native sibling exists next to it.
TEST(Discovery, DeclaredBinaryWinsOverHostExtensionSibling) {
  TempDir td;
  auto plugin_dir = td.path() / "declared-wins";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  touch(plugin_dir / "libtest_mesher.so");
  touch(plugin_dir / (std::string("libtest_mesher") + std::string(kHostExtension)));

  auto report = discover_plugins({td.path()});
  ASSERT_EQ(report.loaded.size(), 1u);
  EXPECT_EQ(report.loaded[0].binary_path, plugin_dir / "libtest_mesher.so");
}

// Robustness leg: neither the declared name nor the host extension is
// present, but another canonical extension is. Resolution is by the fixed
// kBinaryExtensions order, never by directory-iteration order, so the pick
// is identical on every machine.
TEST(Discovery, FallsBackToAnyCanonicalExtensionInFixedOrder) {
  TempDir td;
  auto plugin_dir = td.path() / "cross-ext";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  touch(plugin_dir / "libtest_mesher.dylib");
  touch(plugin_dir / "libtest_mesher.dll");

  auto report = discover_plugins({td.path()});
  ASSERT_EQ(report.loaded.size(), 1u) << (report.rejected.empty() ? "" : report.rejected[0].reason);
  EXPECT_EQ(report.loaded[0].binary_path.extension().string(),
            std::string(kPreferredOfDylibAndDll));
}

// A sibling with a non-canonical extension is not a fallback candidate; the
// rejection still names the declared path.
TEST(Discovery, NonCanonicalSiblingIsNotAFallbackCandidate) {
  TempDir td;
  auto plugin_dir = td.path() / "bogus-sibling";
  fs::create_directories(plugin_dir);
  write_file(plugin_dir / "souxmar-plugin.toml", kValidManifest);
  touch(plugin_dir / "libtest_mesher.bin");

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  ASSERT_EQ(report.rejected.size(), 1u);
  EXPECT_EQ(report.rejected[0].code, DiscoveryRejectionCode::BinaryNotFound);
  EXPECT_NE(report.rejected[0].reason.find("libtest_mesher.so"), std::string::npos);
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
  EXPECT_EQ(report.rejected[0].code, DiscoveryRejectionCode::BinaryNotFound);
}

TEST(Discovery, InvalidExtensionRejected) {
  TempDir td;
  auto plugin_dir = td.path() / "wrong-ext";
  fs::create_directories(plugin_dir);
  std::string manifest = R"toml(
[plugin]
id = "dev.example.x"
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
  EXPECT_EQ(report.rejected[0].code, DiscoveryRejectionCode::BinaryUnrecognisedExtension);
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
  EXPECT_EQ(report.rejected[0].code, DiscoveryRejectionCode::ManifestParseFailed);
  ASSERT_TRUE(report.rejected[0].manifest_code.has_value());
  EXPECT_EQ(*report.rejected[0].manifest_code, ManifestRejection::TomlSyntax);
}

TEST(Discovery, BadCapabilityNamespaceManifestRejectedWithStructuredCode) {
  TempDir td;
  auto plugin_dir = td.path() / "bad-namespace";
  fs::create_directories(plugin_dir);
  std::string manifest = R"toml(
[plugin]
id = "dev.example.x"
name = "x"
version = "0.1.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["garbage.foo"]
)toml";
  write_file(plugin_dir / "souxmar-plugin.toml", manifest);
  touch(plugin_dir / "x.so");

  auto report = discover_plugins({td.path()});
  EXPECT_TRUE(report.loaded.empty());
  ASSERT_EQ(report.rejected.size(), 1u);
  EXPECT_EQ(report.rejected[0].code, DiscoveryRejectionCode::ManifestParseFailed);
  ASSERT_TRUE(report.rejected[0].manifest_code.has_value());
  EXPECT_EQ(*report.rejected[0].manifest_code, ManifestRejection::InvalidCapabilityNamespace);
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
  opts.include_env_path = false;
  opts.include_install_prefix = false;
  opts.include_user_prefix = false;
  opts.include_cwd = false;
  EXPECT_TRUE(default_search_paths(opts).empty());
}

}  // namespace
