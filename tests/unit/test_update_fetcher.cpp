// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 2 — unit tests for the HTTPS fetcher. We don't go
// over the network here; the tests cover the URL-shape guard +
// exit-code categorisation paths that don't need a server. The
// happy-path "real HTTPS fetch lands bytes" coverage lives in the
// release smoke tests run against a staging CDN.

#include "souxmar/update/fetcher.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

using namespace souxmar::update;

TEST(Fetcher, HttpsUrlShape) {
  EXPECT_TRUE(looks_like_https_url("https://dl.souxmar.dev/0.9.0/manifest.toml"));
  EXPECT_TRUE(looks_like_https_url("https://host:8443/"));
  EXPECT_FALSE(looks_like_https_url("http://dl.souxmar.dev/x"));
  EXPECT_FALSE(looks_like_https_url("file:///etc/passwd"));
  EXPECT_FALSE(looks_like_https_url("https://"));
  EXPECT_FALSE(looks_like_https_url("https:///path"));
  EXPECT_FALSE(looks_like_https_url(""));
}

TEST(Fetcher, RefusesNonHttpsByDefault) {
  // No network call should happen — the URL-shape guard fires before
  // curl is spawned.
  auto r = fetch_to_memory("http://dl.example.com/x");
  ASSERT_TRUE(std::holds_alternative<FetchError>(r));
  EXPECT_EQ(std::get<FetchError>(r).kind, FetchErrorKind::BadUrl);
}

TEST(Fetcher, AcceptsInsecureWhenExplicitlyAllowed) {
  // require_https=false lets the URL through; the call will then
  // fail at the network layer (couldn't connect to example.com on
  // a CI runner without internet), which is *also* a fine outcome
  // for this test — both branches confirm the URL guard did NOT
  // fire.
  FetcherOptions opts;
  opts.require_https = false;
  opts.timeout       = std::chrono::seconds(1);  // fast-fail
  auto r = fetch_to_memory("http://127.0.0.1:1/no-server", opts);
  ASSERT_TRUE(std::holds_alternative<FetchError>(r));
  const auto& err = std::get<FetchError>(r);
  EXPECT_NE(err.kind, FetchErrorKind::BadUrl)
      << "URL guard must not fire when require_https=false";
}

TEST(Fetcher, ErrorKindStringRoundtrip) {
  EXPECT_EQ(to_string(FetchErrorKind::HttpClientFailed),   "http-client-failed");
  EXPECT_EQ(to_string(FetchErrorKind::NetworkUnreachable), "network-unreachable");
  EXPECT_EQ(to_string(FetchErrorKind::NotFound),           "not-found");
  EXPECT_EQ(to_string(FetchErrorKind::ServerError),        "server-error");
  EXPECT_EQ(to_string(FetchErrorKind::PayloadTooLarge),    "payload-too-large");
  EXPECT_EQ(to_string(FetchErrorKind::BadUrl),             "bad-url");
  EXPECT_EQ(to_string(FetchErrorKind::LocalIoFailed),      "local-io-failed");
}
