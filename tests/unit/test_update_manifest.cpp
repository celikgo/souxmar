// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 4 — unit tests for the signed update manifest.
// Schema in include/souxmar/update/manifest.h; parser + validator in
// src/updater/manifest.cpp; design lock-in in ADR-0013.

#include "souxmar/update/manifest.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace souxmar::update;

namespace {

// 64-character lowercase hex literal — used wherever a test wants
// "a structurally valid sha256". Bytes don't matter; only the shape.
constexpr const char* kFakeSha =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// Minimal valid manifest — schema, channel, release, exactly one
// artifact, signing block.
constexpr const char* kMinimalToml = R"toml(
schema       = 1
generated_at = "2026-05-11T14:00:00Z"

[channel]
name       = "stable"
expires_at = "2026-08-11T14:00:00Z"

[release]
version              = "0.9.0"
released_at          = "2026-05-10T10:00:00Z"
min_previous_version = "0.8.0"
rollback_target      = "0.8.4"
notes_url            = "https://souxmar.dev/releases/0.9.0"
mandatory            = false

[[artifact]]
os     = "linux"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/0.9.0/souxmar-0.9.0-linux-x86_64.tar.zst"
sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 48217600

[signing]
algorithm     = "ed25519"
public_key_id = "release-2026"
)toml";

// Full manifest — five artifacts (the three OSes × the relevant
// arches). Used in roundtrip + validator-clean tests.
constexpr const char* kFullFiveArtifactToml = R"toml(
schema       = 1
generated_at = "2026-05-11T14:00:00Z"

[channel]
name       = "stable"
expires_at = "2026-08-11T14:00:00Z"

[release]
version              = "0.9.0"
released_at          = "2026-05-10T10:00:00Z"
min_previous_version = "0.8.0"
rollback_target      = "0.8.4"
notes_url            = "https://souxmar.dev/releases/0.9.0"
mandatory            = true

[[artifact]]
os     = "linux"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/0.9.0/linux-x86_64.tar.zst"
sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 48217600

[[artifact]]
os     = "linux"
arch   = "aarch64"
url    = "https://dl.souxmar.dev/0.9.0/linux-aarch64.tar.zst"
sha256 = "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 47900800

[[artifact]]
os     = "macos"
arch   = "aarch64"
url    = "https://dl.souxmar.dev/0.9.0/macos-aarch64.tar.zst"
sha256 = "2123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 52300100

[[artifact]]
os     = "macos"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/0.9.0/macos-x86_64.tar.zst"
sha256 = "3123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 53100200

[[artifact]]
os     = "windows"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/0.9.0/windows-x86_64.zip"
sha256 = "4123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 51200000

[signing]
algorithm     = "ed25519"
public_key_id = "release-2026"
)toml";

const Manifest& expect_ok(const ManifestLoadResult& r) {
  if (auto* err = std::get_if<ManifestParseError>(&r)) {
    ADD_FAILURE() << "unexpected parse error: " << err->message;
  }
  return std::get<Manifest>(r);
}

const ManifestParseError& expect_err(const ManifestLoadResult& r) {
  if (std::holds_alternative<Manifest>(r)) {
    ADD_FAILURE() << "expected parse error, got a Manifest";
  }
  return std::get<ManifestParseError>(r);
}

bool has_error_for_field(const std::vector<ManifestValidationIssue>& issues,
                         std::string_view field) {
  return std::any_of(issues.begin(), issues.end(),
                     [&](const auto& i) {
                       return i.severity == ManifestIssueSeverity::Error &&
                              i.field == field;
                     });
}

bool has_warning_for_field(const std::vector<ManifestValidationIssue>& issues,
                           std::string_view field) {
  return std::any_of(issues.begin(), issues.end(),
                     [&](const auto& i) {
                       return i.severity == ManifestIssueSeverity::Warning &&
                              i.field == field;
                     });
}

bool any_error(const std::vector<ManifestValidationIssue>& issues) {
  return std::any_of(issues.begin(), issues.end(),
                     [](const auto& i) {
                       return i.severity == ManifestIssueSeverity::Error;
                     });
}

}  // namespace

// ===========================================================================
// Parser — happy paths
// ===========================================================================

TEST(UpdateManifest, ParseMinimalValidManifest) {
  const auto& m = expect_ok(parse_manifest_string(kMinimalToml));
  EXPECT_EQ(m.schema,            kManifestSchemaV1);
  EXPECT_EQ(m.generated_at,      "2026-05-11T14:00:00Z");
  EXPECT_EQ(m.channel.name,      Channel::Stable);
  EXPECT_EQ(m.channel.expires_at, "2026-08-11T14:00:00Z");
  EXPECT_EQ(m.release.version,              "0.9.0");
  EXPECT_EQ(m.release.min_previous_version, "0.8.0");
  EXPECT_EQ(m.release.rollback_target,      "0.8.4");
  EXPECT_FALSE(m.release.mandatory);
  ASSERT_EQ(m.artifacts.size(), 1u);
  EXPECT_EQ(m.artifacts[0].os,   Os::Linux);
  EXPECT_EQ(m.artifacts[0].arch, Arch::X86_64);
  EXPECT_EQ(m.artifacts[0].sha256, std::string(kFakeSha));
  EXPECT_EQ(m.artifacts[0].size, 48217600u);
  EXPECT_EQ(m.signing.algorithm,     "ed25519");
  EXPECT_EQ(m.signing.public_key_id, "release-2026");
}

TEST(UpdateManifest, ParseFullFiveArtifactRoundtripsEveryField) {
  const auto& m = expect_ok(parse_manifest_string(kFullFiveArtifactToml));
  ASSERT_EQ(m.artifacts.size(), 5u);
  EXPECT_TRUE(m.release.mandatory);
  // Spot-check ordering preserved.
  EXPECT_EQ(m.artifacts[0].os,   Os::Linux);
  EXPECT_EQ(m.artifacts[0].arch, Arch::X86_64);
  EXPECT_EQ(m.artifacts[2].os,   Os::Macos);
  EXPECT_EQ(m.artifacts[2].arch, Arch::Aarch64);
  EXPECT_EQ(m.artifacts[4].os,   Os::Windows);
  EXPECT_EQ(m.artifacts[4].arch, Arch::X86_64);
}

TEST(UpdateManifest, ChannelStringRoundtrip) {
  EXPECT_EQ(to_string(Channel::Stable),  "stable");
  EXPECT_EQ(to_string(Channel::Beta),    "beta");
  EXPECT_EQ(to_string(Channel::Nightly), "nightly");
  EXPECT_EQ(to_string(Os::Linux),        "linux");
  EXPECT_EQ(to_string(Os::Macos),        "macos");
  EXPECT_EQ(to_string(Os::Windows),      "windows");
  EXPECT_EQ(to_string(Arch::X86_64),     "x86_64");
  EXPECT_EQ(to_string(Arch::Aarch64),    "aarch64");
}

TEST(UpdateManifest, BetaAndNightlyChannelsParse) {
  std::string with_beta = kMinimalToml;
  auto pos = with_beta.find("name       = \"stable\"");
  ASSERT_NE(pos, std::string::npos);
  with_beta.replace(pos, std::string("name       = \"stable\"").size(),
                    "name       = \"beta\"");
  EXPECT_EQ(expect_ok(parse_manifest_string(with_beta)).channel.name,
            Channel::Beta);

  std::string with_nightly = kMinimalToml;
  pos = with_nightly.find("name       = \"stable\"");
  ASSERT_NE(pos, std::string::npos);
  with_nightly.replace(pos, std::string("name       = \"stable\"").size(),
                       "name       = \"nightly\"");
  EXPECT_EQ(expect_ok(parse_manifest_string(with_nightly)).channel.name,
            Channel::Nightly);
}

// ===========================================================================
// Parser — error paths
// ===========================================================================

TEST(UpdateManifest, RejectsMissingSchema) {
  constexpr const char* kNoSchema = R"toml(
[channel]
name = "stable"

[release]
version = "0.9.0"

[[artifact]]
os = "linux"
arch = "x86_64"
url = "https://example.com/x"
sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size = 1

[signing]
algorithm = "ed25519"
public_key_id = "k"
)toml";
  EXPECT_NE(expect_err(parse_manifest_string(kNoSchema)).message.find("schema"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsUnknownSchemaVersion) {
  std::string future = kMinimalToml;
  auto pos = future.find("schema       = 1");
  ASSERT_NE(pos, std::string::npos);
  future.replace(pos, std::string("schema       = 1").size(),
                 "schema       = 999");
  const auto err = expect_err(parse_manifest_string(future));
  EXPECT_NE(err.message.find("999"), std::string::npos);
  EXPECT_NE(err.message.find("schema"), std::string::npos);
}

TEST(UpdateManifest, RejectsUnknownChannelName) {
  std::string bad = kMinimalToml;
  auto pos = bad.find("\"stable\"");
  ASSERT_NE(pos, std::string::npos);
  bad.replace(pos, std::string("\"stable\"").size(), "\"experimental\"");
  EXPECT_NE(expect_err(parse_manifest_string(bad)).message.find("experimental"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsUnknownOs) {
  std::string bad = kMinimalToml;
  auto pos = bad.find("os     = \"linux\"");
  ASSERT_NE(pos, std::string::npos);
  bad.replace(pos, std::string("os     = \"linux\"").size(),
              "os     = \"haiku\"");
  EXPECT_NE(expect_err(parse_manifest_string(bad)).message.find("haiku"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsUnknownArch) {
  std::string bad = kMinimalToml;
  auto pos = bad.find("arch   = \"x86_64\"");
  ASSERT_NE(pos, std::string::npos);
  bad.replace(pos, std::string("arch   = \"x86_64\"").size(),
              "arch   = \"riscv64\"");
  EXPECT_NE(expect_err(parse_manifest_string(bad)).message.find("riscv64"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsMissingRequiredArtifactField) {
  // Remove the size line — required field.
  std::string bad = kMinimalToml;
  auto pos = bad.find("size   = 48217600\n");
  ASSERT_NE(pos, std::string::npos);
  bad.erase(pos, std::string("size   = 48217600\n").size());
  EXPECT_NE(expect_err(parse_manifest_string(bad)).message.find("size"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsMalformedToml) {
  EXPECT_NE(expect_err(parse_manifest_string("not toml = = =")).message.size(),
            0u);
}

TEST(UpdateManifest, RejectsEmptyArtifactArray) {
  constexpr const char* kNoArt = R"toml(
schema = 1

[channel]
name = "stable"

[release]
version = "0.9.0"

[signing]
algorithm = "ed25519"
public_key_id = "k"
)toml";
  EXPECT_NE(expect_err(parse_manifest_string(kNoArt)).message.find("artifact"),
            std::string::npos);
}

TEST(UpdateManifest, RejectsNegativeSize) {
  std::string bad = kMinimalToml;
  auto pos = bad.find("size   = 48217600");
  ASSERT_NE(pos, std::string::npos);
  bad.replace(pos, std::string("size   = 48217600").size(),
              "size   = -1");
  EXPECT_NE(expect_err(parse_manifest_string(bad)).message.find("non-negative"),
            std::string::npos);
}

// ===========================================================================
// Validator — happy paths
// ===========================================================================

TEST(UpdateManifestValidator, FullManifestValidatesClean) {
  const auto& m = expect_ok(parse_manifest_string(kFullFiveArtifactToml));
  const auto issues = validate_manifest(m);
  for (const auto& i : issues) {
    ADD_FAILURE() << "unexpected issue: " << to_string(i.severity)
                  << " " << i.field << ": " << i.message;
  }
  EXPECT_TRUE(issues.empty());
}

TEST(UpdateManifestValidator, MinimalIsCleanWithNoWarnings) {
  // kMinimalToml deliberately fills in every optional field a real
  // release would (rollback_target, min_previous_version,
  // channel.expires_at), so the validator should report nothing —
  // not even a warning. This guards against a regression where the
  // common-case happy path starts producing spurious noise.
  const auto& m = expect_ok(parse_manifest_string(kMinimalToml));
  const auto issues = validate_manifest(m);
  for (const auto& i : issues) {
    ADD_FAILURE() << "unexpected issue: " << to_string(i.severity)
                  << " " << i.field << ": " << i.message;
  }
  EXPECT_TRUE(issues.empty());
}

// ===========================================================================
// Validator — error cases
// ===========================================================================

TEST(UpdateManifestValidator, RejectsNonEd25519Algorithm) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.signing.algorithm = "rsa-2048";
  const auto issues = validate_manifest(m);
  EXPECT_TRUE(has_error_for_field(issues, "signing.algorithm"));
}

TEST(UpdateManifestValidator, RejectsEmptyPublicKeyId) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.signing.public_key_id.clear();
  EXPECT_TRUE(has_error_for_field(validate_manifest(m),
                                  "signing.public_key_id"));
}

TEST(UpdateManifestValidator, RejectsMalformedVersion) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.release.version = "0.9";   // two tokens, not three
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "release.version"));

  m.release.version = "v0.9.0"; // not numeric
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "release.version"));

  m.release.version = "0.9.0.1"; // four tokens
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "release.version"));
}

TEST(UpdateManifestValidator, RejectsBadShaLength) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.artifacts[0].sha256 = "abc";
  EXPECT_TRUE(has_error_for_field(validate_manifest(m),
                                  "artifacts[0].sha256"));
}

TEST(UpdateManifestValidator, RejectsUppercaseShaChars) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.artifacts[0].sha256 =
      "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
  EXPECT_TRUE(has_error_for_field(validate_manifest(m),
                                  "artifacts[0].sha256"));
}

TEST(UpdateManifestValidator, RejectsBadUrlScheme) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.artifacts[0].url = "ftp://example.com/x";
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "artifacts[0].url"));

  m.artifacts[0].url = "/local/path";
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "artifacts[0].url"));
}

TEST(UpdateManifestValidator, RejectsZeroSize) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.artifacts[0].size = 0;
  EXPECT_TRUE(has_error_for_field(validate_manifest(m), "artifacts[0].size"));
}

TEST(UpdateManifestValidator, RejectsDuplicateOsArchPair) {
  auto m = expect_ok(parse_manifest_string(kFullFiveArtifactToml));
  // Force the macos/x86_64 entry (index 3) to be linux/x86_64 — a
  // duplicate of entry 0.
  m.artifacts[3].os   = Os::Linux;
  m.artifacts[3].arch = Arch::X86_64;
  const auto issues = validate_manifest(m);
  // The duplicate is at index 3 in the second-occurrence convention.
  EXPECT_TRUE(has_error_for_field(issues, "artifacts[3].os+arch"));
}

// ===========================================================================
// Validator — warning cases
// ===========================================================================

TEST(UpdateManifestValidator, WarnsOnEmptyRollbackTarget) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.release.rollback_target.clear();
  EXPECT_TRUE(has_warning_for_field(validate_manifest(m),
                                    "release.rollback_target"));
  EXPECT_FALSE(any_error(validate_manifest(m)));
}

TEST(UpdateManifestValidator, WarnsOnEmptyMinPreviousVersion) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.release.min_previous_version.clear();
  EXPECT_TRUE(has_warning_for_field(validate_manifest(m),
                                    "release.min_previous_version"));
  EXPECT_FALSE(any_error(validate_manifest(m)));
}

TEST(UpdateManifestValidator, WarnsOnEmptyChannelExpiresAt) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.channel.expires_at.clear();
  EXPECT_TRUE(has_warning_for_field(validate_manifest(m),
                                    "channel.expires_at"));
  EXPECT_FALSE(any_error(validate_manifest(m)));
}

// ===========================================================================
// Multi-issue accumulation — every issue must report, not just the first
// ===========================================================================

TEST(UpdateManifestValidator, ReportsAllIssuesOnMultiBrokenManifest) {
  auto m = expect_ok(parse_manifest_string(kMinimalToml));
  m.signing.algorithm           = "rsa-2048";
  m.release.version             = "garbage";
  m.artifacts[0].sha256         = "short";
  m.artifacts[0].url            = "not-a-url";
  m.artifacts[0].size           = 0;
  m.release.rollback_target.clear();   // warning

  const auto issues = validate_manifest(m);

  // Five distinct errors expected.
  EXPECT_TRUE(has_error_for_field(issues, "signing.algorithm"));
  EXPECT_TRUE(has_error_for_field(issues, "release.version"));
  EXPECT_TRUE(has_error_for_field(issues, "artifacts[0].sha256"));
  EXPECT_TRUE(has_error_for_field(issues, "artifacts[0].url"));
  EXPECT_TRUE(has_error_for_field(issues, "artifacts[0].size"));
  // Plus at least one warning.
  EXPECT_TRUE(has_warning_for_field(issues, "release.rollback_target"));
}
