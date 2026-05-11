// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater apply-gate. See include/souxmar/update/state_machine.h
// for the surface and ADR-0013 for the design.
//
// Implementation notes:
//   * No I/O, no syscalls in the gate proper. Time comes through
//     TimeSource so unit tests pin it; platform comes through
//     HostPlatform which the caller fills in.
//   * RFC-3339 parsing is deliberately strict (capital Z, no fractional
//     seconds). The manifest emitter is required to produce that exact
//     shape; relaxing here would mask a release-pipeline bug.
//   * Version comparison parses MAJOR.MINOR.PATCH into three uint32s
//     and compares lexicographically. Pre-release suffixes are
//     rejected (return nullopt). Loose-parsing was considered and
//     rejected — see the header for the rationale.

#include "souxmar/update/state_machine.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#if defined(__linux__)
  #include <linux/limits.h>
#endif

namespace souxmar::update {

namespace {

// ---- Numeric helpers -------------------------------------------------------

// Parse a sequence of ASCII decimal digits as uint32. Returns nullopt
// if `s` is empty, contains a non-digit, or overflows. Leading zeros
// are allowed ("09" parses to 9) — the manifest validator already
// forbids them, but lenient parsing here keeps the apply-gate's
// rejection vocabulary focused on policy, not lexical bookkeeping.
std::optional<std::uint32_t> parse_u32(std::string_view s) noexcept {
  if (s.empty()) return std::nullopt;
  std::uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
    if (v > 0xFFFFFFFFu) return std::nullopt;
  }
  return static_cast<std::uint32_t>(v);
}

// Split a SemVer-ish "X.Y.Z" into three uint32. Returns nullopt on any
// shape that doesn't match exactly (extra components, missing parts,
// pre-release suffixes, build-metadata).
std::optional<std::array<std::uint32_t, 3>>
split_version(std::string_view v) noexcept {
  std::array<std::string_view, 3> parts{};
  std::size_t cur = 0;
  for (int i = 0; i < 2; ++i) {
    const auto dot = v.find('.', cur);
    if (dot == std::string_view::npos) return std::nullopt;
    parts[static_cast<std::size_t>(i)] = v.substr(cur, dot - cur);
    cur = dot + 1;
  }
  parts[2] = v.substr(cur);
  // Reject any further dots or '-' or '+' in the patch component
  // (i.e. reject pre-release suffixes).
  for (char c : parts[2]) {
    if (c < '0' || c > '9') return std::nullopt;
  }
  std::array<std::uint32_t, 3> out{};
  for (std::size_t i = 0; i < 3; ++i) {
    auto p = parse_u32(parts[i]);
    if (!p) return std::nullopt;
    out[i] = *p;
  }
  return out;
}

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

}  // namespace

// ---- Version compare --------------------------------------------------------

std::optional<int>
compare_versions(std::string_view a, std::string_view b) {
  auto pa = split_version(a);
  auto pb = split_version(b);
  if (!pa || !pb) return std::nullopt;
  for (std::size_t i = 0; i < 3; ++i) {
    if ((*pa)[i] < (*pb)[i]) return -1;
    if ((*pa)[i] > (*pb)[i]) return +1;
  }
  return 0;
}

// ---- RFC-3339 parser -------------------------------------------------------

std::optional<std::chrono::system_clock::time_point>
parse_rfc3339_utc(std::string_view s) {
  // "YYYY-MM-DDTHH:MM:SSZ" — exactly 20 characters.
  if (s.size() != 20)                return std::nullopt;
  for (std::size_t i = 0; i < 20; ++i) {
    const char c = s[i];
    switch (i) {
      case 4: case 7:                                 if (c != '-') return std::nullopt; continue;
      case 10:                                        if (c != 'T') return std::nullopt; continue;
      case 13: case 16:                               if (c != ':') return std::nullopt; continue;
      case 19:                                        if (c != 'Z') return std::nullopt; continue;
      default:                                        if (!is_digit(c)) return std::nullopt;
    }
  }
  auto two_digits = [&](std::size_t off) -> int {
    return (s[off] - '0') * 10 + (s[off + 1] - '0');
  };
  const int year   = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
  const int month  = two_digits(5);
  const int day    = two_digits(8);
  const int hour   = two_digits(11);
  const int minute = two_digits(14);
  const int second = two_digits(17);

  // Component-range checks. Days-per-month is approximate (28-31 always
  // works); we don't validate Feb 29th on non-leap years, because mktime/
  // timegm below will normalise and we'd rather reject the corner case
  // than carry a calendar.
  if (year   < 1970 || year   > 9999) return std::nullopt;
  if (month  < 1    || month  > 12)   return std::nullopt;
  if (day    < 1    || day    > 31)   return std::nullopt;
  if (hour   > 23)                    return std::nullopt;
  if (minute > 59)                    return std::nullopt;
  if (second > 60)                    return std::nullopt;  // allow leap-second

  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon  = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min  = minute;
  tm.tm_sec  = second;
  // timegm is non-POSIX but present on Linux + macOS. On Windows the
  // CRT exposes _mkgmtime. Wrap to keep the platform fork small.
#if defined(_WIN32)
  const std::time_t epoch_secs = ::_mkgmtime(&tm);
#else
  const std::time_t epoch_secs = ::timegm(&tm);
#endif
  if (epoch_secs == static_cast<std::time_t>(-1)) return std::nullopt;
  return std::chrono::system_clock::from_time_t(epoch_secs);
}

std::string format_rfc3339_utc(std::chrono::system_clock::time_point t) {
  const std::time_t epoch_secs = std::chrono::system_clock::to_time_t(t);
  std::tm tm{};
#if defined(_WIN32)
  ::gmtime_s(&tm, &epoch_secs);
#else
  ::gmtime_r(&epoch_secs, &tm);
#endif
  // 20 chars + trailing NUL.
  std::array<char, 24> buf{};
  // The format spec is fixed; we don't want locale-dependent strftime
  // here. Hand-roll for byte determinism.
  auto put2 = [&](std::size_t off, int v) {
    buf[off]     = static_cast<char>('0' + (v / 10) % 10);
    buf[off + 1] = static_cast<char>('0' + (v % 10));
  };
  const int year = tm.tm_year + 1900;
  buf[0] = static_cast<char>('0' + (year / 1000) % 10);
  buf[1] = static_cast<char>('0' + (year / 100)  % 10);
  buf[2] = static_cast<char>('0' + (year / 10)   % 10);
  buf[3] = static_cast<char>('0' + (year)        % 10);
  buf[4]  = '-'; put2(5,  tm.tm_mon + 1);
  buf[7]  = '-'; put2(8,  tm.tm_mday);
  buf[10] = 'T'; put2(11, tm.tm_hour);
  buf[13] = ':'; put2(14, tm.tm_min);
  buf[16] = ':'; put2(17, tm.tm_sec);
  buf[19] = 'Z';
  return std::string(buf.data(), 20);
}

// ---- Host platform ---------------------------------------------------------

HostPlatform detect_host_platform() noexcept {
  HostPlatform p{};
#if defined(__linux__)
  p.os = Os::Linux;
#elif defined(__APPLE__)
  p.os = Os::Macos;
#elif defined(_WIN32)
  p.os = Os::Windows;
#else
  // Defaulting to Linux on unknown POSIX-likes preserves the most-common
  // distribution case; the apply-gate will still match-or-fail by arch.
  p.os = Os::Linux;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  p.arch = Arch::Aarch64;
#elif defined(__x86_64__) || defined(_M_X64)
  p.arch = Arch::X86_64;
#else
  p.arch = Arch::X86_64;
#endif
  return p;
}

std::optional<HostPlatform> parse_host_platform(std::string_view os_arch) {
  const auto slash = os_arch.find('/');
  if (slash == std::string_view::npos) return std::nullopt;
  const auto os_s   = os_arch.substr(0, slash);
  const auto arch_s = os_arch.substr(slash + 1);
  HostPlatform p{};
  if      (os_s == "linux")   p.os = Os::Linux;
  else if (os_s == "macos")   p.os = Os::Macos;
  else if (os_s == "windows") p.os = Os::Windows;
  else                        return std::nullopt;
  if      (arch_s == "x86_64")  p.arch = Arch::X86_64;
  else if (arch_s == "aarch64") p.arch = Arch::Aarch64;
  else                          return std::nullopt;
  return p;
}

// ---- SystemTimeSource ------------------------------------------------------

std::chrono::system_clock::time_point SystemTimeSource::now() const {
  return std::chrono::system_clock::now();
}

// ---- RefusalReason string --------------------------------------------------

std::string_view to_string(RefusalReason r) noexcept {
  switch (r) {
    case RefusalReason::AlreadyOnOrAheadOfOffered: return "already-on-or-ahead-of-offered";
    case RefusalReason::Expired:                   return "expired";
    case RefusalReason::ExpiredInvalidTime:        return "expired-invalid-time";
    case RefusalReason::BelowMinPrevious:          return "below-min-previous";
    case RefusalReason::ReplayDowngrade:           return "replay-downgrade";
    case RefusalReason::NoArtifactForPlatform:     return "no-artifact-for-platform";
    case RefusalReason::MalformedOfferedVersion:   return "malformed-offered-version";
    case RefusalReason::MalformedCurrentVersion:   return "malformed-current-version";
    case RefusalReason::MalformedReplayFloor:      return "malformed-replay-floor";
  }
  return "unknown";
}

// ---- apply_gate ------------------------------------------------------------

namespace {

UpdateDecision refuse(RefusalReason reason, std::string detail) {
  return UpdateRefusal{reason, std::move(detail)};
}

}  // namespace

UpdateDecision
apply_gate(const Manifest&       m,
           const CurrentInstall& install,
           const TimeSource&     clock) {
  // 1. Offered version must be well-formed.
  if (!split_version(m.release.version)) {
    return refuse(RefusalReason::MalformedOfferedVersion,
                  "release.version '" + m.release.version +
                      "' is not MAJOR.MINOR.PATCH numeric");
  }

  // 2. Current version (if non-empty) must be well-formed.
  if (!install.current_version.empty() &&
      !split_version(install.current_version)) {
    return refuse(RefusalReason::MalformedCurrentVersion,
                  "current_version '" + install.current_version +
                      "' is not MAJOR.MINOR.PATCH numeric");
  }

  // 3. Replay floor (if non-empty) must be well-formed.
  if (!install.max_version_ever_seen.empty() &&
      !split_version(install.max_version_ever_seen)) {
    return refuse(RefusalReason::MalformedReplayFloor,
                  "max_version_ever_seen '" +
                      install.max_version_ever_seen +
                      "' is not MAJOR.MINOR.PATCH numeric");
  }

  // 4. Already up-to-date?
  if (!install.current_version.empty()) {
    const auto cmp =
        compare_versions(install.current_version, m.release.version);
    // cmp is guaranteed non-null here because step 1+2 succeeded.
    if (cmp && *cmp >= 0) {
      return refuse(RefusalReason::AlreadyOnOrAheadOfOffered,
                    "current " + install.current_version +
                        " >= offered " + m.release.version);
    }
  }

  // 5+6. Freshness gate.
  if (!m.channel.expires_at.empty()) {
    const auto expires_tp = parse_rfc3339_utc(m.channel.expires_at);
    if (!expires_tp) {
      return refuse(RefusalReason::ExpiredInvalidTime,
                    "channel.expires_at '" + m.channel.expires_at +
                        "' is not in canonical RFC-3339 UTC form");
    }
    if (clock.now() >= *expires_tp) {
      return refuse(RefusalReason::Expired,
                    "channel.expires_at " + m.channel.expires_at +
                        " is in the past relative to wall clock");
    }
  }
  // Empty expires_at => freshness check is disabled; the publisher was
  // warned by the validator (ADR-0013 § "validator warnings"). Replay
  // defence (step 8) still applies.

  // 7. Minimum-previous-version floor.
  if (!m.release.min_previous_version.empty() &&
      !install.current_version.empty()) {
    const auto cmp =
        compare_versions(install.current_version,
                         m.release.min_previous_version);
    if (!cmp) {
      // The min_previous_version is malformed; the manifest validator
      // already rejected this, but defence-in-depth.
      return refuse(RefusalReason::MalformedOfferedVersion,
                    "release.min_previous_version '" +
                        m.release.min_previous_version +
                        "' is not MAJOR.MINOR.PATCH numeric");
    }
    if (*cmp < 0) {
      return refuse(RefusalReason::BelowMinPrevious,
                    "current " + install.current_version +
                        " < min_previous_version " +
                        m.release.min_previous_version);
    }
  }

  // 8. Replay-downgrade defence.
  if (!install.max_version_ever_seen.empty()) {
    const auto cmp =
        compare_versions(m.release.version,
                         install.max_version_ever_seen);
    // cmp guaranteed non-null (steps 1+3).
    if (cmp && *cmp < 0) {
      return refuse(RefusalReason::ReplayDowngrade,
                    "offered " + m.release.version +
                        " < highest-seen " +
                        install.max_version_ever_seen);
    }
  }

  // 9. Pick the artifact for this host.
  for (const auto& a : m.artifacts) {
    if (a.os == install.platform.os && a.arch == install.platform.arch) {
      return UpdateApply{m.release.version, a, m.release.mandatory};
    }
  }
  return refuse(RefusalReason::NoArtifactForPlatform,
                std::string("no artifact for ") +
                    std::string(to_string(install.platform.os)) + "/" +
                    std::string(to_string(install.platform.arch)) +
                    " in " + std::to_string(m.artifacts.size()) +
                    " manifest entries");
}

}  // namespace souxmar::update
