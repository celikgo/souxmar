// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/registry.h"

#include <gtest/gtest.h>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/status.h"

using namespace souxmar::plugin;

namespace {

// Minimal valid mesher vtable for tests.
souxmar_status_t no_op_mesh(const souxmar_geometry_t*,
                            const souxmar_mesher_options_t*,
                            souxmar_mesh_t**,
                            void*) {
  return souxmar_status_ok();
}

constexpr souxmar_mesher_vtable_t kValidVtable{SOUXMAR_ABI_VERSION_MAJOR, &no_op_mesh, nullptr};

TEST(Registry, EmptyOnConstruction) {
  Registry r;
  EXPECT_EQ(r.size(), 0u);
  EXPECT_TRUE(r.list_capabilities().empty());
}

TEST(Registry, AddMesherSucceeds) {
  Registry r;
  auto rc = r.add_mesher("mesher.tetra.x", "com.example.x", &kValidVtable, nullptr);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(rc));
  EXPECT_EQ(r.size(), 1u);
  EXPECT_EQ(r.list_capabilities()[0], "mesher.tetra.x");
}

TEST(Registry, DuplicateCapabilityRejected) {
  Registry r;
  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      r.add_mesher("mesher.tetra.x", "com.a", &kValidVtable, nullptr)));
  auto rc = r.add_mesher("mesher.tetra.x", "com.b", &kValidVtable, nullptr);
  ASSERT_TRUE(std::holds_alternative<RegistryError>(rc));
  EXPECT_NE(std::get<RegistryError>(rc).message.find("already registered"),
            std::string::npos);
}

TEST(Registry, EmptyCapabilityIdRejected) {
  Registry r;
  auto rc = r.add_mesher("", "com.a", &kValidVtable, nullptr);
  ASSERT_TRUE(std::holds_alternative<RegistryError>(rc));
}

TEST(Registry, NullVtableRejected) {
  Registry r;
  auto rc = r.add_mesher("mesher.tetra.x", "com.a", nullptr, nullptr);
  ASSERT_TRUE(std::holds_alternative<RegistryError>(rc));
}

TEST(Registry, AbiMismatchRejected) {
  constexpr souxmar_mesher_vtable_t bad{SOUXMAR_ABI_VERSION_MAJOR + 99, &no_op_mesh, nullptr};
  Registry r;
  auto rc = r.add_mesher("mesher.tetra.x", "com.a", &bad, nullptr);
  ASSERT_TRUE(std::holds_alternative<RegistryError>(rc));
  EXPECT_NE(std::get<RegistryError>(rc).message.find("abi_version"),
            std::string::npos);
}

TEST(Registry, NullMeshFnRejected) {
  constexpr souxmar_mesher_vtable_t bad{SOUXMAR_ABI_VERSION_MAJOR, nullptr, nullptr};
  Registry r;
  auto rc = r.add_mesher("mesher.tetra.x", "com.a", &bad, nullptr);
  ASSERT_TRUE(std::holds_alternative<RegistryError>(rc));
  EXPECT_NE(std::get<RegistryError>(rc).message.find("mesh_fn"),
            std::string::npos);
}

TEST(Registry, ListByNamespace) {
  Registry r;
  r.add_mesher("mesher.tetra.a", "p", &kValidVtable, nullptr);
  r.add_mesher("mesher.tetra.b", "p", &kValidVtable, nullptr);
  r.add_mesher("mesher.shell.c", "p", &kValidVtable, nullptr);

  const auto tetras = r.list_capabilities_in_namespace("mesher.tetra");
  ASSERT_EQ(tetras.size(), 2u);
  EXPECT_EQ(tetras[0], "mesher.tetra.a");
  EXPECT_EQ(tetras[1], "mesher.tetra.b");

  const auto shells = r.list_capabilities_in_namespace("mesher.shell");
  ASSERT_EQ(shells.size(), 1u);
  EXPECT_EQ(shells[0], "mesher.shell.c");
}

TEST(Registry, FindReturnsEntry) {
  Registry r;
  r.add_mesher("mesher.tetra.x", "com.example", &kValidVtable, nullptr);
  const auto* entry = r.find("mesher.tetra.x");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->id,        "mesher.tetra.x");
  EXPECT_EQ(entry->plugin_id, "com.example");
  EXPECT_EQ(entry->kind,      CapabilityKind::Mesher);

  const auto* mesher = r.find_mesher("mesher.tetra.x");
  ASSERT_NE(mesher, nullptr);
  EXPECT_EQ(mesher->vtable, &kValidVtable);
}

TEST(Registry, FindReturnsNullForMissing) {
  Registry r;
  EXPECT_EQ(r.find("nope"), nullptr);
  EXPECT_EQ(r.find_mesher("nope"), nullptr);
}

TEST(Registry, RemovePluginDropsAllOwnedCapabilities) {
  Registry r;
  r.add_mesher("mesher.tetra.a", "plugin.a", &kValidVtable, nullptr);
  r.add_mesher("mesher.tetra.b", "plugin.a", &kValidVtable, nullptr);
  r.add_mesher("mesher.tetra.c", "plugin.b", &kValidVtable, nullptr);
  EXPECT_EQ(r.size(), 3u);

  r.remove_plugin("plugin.a");
  EXPECT_EQ(r.size(), 1u);
  EXPECT_EQ(r.list_capabilities()[0], "mesher.tetra.c");
}

}  // namespace
