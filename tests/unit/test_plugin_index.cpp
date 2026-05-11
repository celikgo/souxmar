// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 2 — unit tests for the plugin-index data model.
// Schema in include/souxmar/plugin/index.h; parser + search in
// src/plugin-host/index.cpp.

#include "souxmar/plugin/index.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

using namespace souxmar::plugin;

namespace {

// Minimal valid entry — id, name, source, capabilities — plus a
// kitchen-sink entry covering every optional field.
constexpr const char* kMinimalToml = R"toml(
[[plugin]]
id           = "com.example.minimal"
name         = "Minimal plugin"
source       = "https://example.com/source"
capabilities = ["mesher.tetra.example"]
)toml";

constexpr const char* kFullToml = R"toml(
[[plugin]]
id           = "com.example.full"
name         = "Full plugin"
description  = "Every optional field exercised."
capabilities = ["solver.fem.poisson", "solver.fem.heat"]
license      = "Apache-2.0"
source       = "https://example.com/source"
homepage     = "https://example.com/"
author       = "Example Inc."
souxmar_versions = ">=1.0,<2.0"
conformance      = "passed"
conformance_date = "2026-04-30"
status           = "maintained"
paid             = true

[[plugin]]
id           = "com.example.unmaintained"
name         = "Stale plugin"
capabilities = ["reader.legacy"]
source       = "https://example.com/legacy"
status       = "unmaintained"
conformance  = "failed"
)toml";

}  // namespace

TEST(PluginIndex, ParseMinimalEntrySetsRequiredFields) {
  auto result = load_index_string(kMinimalToml);
  ASSERT_TRUE(std::holds_alternative<std::vector<IndexEntry>>(result));
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  ASSERT_EQ(entries.size(), 1u);
  const auto& e = entries[0];
  EXPECT_EQ(e.id, "com.example.minimal");
  EXPECT_EQ(e.name, "Minimal plugin");
  EXPECT_EQ(e.source, "https://example.com/source");
  ASSERT_EQ(e.capabilities.size(), 1u);
  EXPECT_EQ(e.capabilities[0], "mesher.tetra.example");
  // Optional defaults.
  EXPECT_TRUE(e.description.empty());
  EXPECT_TRUE(e.license.empty());
  EXPECT_TRUE(e.author.empty());
  EXPECT_EQ(e.conformance, ConformanceStatus::NotRun);
  EXPECT_EQ(e.status,      LifecycleStatus::Active);
  EXPECT_FALSE(e.paid);
}

TEST(PluginIndex, ParseFullEntryRoundtripsEveryField) {
  auto result = load_index_string(kFullToml);
  ASSERT_TRUE(std::holds_alternative<std::vector<IndexEntry>>(result));
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  ASSERT_EQ(entries.size(), 2u);
  const auto& full = entries[0];
  EXPECT_EQ(full.id, "com.example.full");
  EXPECT_EQ(full.description, "Every optional field exercised.");
  EXPECT_EQ(full.license, "Apache-2.0");
  EXPECT_EQ(full.homepage, "https://example.com/");
  EXPECT_EQ(full.author, "Example Inc.");
  EXPECT_EQ(full.souxmar_versions, ">=1.0,<2.0");
  EXPECT_EQ(full.conformance, ConformanceStatus::Passed);
  EXPECT_EQ(full.conformance_date, "2026-04-30");
  EXPECT_EQ(full.status, LifecycleStatus::Maintained);
  EXPECT_TRUE(full.paid);
  ASSERT_EQ(full.capabilities.size(), 2u);
  EXPECT_EQ(full.capabilities[0], "solver.fem.poisson");
  EXPECT_EQ(full.capabilities[1], "solver.fem.heat");

  const auto& stale = entries[1];
  EXPECT_EQ(stale.id, "com.example.unmaintained");
  EXPECT_EQ(stale.status, LifecycleStatus::Unmaintained);
  EXPECT_EQ(stale.conformance, ConformanceStatus::Failed);
  EXPECT_FALSE(stale.paid);  // default
}

TEST(PluginIndex, MissingRequiredFieldRejectsWithMessage) {
  // Missing 'source' on an otherwise-valid entry.
  constexpr const char* kNoSource = R"toml(
[[plugin]]
id           = "com.example.bad"
name         = "No source"
capabilities = ["x.y"]
)toml";
  auto result = load_index_string(kNoSource);
  ASSERT_TRUE(std::holds_alternative<IndexParseError>(result));
  EXPECT_NE(std::get<IndexParseError>(result).message.find("source"),
            std::string::npos)
      << "error message should name the missing field";
}

TEST(PluginIndex, EmptyCapabilitiesRejected) {
  constexpr const char* kEmptyCaps = R"toml(
[[plugin]]
id           = "com.example.bad"
name         = "Empty caps"
source       = "https://example.com/x"
capabilities = []
)toml";
  auto result = load_index_string(kEmptyCaps);
  ASSERT_TRUE(std::holds_alternative<IndexParseError>(result));
  EXPECT_NE(std::get<IndexParseError>(result).message.find("capabilities"),
            std::string::npos);
}

TEST(PluginIndex, MalformedTomlSurfacesParseError) {
  // Unclosed string — toml++ rejects.
  auto result = load_index_string("[[plugin]]\nid = \"unterminated\n");
  EXPECT_TRUE(std::holds_alternative<IndexParseError>(result));
}

TEST(PluginIndex, SearchEmptyQueryReturnsAll) {
  auto result = load_index_string(kFullToml);
  ASSERT_TRUE(std::holds_alternative<std::vector<IndexEntry>>(result));
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  auto hits = search_index(entries, "");
  EXPECT_EQ(hits.size(), entries.size());
}

TEST(PluginIndex, SearchSubstringMatchesAcrossFields) {
  auto result = load_index_string(kFullToml);
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  // Match by id substring (case insensitive).
  auto hits_id = search_index(entries, "FULL");
  ASSERT_EQ(hits_id.size(), 1u);
  EXPECT_EQ(hits_id[0].id, "com.example.full");
  // Match by author substring.
  auto hits_author = search_index(entries, "example inc");
  ASSERT_EQ(hits_author.size(), 1u);
  EXPECT_EQ(hits_author[0].id, "com.example.full");
  // Match by capability name.
  auto hits_cap = search_index(entries, "poisson");
  ASSERT_EQ(hits_cap.size(), 1u);
  // Match by description.
  auto hits_desc = search_index(entries, "every optional");
  ASSERT_EQ(hits_desc.size(), 1u);
}

TEST(PluginIndex, SearchByCapabilityPrefixRestrictsResults) {
  auto result = load_index_string(kFullToml);
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  // Only com.example.full registers solver.* capabilities.
  auto hits = search_index(entries, "", "solver.fem.");
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].id, "com.example.full");
  // No plugin registers solver.cfd.* — empty result.
  auto miss = search_index(entries, "", "solver.cfd.");
  EXPECT_TRUE(miss.empty());
  // Reader prefix matches only com.example.unmaintained.
  auto rd = search_index(entries, "", "reader.");
  ASSERT_EQ(rd.size(), 1u);
  EXPECT_EQ(rd[0].id, "com.example.unmaintained");
}

TEST(PluginIndex, SearchPreservesInputOrder) {
  auto result = load_index_string(kFullToml);
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  // Empty query → input order; full → unmaintained.
  auto hits = search_index(entries, "");
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].id, "com.example.full");
  EXPECT_EQ(hits[1].id, "com.example.unmaintained");
}

TEST(PluginIndex, StatusStringsRoundtrip) {
  EXPECT_EQ(to_string(LifecycleStatus::Active),       "active");
  EXPECT_EQ(to_string(LifecycleStatus::Maintained),   "maintained");
  EXPECT_EQ(to_string(LifecycleStatus::Unmaintained), "unmaintained");
  EXPECT_EQ(to_string(LifecycleStatus::Archived),     "archived");
  EXPECT_EQ(to_string(ConformanceStatus::Passed),     "passed");
  EXPECT_EQ(to_string(ConformanceStatus::NotRun),     "not_run");
  EXPECT_EQ(to_string(ConformanceStatus::Failed),     "failed");
}
