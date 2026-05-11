// SPDX-License-Identifier: Apache-2.0

#include "souxmar/ai/audit_log.h"

#include <fmt/core.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace souxmar::ai {

namespace fs = std::filesystem;

// ============================================================================
// SessionBudget
// ============================================================================

namespace {

// Threshold table: index → percentage. Matches the bitset layout in the
// header (input.50, input.80, input.100, output…, total…). Keeping the
// layout explicit avoids the "what does fired_thresholds_ bit 3 mean"
// future-archaeology question.
constexpr std::array<int, 3> kThresholds = {50, 80, 100};
constexpr std::array<const char*, 3> kAxes = {"input", "output", "total"};

bool over_threshold(std::size_t consumed, std::size_t max_v, int pct) noexcept {
  if (max_v == 0) return false;
  // Avoid floating point in the hot path: consumed * 100 >= max * pct
  return consumed * 100 >= max_v * static_cast<std::size_t>(pct);
}

}  // namespace

std::size_t SessionBudget::record(std::size_t input_delta,
                                  std::size_t output_delta) {
  consumed_input  += input_delta;
  consumed_output += output_delta;
  const auto total = consumed_total();

  // Fire on_threshold for every newly-crossed (axis, threshold) pair.
  // The bitset compresses 3 axes × 3 thresholds into 9 bits — the order
  // is the iteration order below, MSB unused.
  std::size_t bit = 0;
  for (std::size_t axis_i = 0; axis_i < kAxes.size(); ++axis_i) {
    const std::size_t consumed = (axis_i == 0) ? consumed_input
                                 : (axis_i == 1) ? consumed_output
                                                 : total;
    const std::size_t max_v    = (axis_i == 0) ? max_input_tokens
                                 : (axis_i == 1) ? max_output_tokens
                                                 : max_total_tokens;
    for (std::size_t t_i = 0; t_i < kThresholds.size(); ++t_i, ++bit) {
      const std::uint16_t mask = static_cast<std::uint16_t>(1u << bit);
      if ((fired_thresholds_ & mask) != 0) continue;
      if (!over_threshold(consumed, max_v, kThresholds[t_i])) continue;
      fired_thresholds_ |= mask;
      if (on_threshold) on_threshold(kThresholds[t_i], kAxes[axis_i], *this);
    }
  }
  return total;
}

// ============================================================================
// AuditLog
// ============================================================================

struct AuditLog::Impl {
  fs::path       path;
  std::ofstream  stream;
  std::mutex     mu;
};

AuditLog::AuditLog(fs::path path) : impl_(std::make_unique<Impl>()) {
  impl_->path = std::move(path);
  std::error_code ec;
  if (impl_->path.has_parent_path()) {
    fs::create_directories(impl_->path.parent_path(), ec);
    if (ec && !fs::is_directory(impl_->path.parent_path())) {
      throw fs::filesystem_error("souxmar AuditLog: cannot create directory",
                                 impl_->path.parent_path(), ec);
    }
  }
  // Append mode; one process per file is the common case but multiple
  // are tolerated (POSIX O_APPEND atomic-up-to-PIPE_BUF semantics).
  impl_->stream.open(impl_->path, std::ios::out | std::ios::app);
  if (!impl_->stream.is_open()) {
    throw fs::filesystem_error("souxmar AuditLog: cannot open file for append",
                               impl_->path,
                               std::make_error_code(std::errc::permission_denied));
  }
}

AuditLog::~AuditLog() = default;
AuditLog::AuditLog(AuditLog&&) noexcept = default;
AuditLog& AuditLog::operator=(AuditLog&&) noexcept = default;

const fs::path& AuditLog::path() const noexcept {
  return impl_->path;
}

namespace {

// ISO-8601 UTC timestamp with seconds precision: "2026-05-11T12:34:56Z".
// Stable format avoids locale-dependent surprises in the audit log.
std::string iso_now() {
  const auto now = std::chrono::system_clock::now();
  const auto t   = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &t);
#else
  gmtime_r(&t, &utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

// Quote a value safely for our YAML one-liner. The audit format is a
// YAML flow-style mapping; we double-quote strings containing anything
// outside [A-Za-z0-9_./-:] to be safe.
std::string quote_yaml(std::string_view s) {
  bool plain = !s.empty();
  for (char c : s) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
          c == '/' || c == '-' || c == ':' || c == '+')) {
      plain = false;
      break;
    }
  }
  if (plain) return std::string(s);
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  for (char c : s) {
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  out += '"';
  return out;
}

}  // namespace

void AuditLog::append(const Entry& entry) {
  std::scoped_lock lk(impl_->mu);
  if (!impl_->stream.is_open()) return;  // best-effort: silently drop

  std::ostringstream os;
  os << "{ts: "       << iso_now()
     << ", tool: "    << quote_yaml(entry.tool_name)
     << ", outcome: " << quote_yaml(entry.outcome)
     << ", duration_ms: " << entry.duration.count()
     << ", input_hash: "  << quote_yaml(entry.input_hash);
  if (entry.budget != nullptr) {
    os << ", budget: {in: "  << entry.budget->consumed_input
       << ", out: "          << entry.budget->consumed_output
       << ", total: "        << entry.budget->consumed_total()
       << ", max_total: "    << entry.budget->max_total_tokens
       << "}";
  }
  if (!entry.summary.empty()) {
    os << ", summary: " << quote_yaml(entry.summary);
  }
  os << "}\n";

  const auto line = os.str();
  impl_->stream.write(line.data(), static_cast<std::streamsize>(line.size()));
  impl_->stream.flush();
}

fs::path AuditLog::default_path(const fs::path& project_root) {
  // Allow operators to redirect via env var without code changes.
  if (const char* v = std::getenv("SOUXMAR_AUDIT_LOG"); v && *v) {
    return fs::path{v};
  }
  fs::path base = project_root.empty()
                      ? fs::current_path()
                      : project_root;
  return base / ".souxmar" / "chat" / "audit.log";
}

}  // namespace souxmar::ai
