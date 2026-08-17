// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge provider_call surface smoke test.
// Sprint 14 push 4.
//
// Exercises the C ABI declared in
// include/souxmar-c-bridge/provider.h. The engine side today
// always routes to StubProvider so the test doesn't require
// credentials; Sprint 15 push 1's per-project provider lookup
// is exercised by a separate integration test once it lands.

#include "souxmar-c-bridge/pipeline.h"
#include "souxmar-c-bridge/provider.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

TEST(CBridgeProvider, AbiVersionBumpedToV2) {
  // The invariant this test owns is "the provider surface is present", which
  // landed at v2 and survives every later ratchet — so it asserts a floor,
  // not an exact value. CBridge.AbiVersionMatchesExpected owns the exact
  // lockstep check against the Rust side; pinning the exact value here too is
  // what left this test asserting 2 long after the surface reached 3.
  EXPECT_GE(souxmar_bridge_abi_version(), 2u);
}

TEST(CBridgeProvider, StubChatRequestReturnsResponse) {
  const char* json = R"({"model":"stub-model","messages":[{"role":"user","content":"hello"}]})";
  char* err = nullptr;
  auto* r = souxmar_bridge_chat_send(json, "project-1", &err);
  ASSERT_NE(r, nullptr) << "chat_send failed: " << (err ? err : "<no error>");
  EXPECT_EQ(err, nullptr);

  // The bridge programs a catch-all reply into the StubProvider before
  // dispatching (src/c-bridge/provider.cpp), so the wiring path always
  // answers. A bare StubProvider would return ProtocolMismatch here — that
  // is its documented behaviour for an unprogrammed request, and it used to
  // make this branch error on every chat message.
  EXPECT_EQ(souxmar_bridge_chat_error_kind(r), SOUXMAR_BRIDGE_PE_OK);
  EXPECT_EQ(souxmar_bridge_chat_provider(r), SOUXMAR_BRIDGE_PROVIDER_STUB);

  const char* reply = souxmar_bridge_chat_reply_text(r);
  EXPECT_NE(reply, nullptr);
  EXPECT_GT(std::strlen(reply), 0u) << "stub reply was empty";

  // Token counts are 0 on the stub (provider.h documents this).
  EXPECT_EQ(souxmar_bridge_chat_tokens_in(r), 0);
  EXPECT_EQ(souxmar_bridge_chat_tokens_out(r), 0);

  souxmar_bridge_chat_response_free(r);
}

TEST(CBridgeProvider, NullRequestJsonIsRejectedNotCrash) {
  char* err = nullptr;
  auto* r = souxmar_bridge_chat_send(nullptr, "p", &err);
  EXPECT_EQ(r, nullptr);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("NULL"), std::string::npos);
  souxmar_bridge_free_string(err);
}

TEST(CBridgeProvider, NullHandleAccessorsReturnSafeDefaults) {
  // Contract: NULL handle is safe, not UB. Mirrors pipeline.h's
  // NULL-safety: every accessor returns a sentinel for NULL.
  EXPECT_EQ(souxmar_bridge_chat_error_kind(nullptr), SOUXMAR_BRIDGE_PE_INTERNAL);
  EXPECT_EQ(souxmar_bridge_chat_provider(nullptr), SOUXMAR_BRIDGE_PROVIDER_UNKNOWN);
  EXPECT_EQ(souxmar_bridge_chat_tokens_in(nullptr), 0);
  EXPECT_EQ(souxmar_bridge_chat_tokens_out(nullptr), 0);
  // String accessors return an empty C-string (never NULL) so
  // callers can `strlen()` without a guard.
  EXPECT_STREQ(souxmar_bridge_chat_error_text(nullptr), "");
  EXPECT_STREQ(souxmar_bridge_chat_reply_text(nullptr), "");
}

TEST(CBridgeProvider, FreeNullIsSafe) {
  souxmar_bridge_chat_response_free(nullptr);
}
