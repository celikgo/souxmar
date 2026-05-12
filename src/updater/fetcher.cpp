// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater HTTPS fetcher. See include/souxmar/update/fetcher.h
// for the contract.

#include "souxmar/update/fetcher.h"

#include "souxmar/plugin/subprocess.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace souxmar::update {

namespace fs = std::filesystem;

std::string_view to_string(FetchErrorKind k) noexcept {
  switch (k) {
    case FetchErrorKind::HttpClientFailed:
      return "http-client-failed";
    case FetchErrorKind::NetworkUnreachable:
      return "network-unreachable";
    case FetchErrorKind::NotFound:
      return "not-found";
    case FetchErrorKind::ServerError:
      return "server-error";
    case FetchErrorKind::PayloadTooLarge:
      return "payload-too-large";
    case FetchErrorKind::BadUrl:
      return "bad-url";
    case FetchErrorKind::LocalIoFailed:
      return "local-io-failed";
  }
  return "unknown";
}

bool looks_like_https_url(std::string_view url) noexcept {
  if (!url.starts_with("https://"))
    return false;
  const auto rest = url.substr(8);
  if (rest.empty())
    return false;
  // Reject "https:///path".
  if (rest.front() == '/')
    return false;
  return true;
}

namespace {

FetchError client_failed(std::string msg) {
  return FetchError{FetchErrorKind::HttpClientFailed, std::move(msg), 0};
}

FetchError network_unreachable(std::string msg, int exit_code) {
  FetchError e;
  e.kind = FetchErrorKind::NetworkUnreachable;
  e.message = std::move(msg) + " (curl exit " + std::to_string(exit_code) + ")";
  e.http_status = 0;
  return e;
}

// Parse the trailing "http_code" curl writes when --write-out
// "%{http_code}" is the last byte of its captured output. The
// fetcher passes that flag so we can distinguish 4xx (NotFound /
// BadUrl) from 5xx (ServerError) without a second HTTP HEAD.
int parse_trailing_http_code(std::string& body_buf) {
  if (body_buf.empty())
    return 0;
  // The status code we asked curl to emit is exactly 3 ASCII
  // digits (always — even 0 for offline => "000"). Strip them from
  // the body so the caller sees only the actual payload.
  if (body_buf.size() < 3)
    return 0;
  std::size_t at = body_buf.size() - 3;
  for (std::size_t i = at; i < body_buf.size(); ++i) {
    const char c = body_buf[i];
    if (c < '0' || c > '9')
      return 0;
  }
  const int code =
      (body_buf[at] - '0') * 100 + (body_buf[at + 1] - '0') * 10 + (body_buf[at + 2] - '0');
  body_buf.resize(at);
  return code;
}

// Categorise curl exit codes. We mirror notable categories rather
// than enumerate the whole table:
//   * 0  = ok (HTTP status decides further)
//   * 6  = couldn't resolve host
//   * 7  = couldn't connect
//   * 22 = HTTP returned >= 400 (with --fail-with-body)
//   * 28 = timeout
//   * 60 = ssl cert problem (mapped to BadUrl to surface CA issues)
//   * 63 = file size exceeded --max-filesize
struct CurlOutcome {
  bool spawn_ok;
  bool timed_out;
  int exit_code;
  std::string stdout_bytes;
  std::string stderr_bytes;
};

CurlOutcome run_curl(std::vector<std::string> argv,
                     std::chrono::seconds timeout,
                     std::size_t cap_bytes) {
  plugin::SubprocessOptions o;
  o.argv = std::move(argv);
  o.timeout = std::chrono::milliseconds(timeout.count() * 1000);
  o.max_capture_bytes = cap_bytes;
  auto r = plugin::run_subprocess(o);
  return CurlOutcome{
      r.ok, r.timed_out, r.exit_code, std::move(r.stdout_bytes), std::move(r.stderr_bytes)};
}

}  // namespace

BytesResult fetch_to_memory(const std::string& url, const FetcherOptions& opts) {
  if (opts.require_https && !looks_like_https_url(url)) {
    return FetchError{FetchErrorKind::BadUrl, "fetcher: refusing non-https URL '" + url + "'", 0};
  }

  const std::string curl = opts.curl_binary.empty() ? "curl" : opts.curl_binary;
  std::vector<std::string> argv = {
      curl,
      "--silent",
      "--show-error",
      "--location",  // follow redirects
      "--max-time",
      std::to_string(opts.timeout.count()),
      "--max-filesize",
      std::to_string(opts.max_bytes),
      "--fail-with-body",  // 4xx/5xx => non-zero exit + body
      "--write-out",
      "%{http_code}",
      url,
  };

  const auto started = std::chrono::steady_clock::now();
  auto r = run_curl(std::move(argv), opts.timeout, opts.max_bytes + 16);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (!r.spawn_ok) {
    return client_failed("curl spawn failed: " + r.stderr_bytes);
  }
  if (r.timed_out) {
    return FetchError{FetchErrorKind::NetworkUnreachable,
                      "curl timed out after " + std::to_string(opts.timeout.count()) + "s",
                      0};
  }

  // Pull the http_code prefix off the body. On a non-HTTP failure
  // (couldn't resolve, couldn't connect), curl writes "000".
  std::string body = std::move(r.stdout_bytes);
  const int http_code = parse_trailing_http_code(body);

  switch (r.exit_code) {
    case 0:
      // Success at the curl layer; status code decides further.
      if (http_code >= 200 && http_code < 300) {
        FetchedBytes b;
        b.bytes.assign(body.begin(), body.end());
        b.duration = elapsed;
        return b;
      }
      if (http_code >= 400 && http_code < 500) {
        return FetchError{FetchErrorKind::NotFound, "HTTP " + std::to_string(http_code), http_code};
      }
      if (http_code >= 500) {
        return FetchError{
            FetchErrorKind::ServerError, "HTTP " + std::to_string(http_code), http_code};
      }
      return FetchError{
          FetchErrorKind::ServerError, "unexpected HTTP " + std::to_string(http_code), http_code};
    case 6:
    case 7:
      return network_unreachable("curl could not reach " + url, r.exit_code);
    case 22:
      // --fail-with-body fired; the body carries the error and the
      // http_code is one of 4xx/5xx — fold via the http_code arm.
      if (http_code >= 400 && http_code < 500) {
        return FetchError{FetchErrorKind::NotFound, "HTTP " + std::to_string(http_code), http_code};
      }
      return FetchError{
          FetchErrorKind::ServerError, "HTTP " + std::to_string(http_code), http_code};
    case 28:
      return FetchError{FetchErrorKind::NetworkUnreachable, "curl operation timeout (exit 28)", 0};
    case 60:
      return FetchError{FetchErrorKind::BadUrl, "curl SSL cert problem: " + r.stderr_bytes, 0};
    case 63:
      return FetchError{FetchErrorKind::PayloadTooLarge,
                        "response exceeded max_bytes (" + std::to_string(opts.max_bytes) + ")",
                        0};
    default:
      return FetchError{FetchErrorKind::HttpClientFailed,
                        "curl exit " + std::to_string(r.exit_code) + ": " + r.stderr_bytes,
                        0};
  }
}

FileResult fetch_to_file(const std::string& url, const fs::path& dest, const FetcherOptions& opts) {
  if (opts.require_https && !looks_like_https_url(url)) {
    return FetchError{FetchErrorKind::BadUrl, "fetcher: refusing non-https URL '" + url + "'", 0};
  }

  std::error_code ec;
  if (dest.has_parent_path()) {
    fs::create_directories(dest.parent_path(), ec);
    if (ec && !fs::is_directory(dest.parent_path())) {
      return FetchError{FetchErrorKind::LocalIoFailed,
                        "cannot create dest directory " + dest.parent_path().string(),
                        0};
    }
  }

  thread_local std::mt19937_64 rng{std::random_device{}()};
  const fs::path tmp = dest.string() + ".tmp." + std::to_string(rng());

  const std::string curl = opts.curl_binary.empty() ? "curl" : opts.curl_binary;
  std::vector<std::string> argv = {
      curl,
      "--silent",
      "--show-error",
      "--location",
      "--max-time",
      std::to_string(opts.timeout.count()),
      "--max-filesize",
      std::to_string(opts.max_bytes),
      "--fail-with-body",
      "--output",
      tmp.string(),
      url,
  };

  const auto started = std::chrono::steady_clock::now();
  auto r = run_curl(std::move(argv), opts.timeout, 4096);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (!r.spawn_ok) {
    fs::remove(tmp, ec);
    return client_failed("curl spawn failed: " + r.stderr_bytes);
  }
  if (r.timed_out) {
    fs::remove(tmp, ec);
    return FetchError{FetchErrorKind::NetworkUnreachable,
                      "curl timed out after " + std::to_string(opts.timeout.count()) + "s",
                      0};
  }

  switch (r.exit_code) {
    case 0: {
      const auto size = fs::file_size(tmp, ec);
      if (ec || size == 0) {
        fs::remove(tmp, ec);
        return FetchError{FetchErrorKind::LocalIoFailed, "downloaded file is empty", 0};
      }
      fs::rename(tmp, dest, ec);
      if (ec) {
        fs::copy_file(tmp, dest, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) {
          return FetchError{FetchErrorKind::LocalIoFailed, "rename/copy to dest failed", 0};
        }
      }
      FetchedFile out;
      out.path = dest;
      out.size = size;
      out.duration = elapsed;
      return out;
    }
    case 6:
    case 7:
      fs::remove(tmp, ec);
      return network_unreachable("curl could not reach " + url, r.exit_code);
    case 22:
      fs::remove(tmp, ec);
      return FetchError{FetchErrorKind::NotFound, "HTTP 4xx from " + url, 0};
    case 28:
      fs::remove(tmp, ec);
      return FetchError{FetchErrorKind::NetworkUnreachable, "curl operation timeout (exit 28)", 0};
    case 60:
      fs::remove(tmp, ec);
      return FetchError{FetchErrorKind::BadUrl, "curl SSL cert problem: " + r.stderr_bytes, 0};
    case 63:
      fs::remove(tmp, ec);
      return FetchError{FetchErrorKind::PayloadTooLarge,
                        "response exceeded max_bytes (" + std::to_string(opts.max_bytes) + ")",
                        0};
    default:
      fs::remove(tmp, ec);
      return FetchError{FetchErrorKind::HttpClientFailed,
                        "curl exit " + std::to_string(r.exit_code) + ": " + r.stderr_bytes,
                        0};
  }
}

}  // namespace souxmar::update
