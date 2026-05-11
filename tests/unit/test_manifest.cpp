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
      << "expected success but got: "
      << std::get<ParseError>(result).message;
  const auto& m = std::get<Manifest>(result);
  EXPECT_EQ(m.id,                       "com.example.netgen-mesher");
  EXPECT_EQ(m.name,                     "Netgen-backed Tetra Mesher");
  EXPECT_EQ(m.version,                  "0.3.1");
  EXPECT_EQ(m.abi,                      1);
  EXPECT_EQ(m.license,                  "Apache-2.0");
  EXPECT_EQ(m.homepage,                 "https://example.com/netgen-mesher");
  EXPECT_EQ(m.binary_file,              "libnetgen_mesher.so");
  EXPECT_EQ(m.threading,                ThreadingModel::InternalParallel);
  EXPECT_EQ(m.souxmar_version_constraint, ">=1.0,<2.0");
  ASSERT_EQ(m.capabilities.size(),      1u);
  EXPECT_EQ(m.capabilities[0],          "mesher.tetra.netgen");
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
  EXPECT_NE(std::get<ParseError>(r).message.find("at least one"),
            std::string::npos);
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
  EXPECT_NE(std::get<ParseError>(r).message.find("threading"),
            std::string::npos);
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
  EXPECT_NE(std::get<ParseError>(r).message.find("ABI v1"),
            std::string::npos);
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
  EXPECT_EQ(to_string(ThreadingModel::Reentrant),       "reentrant");
  EXPECT_EQ(to_string(ThreadingModel::SingleThreaded),  "single-threaded");
  EXPECT_EQ(to_string(ThreadingModel::InternalParallel), "internal-parallel");
  EXPECT_EQ(threading_from_string("reentrant").value(), ThreadingModel::Reentrant);
  EXPECT_EQ(threading_from_string("single-threaded").value(), ThreadingModel::SingleThreaded);
  EXPECT_EQ(threading_from_string("internal-parallel").value(), ThreadingModel::InternalParallel);
  EXPECT_FALSE(threading_from_string("garbage").has_value());
}

}  // namespace
