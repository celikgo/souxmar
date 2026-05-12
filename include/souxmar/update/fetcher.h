// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater HTTPS fetcher.
//
// Sprint 11 push 2. Closes the deferred network-fetch surface
// noted in Sprint 10 push 7's commit ("HTTPS fetcher remains
// deferred; `--artifact <path>` is the boundary"). The fetcher's
// only job is to put bytes on disk; the signature path is
// unchanged — bytes get hashed against `manifest.artifact.sha256`
// by `apply_update()` regardless of where they came from.
//
// Implementation: shell out to curl. Same pattern OllamaProvider
// (Sprint 10 push 9) validated for the "local daemon, no
// in-process HTTP client" case; here we extend it to "remote CDN"
// with the same subprocess harness. No new third-party dep.
//
// Three entry points:
//   * fetch_manifest        — GET <url>/<channel>.toml + .sig
//   * fetch_artifact        — GET <url>, stream to a temp file
//   * verify_https_url      — pre-check that a URL is https://...
//                              (the manifest validator allows http://
//                              too — useful for in-flight testing —
//                              but production downloads should be
//                              https-only; the fetcher's caller
//                              enforces this above the primitive).

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::update {

enum class FetchErrorKind : std::uint8_t {
  // Spawn failed (curl missing or fork denied).
  HttpClientFailed = 0,
  // Network-layer failure: couldn't connect, couldn't resolve,
  // timed out. The state machine's caller retries this class with
  // backoff.
  NetworkUnreachable = 1,
  // 4xx response. Permanent — retrying won't help.
  NotFound = 2,
  // 5xx response. Transient — caller may retry.
  ServerError = 3,
  // The downloaded file exceeded the size cap (defaults to 2 GiB —
  // any legitimate souxmar release is under 200 MB). Defends against
  // a runaway download.
  PayloadTooLarge = 4,
  // The URL doesn't look like the expected scheme/shape.
  BadUrl = 5,
  // I/O error writing the download to disk.
  LocalIoFailed = 6,
};

[[nodiscard]] std::string_view to_string(FetchErrorKind) noexcept;

struct FetchError {
  FetchErrorKind kind;
  std::string message;  // diagnostic only
  int http_status = 0;  // 0 if not HTTP-mediated
};

struct FetchedBytes {
  std::vector<std::uint8_t> bytes;
  std::chrono::milliseconds duration{0};
};

struct FetchedFile {
  std::filesystem::path path;  // where the bytes landed
  std::uint64_t size = 0;
  std::chrono::milliseconds duration{0};
};

using BytesResult = std::variant<FetchedBytes, FetchError>;
using FileResult = std::variant<FetchedFile, FetchError>;

struct FetcherOptions {
  std::string curl_binary;            // defaults to PATH `curl`
  std::chrono::seconds timeout{600};  // generous for slow links
  std::size_t max_bytes = 2ULL * 1024 * 1024 * 1024;
  // Whether the fetcher enforces https://. False permits http:// for
  // localhost / in-flight testing only; production code paths set
  // this true.
  bool require_https = true;
};

// Fetch a small text/binary asset to memory. Used for the manifest
// + its detached signature, both of which are < 4 KiB in practice.
[[nodiscard]] BytesResult fetch_to_memory(const std::string& url, const FetcherOptions& opts = {});

// Stream a larger asset to disk via curl's --output. Used for the
// artifact bytes (tens to hundreds of MB). The caller picks the
// destination path; the function atomically writes to <path>.tmp
// + renames on success.
[[nodiscard]] FileResult fetch_to_file(const std::string& url,
                                       const std::filesystem::path& dest,
                                       const FetcherOptions& opts = {});

// True iff `url` looks like https://host[/...] with a non-empty host.
[[nodiscard]] bool looks_like_https_url(std::string_view url) noexcept;

}  // namespace souxmar::update
