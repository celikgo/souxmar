// SPDX-License-Identifier: Apache-2.0
//
// End-to-end smoke test for the plugin pipeline:
//   1. Discover the in-tree built hello-mesher plugin via SOUXMAR_PLUGIN_PATH
//      pointing at its build-output directory.
//   2. Load it through PluginLoader (dlopen + dlsym + register).
//   3. Verify its capability appears in the Registry.
//   4. Drop the LoadedPlugin and verify the registry empties.
//
// This is the canary that proves the entire Sprint 2 critical path
// (manifest -> discovery -> loader -> registry) is wired together.

#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <variant>

namespace fs = std::filesystem;
using namespace souxmar::plugin;

namespace {

TEST(LoadHelloMesher, DiscoveryFindsTheBuiltPlugin) {
  // SOUXMAR_HELLO_MESHER_DIR is the directory CMake placed the built
  // libhello_mesher.{so,dylib,dll} + souxmar-plugin.toml. Discovery walks
  // the *parent* and inspects each subdirectory, so we point at the parent.
  const fs::path plugin_dir = SOUXMAR_TEST_HELLO_MESHER_DIR;
  ASSERT_TRUE(fs::exists(plugin_dir / "souxmar-plugin.toml"))
      << "expected manifest beside the built binary at " << plugin_dir;

  const auto report = discover_plugins({plugin_dir.parent_path()});
  ASSERT_FALSE(report.loaded.empty())
      << "discovery rejected: "
      << (report.rejected.empty() ? std::string{"<no rejections>"} : report.rejected[0].reason);
  // The built plugin's directory is the only one we expect to find.
  bool found = false;
  for (const auto& p : report.loaded) {
    if (p.manifest.id == "dev.souxmar.examples.hello-mesher")
      found = true;
  }
  EXPECT_TRUE(found);
}

TEST(LoadHelloMesher, LoadAndRegisterRoundtrip) {
  const fs::path plugin_dir = SOUXMAR_TEST_HELLO_MESHER_DIR;
  const auto report = discover_plugins({plugin_dir.parent_path()});
  ASSERT_FALSE(report.loaded.empty());

  const DiscoveredPlugin* hello = nullptr;
  for (const auto& p : report.loaded) {
    if (p.manifest.id == "dev.souxmar.examples.hello-mesher")
      hello = &p;
  }
  ASSERT_NE(hello, nullptr);

  Registry registry;
  PluginLoader loader(registry, "test-host/0.0.0");

  auto result = loader.load(*hello);
  ASSERT_TRUE(std::holds_alternative<LoadedPlugin>(result))
      << "load failed: " << std::get<LoadError>(result).message;

  // The loaded plugin must have registered exactly the capability it advertises.
  EXPECT_EQ(registry.size(), 1u);
  const auto* mesher = registry.find_mesher("mesher.tetra.hello");
  EXPECT_NE(mesher, nullptr);

  // Sanity: list_capabilities reports the full id; namespace lookup works.
  const auto all = registry.list_capabilities();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0], "mesher.tetra.hello");
  const auto tetras = registry.list_capabilities_in_namespace("mesher.tetra");
  ASSERT_EQ(tetras.size(), 1u);
  EXPECT_EQ(tetras[0], "mesher.tetra.hello");
}

TEST(LoadHelloMesher, DroppingLoadedPluginEmptiesRegistry) {
  const fs::path plugin_dir = SOUXMAR_TEST_HELLO_MESHER_DIR;
  const auto report = discover_plugins({plugin_dir.parent_path()});
  ASSERT_FALSE(report.loaded.empty());
  const DiscoveredPlugin* hello = nullptr;
  for (const auto& p : report.loaded) {
    if (p.manifest.id == "dev.souxmar.examples.hello-mesher")
      hello = &p;
  }
  ASSERT_NE(hello, nullptr);

  Registry registry;
  PluginLoader loader(registry, "test-host/0.0.0");

  {
    auto result = loader.load(*hello);
    ASSERT_TRUE(std::holds_alternative<LoadedPlugin>(result));
    EXPECT_EQ(registry.size(), 1u);
  }
  // LoadedPlugin out of scope here: dlclose has been called and capabilities
  // owned by this plugin id are removed from the registry.
  EXPECT_EQ(registry.size(), 0u);
}

}  // namespace
