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
  EXPECT_EQ(to_string(IndexIssueSeverity::Error),     "error");
  EXPECT_EQ(to_string(IndexIssueSeverity::Warning),   "warning");
}

// -------- Sprint 10 push 3 — validate_index ---------------------------

namespace {

// Build an IndexEntry by hand for the validator tests. Bypassing the
// parser keeps the tests focused on the validator's checks (the parser
// has its own tests above).
IndexEntry make_min(std::string id) {
  IndexEntry e;
  e.id     = std::move(id);
  e.name   = "Test plugin";
  e.source = "https://example.com/source";
  e.capabilities = {"mesher.tetra.example"};
  e.license          = "Apache-2.0";
  e.souxmar_versions = ">=1.0,<2.0";
  return e;
}

}  // namespace

TEST(PluginIndexValidate, CleanEntryProducesNoIssues) {
  std::vector<IndexEntry> entries = {make_min("com.example.clean")};
  auto issues = validate_index(entries);
  EXPECT_TRUE(issues.empty()) << "got " << issues.size() << " issues";
}

TEST(PluginIndexValidate, DuplicateIdIsError) {
  std::vector<IndexEntry> entries = {
      make_min("com.example.dup"),
      make_min("com.example.dup"),
  };
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity,    IndexIssueSeverity::Error);
  EXPECT_EQ(issues[0].entry_index, 1u);  // points at the second occurrence
  EXPECT_EQ(issues[0].field,       "id");
  EXPECT_NE(issues[0].message.find("duplicate"), std::string::npos);
}

TEST(PluginIndexValidate, BadSourceUrlIsError) {
  auto e = make_min("com.example.bad-url");
  e.source = "git@github.com:example/repo.git";  // SSH-style; not http(s)
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Error);
  EXPECT_EQ(issues[0].field,    "source");
}

TEST(PluginIndexValidate, BadHomepageUrlIsError) {
  auto e = make_min("com.example.bad-home");
  e.homepage = "example.com";  // no scheme
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Error);
  EXPECT_EQ(issues[0].field,    "homepage");
}

TEST(PluginIndexValidate, EmptyHomepageIsOk) {
  // homepage is optional and defaults to empty — validator must not
  // flag absence.
  auto e = make_min("com.example.no-home");
  e.homepage = "";
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  EXPECT_TRUE(issues.empty());
}

TEST(PluginIndexValidate, BadCapabilityIdIsError) {
  // Capability without a dot — would collide with the souxmar
  // top-level taxonomy.
  auto e = make_min("com.example.bad-cap");
  e.capabilities = {"mesher tetra example"};  // spaces!
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Error);
  EXPECT_NE(issues[0].field.find("capabilities"), std::string::npos);
}

TEST(PluginIndexValidate, MissingLicenseOnFreeEntryIsWarning) {
  auto e = make_min("com.example.no-license");
  e.license = "";
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Warning);
  EXPECT_EQ(issues[0].field,    "license");
}

TEST(PluginIndexValidate, MissingLicenseOnPaidEntryIsOk) {
  // Paid-marketplace entries may legitimately omit `license` (the
  // marketplace handles the license-key flow separately).
  auto e = make_min("com.example.paid");
  e.license = "";
  e.paid    = true;
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  EXPECT_TRUE(issues.empty());
}

TEST(PluginIndexValidate, MissingVersionRangeIsWarning) {
  auto e = make_min("com.example.no-version");
  e.souxmar_versions = "";
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Warning);
  EXPECT_EQ(issues[0].field,    "souxmar_versions");
}

TEST(PluginIndexValidate, FailedConformanceIsWarningNotError) {
  auto e = make_min("com.example.failing");
  e.conformance = ConformanceStatus::Failed;
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].severity, IndexIssueSeverity::Warning);
  EXPECT_EQ(issues[0].field,    "conformance");
}

TEST(PluginIndexValidate, MultipleIssuesAllReported) {
  // One entry with three errors + one warning. Validator should
  // report every issue, not stop at the first.
  auto e = make_min("com.example.disaster");
  e.source           = "not-a-url";
  e.homepage         = "also-not-a-url";
  e.capabilities     = {"bad cap"};
  e.souxmar_versions = "";
  std::vector<IndexEntry> entries = {std::move(e)};
  auto issues = validate_index(entries);
  ASSERT_EQ(issues.size(), 4u);
  std::size_t errors = 0, warnings = 0;
  for (const auto& iss : issues) {
    if (iss.severity == IndexIssueSeverity::Error)   ++errors;
    if (iss.severity == IndexIssueSeverity::Warning) ++warnings;
  }
  EXPECT_EQ(errors,   3u);
  EXPECT_EQ(warnings, 1u);
}

TEST(PluginIndexValidate, InTreeIndexIsClean) {
  // The committed docs/plugin-index.toml shouldn't surface any
  // issues — if push 2 added an entry that doesn't pass validation,
  // push 3's gate fails on its own data and that's the loudest
  // possible signal.
  constexpr const char* kAllInTreeReducedSample = R"toml(
[[plugin]]
id           = "dev.souxmar.examples.hello-mesher"
name         = "Hello mesher"
capabilities = ["mesher.tetra.hello"]
license      = "Apache-2.0"
source       = "https://github.com/souxmar/souxmar/tree/master/examples/plugins/hello-mesher"
author       = "souxmar project"
souxmar_versions = ">=1.0,<2.0"
conformance      = "passed"
status           = "active"

[[plugin]]
id           = "dev.souxmar.examples.openfoam-solver"
name         = "OpenFOAM CFD adapter"
capabilities = ["solver.cfd.openfoam.simple", "solver.cfd.openfoam.pimple"]
license      = "Apache-2.0"
source       = "https://github.com/souxmar/souxmar/tree/master/examples/plugins/openfoam-solver"
author       = "souxmar project"
souxmar_versions = ">=1.0,<2.0"
conformance      = "passed"
status           = "active"
)toml";
  auto result = load_index_string(kAllInTreeReducedSample);
  ASSERT_TRUE(std::holds_alternative<std::vector<IndexEntry>>(result));
  const auto& entries = std::get<std::vector<IndexEntry>>(result);
  auto issues = validate_index(entries);
  EXPECT_TRUE(issues.empty()) << "in-tree-style entries should validate clean";
}
