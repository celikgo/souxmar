// SPDX-License-Identifier: Apache-2.0
//
// Update manifest parser + structural validator. See
// include/souxmar/update/manifest.h for the schema and
// docs/adr/0013-signed-update-manifest.md for the full design.
//
// We use tomlplusplus because the plugin manifest, plugin index, and
// pipeline parsers all already depend on it — no new third-party adds
// at this layer. Parser is defensive on optional fields (missing /
// empty / wrong-type all default to empty), strict on required fields
// (schema, channel.name, release.version, every artifact field, the
// signing block). Validator runs the publishability gates; the
// time-dependent freshness check is *not* here — it lives in the
// apply gate (push 6) where wall-clock is meaningful.

#include "souxmar/update/manifest.h"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace souxmar::update {

namespace fs = std::filesystem;

namespace {

// ---- Enum string-roundtrips ------------------------------------------------

std::optional<Channel> parse_channel(std::string_view s) noexcept {
  if (s == "stable")
    return Channel::Stable;
  if (s == "beta")
    return Channel::Beta;
  if (s == "nightly")
    return Channel::Nightly;
  return std::nullopt;
}

std::optional<Os> parse_os(std::string_view s) noexcept {
  if (s == "linux")
    return Os::Linux;
  if (s == "macos")
    return Os::Macos;
  if (s == "windows")
    return Os::Windows;
  return std::nullopt;
}

std::optional<Arch> parse_arch(std::string_view s) noexcept {
  if (s == "x86_64")
    return Arch::X86_64;
  if (s == "aarch64")
    return Arch::Aarch64;
  return std::nullopt;
}

// ---- Parser primitives ----------------------------------------------------

// Pull a required string field from a table; throws std::runtime_error
// with a human-readable diagnostic if missing or wrong type. The
// caller wraps the exception into a ManifestParseError.
std::string required_string(const toml::table& tbl, const char* key, std::string_view scope) {
  const auto* v = tbl.get(key);
  if (!v) {
    std::ostringstream oss;
    oss << scope << ": missing required field '" << key << "'";
    throw std::runtime_error(oss.str());
  }
  auto sv = v->value<std::string>();
  if (!sv) {
    std::ostringstream oss;
    oss << scope << ": field '" << key << "' must be a string";
    throw std::runtime_error(oss.str());
  }
  return *sv;
}

std::string optional_string(const toml::table& tbl, const char* key) {
  const auto* v = tbl.get(key);
  if (!v)
    return {};
  auto sv = v->value<std::string>();
  return sv ? *sv : std::string{};
}

bool optional_bool(const toml::table& tbl, const char* key, bool dv) {
  const auto* v = tbl.get(key);
  if (!v)
    return dv;
  auto bv = v->value<bool>();
  return bv ? *bv : dv;
}

std::uint64_t required_uint(const toml::table& tbl, const char* key, std::string_view scope) {
  const auto* v = tbl.get(key);
  if (!v) {
    std::ostringstream oss;
    oss << scope << ": missing required field '" << key << "'";
    throw std::runtime_error(oss.str());
  }
  // tomlplusplus stores integers as int64_t; reject negatives explicitly.
  auto iv = v->value<std::int64_t>();
  if (!iv) {
    std::ostringstream oss;
    oss << scope << ": field '" << key << "' must be a positive integer";
    throw std::runtime_error(oss.str());
  }
  if (*iv < 0) {
    std::ostringstream oss;
    oss << scope << ": field '" << key << "' must be non-negative (got " << *iv << ")";
    throw std::runtime_error(oss.str());
  }
  return static_cast<std::uint64_t>(*iv);
}

// ---- Schema parsing -------------------------------------------------------

Manifest parse_root(const toml::table& root) {
  // schema: required, must equal kManifestSchemaV1.
  const auto* schema_node = root.get("schema");
  if (!schema_node) {
    throw std::runtime_error("missing required field 'schema'");
  }
  auto schema_val = schema_node->value<std::int64_t>();
  if (!schema_val) {
    throw std::runtime_error("'schema' must be an integer");
  }
  if (*schema_val != static_cast<std::int64_t>(kManifestSchemaV1)) {
    std::ostringstream oss;
    oss << "unsupported manifest schema " << *schema_val
        << " (this parser supports schema=" << kManifestSchemaV1 << ")";
    throw std::runtime_error(oss.str());
  }

  Manifest out;
  out.schema = kManifestSchemaV1;
  out.generated_at = optional_string(root, "generated_at");

  // [channel]
  const auto* channel_tbl = root.get_as<toml::table>("channel");
  if (!channel_tbl) {
    throw std::runtime_error("missing required table [channel]");
  }
  {
    const auto name_str = required_string(*channel_tbl, "name", "[channel]");
    auto parsed = parse_channel(name_str);
    if (!parsed) {
      std::ostringstream oss;
      oss << "[channel]: unknown channel name '" << name_str
          << "' (expected stable, beta, or nightly)";
      throw std::runtime_error(oss.str());
    }
    out.channel.name = *parsed;
    out.channel.expires_at = optional_string(*channel_tbl, "expires_at");
  }

  // [release]
  const auto* release_tbl = root.get_as<toml::table>("release");
  if (!release_tbl) {
    throw std::runtime_error("missing required table [release]");
  }
  out.release.version = required_string(*release_tbl, "version", "[release]");
  out.release.released_at = optional_string(*release_tbl, "released_at");
  out.release.min_previous_version = optional_string(*release_tbl, "min_previous_version");
  out.release.rollback_target = optional_string(*release_tbl, "rollback_target");
  out.release.notes_url = optional_string(*release_tbl, "notes_url");
  out.release.mandatory = optional_bool(*release_tbl, "mandatory", false);

  // [[artifact]] — required, at least one entry.
  const auto* art_arr = root.get_as<toml::array>("artifact");
  if (!art_arr) {
    throw std::runtime_error("missing required table-array [[artifact]]");
  }
  if (art_arr->empty()) {
    throw std::runtime_error("[[artifact]] must contain at least one entry");
  }
  out.artifacts.reserve(art_arr->size());
  std::size_t idx = 0;
  for (const auto& node : *art_arr) {
    const auto* tbl = node.as_table();
    if (!tbl) {
      std::ostringstream oss;
      oss << "[[artifact]] entry #" << idx << " is not a table";
      throw std::runtime_error(oss.str());
    }
    const std::string scope = std::string("[[artifact]] #") + std::to_string(idx);
    const auto os_str = required_string(*tbl, "os", scope);
    const auto arch_str = required_string(*tbl, "arch", scope);
    auto os_v = parse_os(os_str);
    if (!os_v) {
      throw std::runtime_error(scope + ": unknown os '" + os_str
                               + "' (expected linux, macos, or windows)");
    }
    auto arch_v = parse_arch(arch_str);
    if (!arch_v) {
      throw std::runtime_error(scope + ": unknown arch '" + arch_str
                               + "' (expected x86_64 or aarch64)");
    }
    Artifact a;
    a.os = *os_v;
    a.arch = *arch_v;
    a.url = required_string(*tbl, "url", scope);
    a.sha256 = required_string(*tbl, "sha256", scope);
    a.size = required_uint(*tbl, "size", scope);
    out.artifacts.push_back(std::move(a));
    ++idx;
  }

  // [signing]
  const auto* sig_tbl = root.get_as<toml::table>("signing");
  if (!sig_tbl) {
    throw std::runtime_error("missing required table [signing]");
  }
  out.signing.algorithm = required_string(*sig_tbl, "algorithm", "[signing]");
  out.signing.public_key_id = required_string(*sig_tbl, "public_key_id", "[signing]");
  return out;
}

ManifestLoadResult wrap_parse(const toml::table& root, std::string_view ctx) {
  try {
    return parse_root(root);
  } catch (const std::exception& e) {
    ManifestParseError err;
    err.message = std::string(ctx) + ": " + e.what();
    return err;
  }
}

}  // namespace

ManifestLoadResult parse_manifest_file(const fs::path& path) {
  try {
    auto root = toml::parse_file(path.string());
    return wrap_parse(root, path.string());
  } catch (const toml::parse_error& e) {
    ManifestParseError err;
    std::ostringstream oss;
    oss << path.string() << ": " << e.description();
    err.message = oss.str();
    return err;
  } catch (const std::exception& e) {
    ManifestParseError err;
    err.message = path.string() + ": " + e.what();
    return err;
  }
}

ManifestLoadResult parse_manifest_string(std::string_view toml_text) {
  try {
    auto root = toml::parse(toml_text);
    return wrap_parse(root, "<string>");
  } catch (const toml::parse_error& e) {
    ManifestParseError err;
    err.message = std::string("<string>: ") + e.description().data();
    return err;
  } catch (const std::exception& e) {
    ManifestParseError err;
    err.message = std::string("<string>: ") + e.what();
    return err;
  }
}

std::string_view to_string(Channel c) noexcept {
  switch (c) {
    case Channel::Stable:
      return "stable";
    case Channel::Beta:
      return "beta";
    case Channel::Nightly:
      return "nightly";
  }
  return "unknown";
}

std::string_view to_string(Os o) noexcept {
  switch (o) {
    case Os::Linux:
      return "linux";
    case Os::Macos:
      return "macos";
    case Os::Windows:
      return "windows";
  }
  return "unknown";
}

std::string_view to_string(Arch a) noexcept {
  switch (a) {
    case Arch::X86_64:
      return "x86_64";
    case Arch::Aarch64:
      return "aarch64";
  }
  return "unknown";
}

std::string_view to_string(ManifestIssueSeverity s) noexcept {
  switch (s) {
    case ManifestIssueSeverity::Error:
      return "error";
    case ManifestIssueSeverity::Warning:
      return "warning";
  }
  return "unknown";
}

// ============================================================================
// Validation
// ============================================================================

namespace {

bool looks_like_http_url(std::string_view s) noexcept {
  if (s.size() < 8)
    return false;  // "http://x" minimum
  const bool https = s.starts_with("https://");
  const bool http = s.starts_with("http://");
  if (!https && !http)
    return false;
  const std::size_t prefix_len = https ? 8 : 7;
  if (s.size() <= prefix_len)
    return false;
  return s[prefix_len] != '/';
}

bool is_lowercase_hex_64(std::string_view s) noexcept {
  if (s.size() != 64)
    return false;
  for (char c : s) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!ok)
      return false;
  }
  return true;
}

std::string artifact_field(std::size_t i, const char* field) {
  return std::string("artifacts[") + std::to_string(i) + "]." + field;
}

}  // namespace

std::vector<ManifestValidationIssue> validate_manifest(const Manifest& m) {
  std::vector<ManifestValidationIssue> out;

  // Signing block: algorithm must be the ed25519 we ship today.
  // ADR-0013 keeps the field a string for forward compat (sigstore,
  // Ed448), but rejects every other value at v1.
  if (m.signing.algorithm != "ed25519") {
    out.push_back({ManifestIssueSeverity::Error,
                   "signing.algorithm",
                   "unsupported signing algorithm '" + m.signing.algorithm
                       + "' (v1 manifest must be 'ed25519')"});
  }
  if (m.signing.public_key_id.empty()) {
    out.push_back({ManifestIssueSeverity::Error,
                   "signing.public_key_id",
                   "public_key_id must not be empty — the verifier "
                   "needs an id to look up the trust-store key"});
  }

  // Release block: version must be three dot-separated numeric tokens.
  // We do not implement full SemVer here (no pre-release / build
  // metadata) — that grammar lives in the version-comparison module
  // (push 6); the validator's job is just to catch outright garbage.
  auto looks_semverish = [](std::string_view v) -> bool {
    if (v.empty())
      return false;
    int dots = 0;
    for (char c : v) {
      if (c == '.') {
        if (++dots > 2)
          return false;
      } else if (!(c >= '0' && c <= '9')) {
        return false;
      }
    }
    return dots == 2;
  };
  if (!looks_semverish(m.release.version)) {
    out.push_back({ManifestIssueSeverity::Error,
                   "release.version",
                   "version must look like MAJOR.MINOR.PATCH "
                   "(got: '"
                       + m.release.version + "')"});
  }
  if (m.release.rollback_target.empty()) {
    out.push_back({ManifestIssueSeverity::Warning,
                   "release.rollback_target",
                   "rollback_target is empty — rollback will be "
                   "disabled for this release; confirm this is "
                   "intentional"});
  }
  if (m.release.min_previous_version.empty()) {
    out.push_back({ManifestIssueSeverity::Warning,
                   "release.min_previous_version",
                   "min_previous_version is empty — clients at any "
                   "earlier version may apply this update"});
  }

  // Channel block: expires_at being empty is a warning. The actual
  // expiry-vs-now check is the apply gate's job (push 6).
  if (m.channel.expires_at.empty()) {
    out.push_back({ManifestIssueSeverity::Warning,
                   "channel.expires_at",
                   "channel.expires_at is empty — the freshness check "
                   "loses its grip on this manifest"});
  }

  // Artifacts: duplicate (os, arch) pairs are an error; per-artifact
  // structural checks below.
  std::unordered_set<std::uint16_t> seen_pairs;
  for (std::size_t i = 0; i < m.artifacts.size(); ++i) {
    const auto& a = m.artifacts[i];
    // (os << 8) | arch integer-promotes through int; cast back to
    // uint16_t to satisfy -Wconversion. Both enums are uint8_t-backed
    // (manifest.h), so the packed value always fits in 16 bits.
    const std::uint16_t pair = static_cast<std::uint16_t>((static_cast<std::uint16_t>(a.os) << 8)
                                                          | static_cast<std::uint16_t>(a.arch));
    if (!seen_pairs.insert(pair).second) {
      out.push_back({ManifestIssueSeverity::Error,
                     artifact_field(i, "os+arch"),
                     "duplicate (os, arch) pair (" + std::string(to_string(a.os)) + ", "
                         + std::string(to_string(a.arch))
                         + ") — each platform tuple must appear exactly once"});
    }
    if (!looks_like_http_url(a.url)) {
      out.push_back({ManifestIssueSeverity::Error,
                     artifact_field(i, "url"),
                     "url must start with http:// or https:// "
                     "(got: '"
                         + a.url + "')"});
    }
    if (!is_lowercase_hex_64(a.sha256)) {
      out.push_back({ManifestIssueSeverity::Error,
                     artifact_field(i, "sha256"),
                     "sha256 must be exactly 64 lowercase hex "
                     "characters (got: '"
                         + a.sha256 + "', " + std::to_string(a.sha256.size()) + " chars)"});
    }
    if (a.size == 0) {
      out.push_back({ManifestIssueSeverity::Error,
                     artifact_field(i, "size"),
                     "size must be > 0 (got 0) — the pre-flight "
                     "disk-space check cannot proceed against a "
                     "zero-byte artifact declaration"});
    }
  }

  return out;
}

}  // namespace souxmar::update
