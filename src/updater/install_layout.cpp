// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater install-root layout. See
// include/souxmar/update/install_layout.h for the on-disk shape and
// atomicity guarantees.

#include "souxmar/update/install_layout.h"

#include "souxmar/crypto/primitives.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace souxmar::update {

namespace fs = std::filesystem;

namespace {

// One-line text-file read with leading/trailing ASCII whitespace
// stripped. Used for current.txt / previous.txt.
std::string read_one_line(const fs::path& p) {
  std::ifstream src(p, std::ios::binary);
  if (!src.is_open())
    return {};
  std::ostringstream buf;
  buf << src.rdbuf();
  std::string s = buf.str();
  std::size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r'))
    ++b;
  std::size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r'))
    --e;
  return s.substr(b, e - b);
}

// Write a one-line text file atomically: write to <p>.tmp.<rand>,
// fsync, rename. Returns false on any I/O step.
bool write_marker_atomic(const fs::path& p, const std::string& content) {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  const fs::path tmp = p.string() + ".tmp." + std::to_string(rng());
  {
    std::ofstream sink(tmp, std::ios::binary | std::ios::trunc);
    if (!sink.is_open())
      return false;
    sink << content << '\n';
    if (!sink.good()) {
      sink.close();
      std::error_code ec;
      fs::remove(tmp, ec);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(tmp, p, ec);
  if (ec) {
    fs::copy_file(tmp, p, fs::copy_options::overwrite_existing, ec);
    fs::remove(tmp, ec);
    if (ec)
      return false;
  }
  return true;
}

bool write_payload_atomic(const fs::path& final_dir,
                          const fs::path& staging_dir,
                          std::span<const std::uint8_t> bytes) {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  std::error_code ec;
  fs::create_directories(staging_dir, ec);
  if (ec && !fs::is_directory(staging_dir))
    return false;

  const fs::path scratch =
      staging_dir / (final_dir.filename().string() + "." + std::to_string(rng()));
  fs::create_directories(scratch, ec);
  if (ec)
    return false;

  const fs::path scratch_payload = scratch / "payload";
  {
    std::ofstream sink(scratch_payload, std::ios::binary | std::ios::trunc);
    if (!sink.is_open()) {
      fs::remove_all(scratch, ec);
      return false;
    }
    if (!bytes.empty()) {
      sink.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    }
    if (!sink.good()) {
      sink.close();
      fs::remove_all(scratch, ec);
      return false;
    }
  }

  fs::create_directories(final_dir.parent_path(), ec);
  // Move into place. If the final_dir already exists (partial leftover
  // from a previous attempt), remove it first — this is the GC story
  // converging in real time, not a destructive override of a current
  // install (current install lives under a *different* version dir,
  // and `gc_unreferenced` protects current + previous).
  if (fs::exists(final_dir, ec)) {
    fs::remove_all(final_dir, ec);
    if (ec) {
      fs::remove_all(scratch, ec);
      return false;
    }
  }
  fs::rename(scratch, final_dir, ec);
  if (ec) {
    fs::remove_all(scratch, ec);
    return false;
  }
  return true;
}

}  // namespace

// ============================================================================
// InstallLayout
// ============================================================================

InstallLayout::InstallLayout(fs::path target_root) : root_(std::move(target_root)) {}

fs::path InstallLayout::current_marker_path() const {
  return root_ / "current.txt";
}

fs::path InstallLayout::previous_marker_path() const {
  return root_ / "previous.txt";
}

fs::path InstallLayout::versions_dir() const {
  return root_ / "versions";
}

fs::path InstallLayout::staging_dir() const {
  return root_ / "staging";
}

fs::path InstallLayout::payload_path(const std::string& v) const {
  return versions_dir() / v / "payload";
}

fs::path InstallLayout::rollback_log_path() const {
  return root_ / "rollback.log";
}

std::string InstallLayout::read_current_version() const {
  return read_one_line(current_marker_path());
}

std::string InstallLayout::read_previous_version() const {
  return read_one_line(previous_marker_path());
}

bool InstallLayout::has_current() const {
  return !read_current_version().empty();
}

bool InstallLayout::has_previous() const {
  return !read_previous_version().empty();
}

std::vector<std::string> InstallLayout::list_versions() const {
  std::vector<std::string> out;
  std::error_code ec;
  if (!fs::is_directory(versions_dir(), ec))
    return out;
  for (const auto& entry : fs::directory_iterator(versions_dir(), ec)) {
    if (entry.is_directory()) {
      out.push_back(entry.path().filename().string());
    }
  }
  return out;
}

bool InstallLayout::has_version_payload(const std::string& v) const {
  std::error_code ec;
  const auto p = payload_path(v);
  if (!fs::exists(p, ec))
    return false;
  return fs::file_size(p, ec) > 0;
}

bool InstallLayout::stage_version(const std::string& version,
                                  std::span<const std::uint8_t> payload) {
  if (version.empty())
    return false;
  return write_payload_atomic(versions_dir() / version, staging_dir(), payload);
}

bool InstallLayout::atomic_switch_to(const std::string& from_version,
                                     const std::string& to_version) {
  if (to_version.empty())
    return false;
  if (!has_version_payload(to_version))
    return false;
  // Write previous.txt first — if anything goes wrong before
  // current.txt updates, the previous-pointer is stale but the
  // current-pointer is unchanged, which is the safe direction.
  if (!from_version.empty()) {
    if (!write_marker_atomic(previous_marker_path(), from_version)) {
      return false;
    }
  }
  return write_marker_atomic(current_marker_path(), to_version);
}

bool InstallLayout::remove_version(const std::string& version) {
  if (version.empty())
    return false;
  if (version == read_current_version())
    return false;
  if (version == read_previous_version())
    return false;
  std::error_code ec;
  fs::remove_all(versions_dir() / version, ec);
  return !ec;
}

std::vector<std::string> InstallLayout::gc_unreferenced(
    std::span<const std::string> protect_versions) {
  std::vector<std::string> reaped;
  const auto current = read_current_version();
  const auto previous = read_previous_version();
  for (const auto& v : list_versions()) {
    if (v == current || v == previous)
      continue;
    if (std::find(protect_versions.begin(), protect_versions.end(), v) != protect_versions.end()) {
      continue;
    }
    if (remove_version(v)) {
      reaped.push_back(v);
    }
  }
  return reaped;
}

// ============================================================================
// sha256_hex — forwarder to libsouxmar-crypto (ADR-0015).
// ============================================================================

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
  return crypto::sha256_hex(bytes);
}

}  // namespace souxmar::update
