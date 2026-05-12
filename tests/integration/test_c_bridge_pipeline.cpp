// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge smoke test — Sprint 13 push 3.
//
// Exercises the C ABI declared in include/souxmar-c-bridge/pipeline.h
// from C++ (the easiest way to drive it without a Rust test
// harness — the Rust side has its own cargo-test smoke). This is
// the regression net for the bridge surface; any change to the C
// signature lands a corresponding test edit here so the contract
// stays nameable.

#include "souxmar-c-bridge/pipeline.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

const char* kCantileverYaml =
    "version: 1\n"
    "stages:\n"
    "  - id: mesh\n"
    "    plugin: mesher.tetra.hello\n"
    "    input:\n"
    "      target_size: 0.05\n"
    "      element_order: 1\n"
    "  - id: write\n"
    "    plugin: writer.vtu\n"
    "    input:\n"
    "      mesh: { from: mesh }\n"
    "      path: cantilever.vtu\n";

}  // namespace

TEST(CBridge, AbiVersionMatchesExpected) {
  // Aligned with EXPECTED_ABI_VERSION on the Rust side. When the
  // surface ratchets, both sides bump together.
  EXPECT_EQ(souxmar_bridge_abi_version(), 1u);
}

TEST(CBridge, ParseCantileverReturnsTwoStages) {
  char* err = nullptr;
  auto* p = souxmar_bridge_pipeline_parse(kCantileverYaml, &err);
  ASSERT_NE(p, nullptr) << "parse failed: " << (err ? err : "<no error message>");
  EXPECT_EQ(err, nullptr);

  EXPECT_EQ(souxmar_bridge_pipeline_stage_count(p), 2u);

  const char* id = nullptr;
  const char* plugin = nullptr;

  EXPECT_EQ(souxmar_bridge_pipeline_stage_at(p, 0, &id, &plugin), 0);
  EXPECT_STREQ(id, "mesh");
  EXPECT_STREQ(plugin, "mesher.tetra.hello");

  EXPECT_EQ(souxmar_bridge_pipeline_stage_at(p, 1, &id, &plugin), 0);
  EXPECT_STREQ(id, "write");
  EXPECT_STREQ(plugin, "writer.vtu");

  // Out-of-range index is rejected, not undefined behaviour.
  EXPECT_EQ(souxmar_bridge_pipeline_stage_at(p, 5, &id, &plugin), -1);

  souxmar_bridge_pipeline_free(p);
}

TEST(CBridge, ParseGarbageReportsError) {
  char* err = nullptr;
  auto* p = souxmar_bridge_pipeline_parse("this is not valid yaml: {[\n", &err);
  EXPECT_EQ(p, nullptr);
  ASSERT_NE(err, nullptr) << "parse error must populate out_err";
  // The error message is the parser's — don't pin its exact text,
  // just confirm it's non-empty.
  EXPECT_GT(std::strlen(err), 0u);
  souxmar_bridge_free_string(err);
}

TEST(CBridge, NullYamlIsRejectedNotCrash) {
  char* err = nullptr;
  auto* p = souxmar_bridge_pipeline_parse(nullptr, &err);
  EXPECT_EQ(p, nullptr);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("NULL"), std::string::npos);
  souxmar_bridge_free_string(err);
}

TEST(CBridge, NullHandleStageCountReturnsZero) {
  // Contract: NULL handle is safe, not UB. The Rust side's
  // wrapper doesn't pass NULL but defensive C consumers might.
  EXPECT_EQ(souxmar_bridge_pipeline_stage_count(nullptr), 0u);

  const char* id = nullptr;
  const char* plugin = nullptr;
  EXPECT_EQ(souxmar_bridge_pipeline_stage_at(nullptr, 0, &id, &plugin), -1);
}

TEST(CBridge, FreeNullIsSafe) {
  // Contract: souxmar_bridge_pipeline_free(NULL) is a no-op.
  // Without this, RAII-style wrappers in Rust would need extra
  // null guards.
  souxmar_bridge_pipeline_free(nullptr);
  souxmar_bridge_free_string(nullptr);
}
