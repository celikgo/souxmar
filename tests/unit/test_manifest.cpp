// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/manifest.h"

#include <gtest/gtest.h>

using namespace souxmar::plugin;

namespace {

constexpr std::string_view kValidManifest = R"toml(
[plugin]
id            = "com.example.netgen-mesher"
name          = "Netgen-backed Tetra Mesher"
version       = "0.3.1"
abi           = 1
license       = "Apache-2.0"
homepage      = "https://example.com/netgen-mesher"

[plugin.binary]
file          = "libnetgen_mesher.so"

[plugin.capabilities]
provides      = ["mesher.tetra.netgen"]

[plugin.threading]
model         = "internal-parallel"

[plugin.dependencies]
souxmar       = ">=1.0,<2.0"
)toml";

TEST(Manifest, ValidParse) {
  auto result = parse_manifest(kValidManifest);
  ASSERT_TRUE(std::holds_alternative<Manifest>(result))
      << "expected success but got: " << std::get<ParseError>(result).message;
  const auto& m = std::get<Manifest>(result);
  EXPECT_EQ(m.id, "com.example.netgen-mesher");
  EXPECT_EQ(m.name, "Netgen-backed Tetra Mesher");
  EXPECT_EQ(m.version, "0.3.1");
  EXPECT_EQ(m.abi, 1);
  EXPECT_EQ(m.license, "Apache-2.0");
  EXPECT_EQ(m.homepage, "https://example.com/netgen-mesher");
  EXPECT_EQ(m.binary_file, "libnetgen_mesher.so");
  EXPECT_EQ(m.threading, ThreadingModel::InternalParallel);
  EXPECT_EQ(m.souxmar_version_constraint, ">=1.0,<2.0");
  ASSERT_EQ(m.capabilities.size(), 1u);
  EXPECT_EQ(m.capabilities[0], "mesher.tetra.netgen");
}

TEST(Manifest, MultipleCapabilities) {
  std::string toml = R"(
[plugin]
id = "com.example.elasticity-pack"
name = "Elasticity Pack"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "libelasticity.dylib"

[plugin.capabilities]
provides = ["solver.elasticity.linear", "element.solid.tet4", "postproc.von_mises"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<Manifest>(r));
  const auto& m = std::get<Manifest>(r);
  ASSERT_EQ(m.capabilities.size(), 3u);
  EXPECT_EQ(m.capabilities[0], "solver.elasticity.linear");
  EXPECT_EQ(m.capabilities[1], "element.solid.tet4");
  EXPECT_EQ(m.capabilities[2], "postproc.von_mises");
}

TEST(Manifest, MissingRequiredFieldRejected) {
  std::string toml = R"(
[plugin]
name = "no id here"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("plugin.id"), std::string::npos);
}

TEST(Manifest, EmptyCapabilitiesRejected) {
  std::string toml = R"(
[plugin]
id = "x"
name = "x"
version = "0.1.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = []
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("at least one"), std::string::npos);
}

TEST(Manifest, UnknownThreadingModelRejected) {
  std::string toml = R"(
[plugin]
id = "x"
name = "x"
version = "0.1.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]

[plugin.threading]
model = "nonsense"
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("threading"), std::string::npos);
}

TEST(Manifest, AbiVersionMustBeOneInV1xHost) {
  std::string toml = R"(
[plugin]
id = "x"
name = "x"
version = "0.1.0"
abi = 2
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_NE(std::get<ParseError>(r).message.find("ABI v1"), std::string::npos);
}

TEST(Manifest, MalformedTomlReportsLine) {
  std::string toml = "[plugin\nname = \"x\"\n";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_TRUE(std::get<ParseError>(r).line.has_value());
}

TEST(Manifest, ThreadingDefaultIsSingleThreaded) {
  std::string toml = R"(
[plugin]
id = "x"
name = "x"
version = "0.1.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.dll"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<Manifest>(r));
  EXPECT_EQ(std::get<Manifest>(r).threading, ThreadingModel::SingleThreaded);
}

TEST(Manifest, ThreadingStringRoundtrip) {
  EXPECT_EQ(to_string(ThreadingModel::Reentrant), "reentrant");
  EXPECT_EQ(to_string(ThreadingModel::SingleThreaded), "single-threaded");
  EXPECT_EQ(to_string(ThreadingModel::InternalParallel), "internal-parallel");
  EXPECT_EQ(threading_from_string("reentrant").value(), ThreadingModel::Reentrant);
  EXPECT_EQ(threading_from_string("single-threaded").value(), ThreadingModel::SingleThreaded);
  EXPECT_EQ(threading_from_string("internal-parallel").value(), ThreadingModel::InternalParallel);
  EXPECT_FALSE(threading_from_string("garbage").has_value());
}

// ============================================================================
// Sprint 6 push 2 — manifest schema validation hardening.
// ============================================================================

TEST(ManifestRejection, RejectionTokensAreStable) {
  // The audit log + tooling depend on these exact strings.
  EXPECT_EQ(to_string(ManifestRejection::Ok), "ok");
  EXPECT_EQ(to_string(ManifestRejection::TomlSyntax), "toml_syntax");
  EXPECT_EQ(to_string(ManifestRejection::MissingField), "missing_field");
  EXPECT_EQ(to_string(ManifestRejection::WrongType), "wrong_type");
  EXPECT_EQ(to_string(ManifestRejection::AbiUnsupported), "abi_unsupported");
  EXPECT_EQ(to_string(ManifestRejection::EmptyCapabilities), "empty_capabilities");
  EXPECT_EQ(to_string(ManifestRejection::UnknownThreading), "unknown_threading");
  EXPECT_EQ(to_string(ManifestRejection::InvalidCapabilityNamespace),
            "invalid_capability_namespace");
  EXPECT_EQ(to_string(ManifestRejection::InvalidPluginId), "invalid_plugin_id");
  EXPECT_EQ(to_string(ManifestRejection::InvalidVersion), "invalid_version");
  EXPECT_EQ(to_string(ManifestRejection::FileIo), "file_io");
}

TEST(ManifestRejection, CodeFlowsThroughMissingField) {
  std::string toml = R"(
[plugin]
name = "x"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  const auto& e = std::get<ParseError>(r);
  EXPECT_EQ(e.code, ManifestRejection::MissingField);
  EXPECT_EQ(e.field, "plugin.id");
}

TEST(ManifestRejection, CodeFlowsThroughWrongType) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "1.0.0"
abi = "not-an-int"
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  const auto& e = std::get<ParseError>(r);
  EXPECT_EQ(e.code, ManifestRejection::WrongType);
  EXPECT_EQ(e.field, "plugin.abi");
}

TEST(ManifestRejection, EmptyCapabilitiesHasItsOwnCode) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = []
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::EmptyCapabilities);
}

TEST(ManifestRejection, UnknownThreadingHasItsOwnCode) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]

[plugin.threading]
model = "yolo"
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::UnknownThreading);
}

TEST(ManifestRejection, AbiUnsupportedHasItsOwnCode) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "1.0.0"
abi = 7
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::AbiUnsupported);
}

TEST(ManifestRejection, InvalidCapabilityNamespaceRejected) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["garbage.foo"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::InvalidCapabilityNamespace);
}

TEST(ManifestRejection, InvalidPluginIdRejected) {
  std::string toml = R"(
[plugin]
id = "no-dot-here"
name = "x"
version = "1.0.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::InvalidPluginId);
}

TEST(ManifestRejection, InvalidVersionRejected) {
  std::string toml = R"(
[plugin]
id = "x.y"
name = "x"
version = "abc"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["mesher.x"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::InvalidVersion);
}

TEST(ManifestRejection, MalformedTomlReportsLineAndColumn) {
  // toml++ surfaces both line + column on syntax errors; we forward
  // both so audit logs / IDEs can point straight at the broken byte.
  std::string toml = "[plugin\nname = \"x\"\n";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  const auto& e = std::get<ParseError>(r);
  EXPECT_EQ(e.code, ManifestRejection::TomlSyntax);
  EXPECT_TRUE(e.line.has_value());
  EXPECT_TRUE(e.column.has_value());
}

TEST(Manifest_AdditiveFields, NewFieldsParseWhenPresent) {
  std::string toml = R"(
[plugin]
id = "dev.example.thing"
name = "Thing"
version = "0.2.0"
abi = 1
license = "MIT"
description = "Does the thing."
documentation = "https://example.dev/thing"
tags = ["mesh", "qa"]
min_souxmar_abi_minor = 2

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["postproc.scalar_magnitude"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<Manifest>(r))
      << "expected success but got: " << std::get<ParseError>(r).message;
  const auto& m = std::get<Manifest>(r);
  EXPECT_EQ(m.description, "Does the thing.");
  EXPECT_EQ(m.documentation, "https://example.dev/thing");
  ASSERT_EQ(m.tags.size(), 2u);
  EXPECT_EQ(m.tags[0], "mesh");
  EXPECT_EQ(m.tags[1], "qa");
  EXPECT_EQ(m.min_souxmar_abi_minor, 2);
}

TEST(Manifest_AdditiveFields, NewFieldsDefaultWhenAbsent) {
  // Sanity: a manifest written for the Sprint 5 surface keeps parsing.
  std::string toml = R"(
[plugin]
id = "dev.example.thing"
name = "Thing"
version = "0.2.0"
abi = 1
license = "MIT"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["postproc.scalar_magnitude"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<Manifest>(r));
  const auto& m = std::get<Manifest>(r);
  EXPECT_TRUE(m.description.empty());
  EXPECT_TRUE(m.documentation.empty());
  EXPECT_TRUE(m.tags.empty());
  EXPECT_EQ(m.min_souxmar_abi_minor, 0);
}

TEST(Manifest_AdditiveFields, TagsMustBeArrayOfStrings) {
  std::string toml = R"(
[plugin]
id = "dev.example.thing"
name = "Thing"
version = "0.2.0"
abi = 1
license = "MIT"
tags = "not-an-array"

[plugin.binary]
file = "x.so"

[plugin.capabilities]
provides = ["postproc.scalar_magnitude"]
)";
  auto r = parse_manifest(toml);
  ASSERT_TRUE(std::holds_alternative<ParseError>(r));
  EXPECT_EQ(std::get<ParseError>(r).code, ManifestRejection::WrongType);
  EXPECT_EQ(std::get<ParseError>(r).field, "plugin.tags");
}

TEST(CapabilityNamespace, AllowList) {
  EXPECT_TRUE(is_allowed_capability("reader.step"));
  EXPECT_TRUE(is_allowed_capability("writer.vtu"));
  EXPECT_TRUE(is_allowed_capability("mesher.tetra.gmsh"));
  EXPECT_TRUE(is_allowed_capability("element.solid.tet4"));
  EXPECT_TRUE(is_allowed_capability("solver.heat.linear"));
  EXPECT_TRUE(is_allowed_capability("postproc.mesh_quality"));

  EXPECT_FALSE(is_allowed_capability("garbage.foo"));
  EXPECT_FALSE(is_allowed_capability(".starts_with_dot"));
  EXPECT_FALSE(is_allowed_capability("ends_with_dot."));
  EXPECT_FALSE(is_allowed_capability("no_dot"));
  EXPECT_FALSE(is_allowed_capability(""));
}

}  // namespace
