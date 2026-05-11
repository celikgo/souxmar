// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 8 — unit tests for the build-time embedded trust
// store. We can't assert anything cryptographically meaningful about
// the embedded key (its bytes are configured at build time and the
// release pipeline overrides them per release), but we can assert
// the *shape*: a non-empty trust store with a 32-byte key, and a
// stable build_uses_dev_key() bit that release CI greps for.

#include "souxmar/update/embedded_trust.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace souxmar::update;

TEST(EmbeddedTrust, StoreHasExactlyOneKey) {
  const auto ts = embedded_trust_store();
  EXPECT_EQ(ts.size(), 1u);
}

TEST(EmbeddedTrust, KeyIdIsNonEmpty) {
  const auto ts = embedded_trust_store();
  ASSERT_EQ(ts.keys().size(), 1u);
  EXPECT_FALSE(ts.keys()[0].public_key_id.empty());
}

TEST(EmbeddedTrust, PublicKeyHasEd25519Length) {
  const auto ts = embedded_trust_store();
  ASSERT_EQ(ts.keys().size(), 1u);
  EXPECT_EQ(ts.keys()[0].public_key.size(), kEd25519PublicKeyBytes);
}

TEST(EmbeddedTrust, DevKeyIsDistinguishable) {
  // Default builds carry "souxmar-dev-key" — release builds must
  // override this *or* a release CI gate kicks in. The unit test
  // suite is built without that override, so build_uses_dev_key()
  // is expected to be true here. The assertion below isn't strict
  // equality: it's "*if* the build-time id matches kDevKeyId, then
  // build_uses_dev_key returns true" — which holds in either
  // configuration.
  if (std::string_view{kEmbeddedReleaseKeyId} == "souxmar-dev-key") {
    EXPECT_TRUE(build_uses_dev_key());
  } else {
    EXPECT_FALSE(build_uses_dev_key());
  }
}

TEST(EmbeddedTrust, FindReturnsTheConfiguredKey) {
  const auto ts = embedded_trust_store();
  ASSERT_EQ(ts.size(), 1u);
  const auto& id = ts.keys()[0].public_key_id;
  const auto* k = ts.find(id);
  ASSERT_NE(k, nullptr);
  EXPECT_EQ(k->public_key_id, id);
}
