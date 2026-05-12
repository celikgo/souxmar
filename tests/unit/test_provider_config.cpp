// SPDX-License-Identifier: Apache-2.0
//
// provider_config loader tests. Sprint 15 push 2 (ADR-0020).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <variant>

#include "souxmar/ai/provider_config.h"

namespace fs = std::filesystem;
using souxmar::ai::load_provider_config;
using souxmar::ai::ProviderConfig;
using souxmar::ai::ProviderConfigError;
using souxmar::ai::ProviderConfigErrorKind;
using souxmar::ai::ProviderKind;

namespace {

fs::path scratch_dir(std::string_view tag) {
  std::random_device rd;
  fs::path dir = fs::temp_directory_path() /
                 ("souxmar-pc-" + std::string(tag) + "-" +
                  std::to_string(rd()));
  fs::create_directories(dir);
  return dir;
}

void write_toml(const fs::path& dir, std::string_view content) {
  std::ofstream f(dir / "project.ai.toml");
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace

TEST(ProviderConfig, AbsentFileReturnsNotFound) {
  auto dir = scratch_dir("absent");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind,
            ProviderConfigErrorKind::NotFound);
  fs::remove_all(dir);
}

TEST(ProviderConfig, MissingSchemaIsRejected) {
  auto dir = scratch_dir("no-schema");
  write_toml(dir, R"(
provider = "ollama"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind,
            ProviderConfigErrorKind::SchemaMismatch);
  fs::remove_all(dir);
}

TEST(ProviderConfig, ValidOllamaConfig) {
  auto dir = scratch_dir("ollama");
  write_toml(dir, R"(
schema = 1
provider = "ollama"
model = "llama-3.1:8b"

[ollama]
endpoint = "http://localhost:11434"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r));
  const auto& cfg = std::get<ProviderConfig>(r);
  EXPECT_EQ(cfg.provider, ProviderKind::Ollama);
  EXPECT_EQ(cfg.model,    "llama-3.1:8b");
  EXPECT_EQ(cfg.endpoint, "http://localhost:11434");
  fs::remove_all(dir);
}

TEST(ProviderConfig, BYOKAnthropicWithoutModelRejected) {
  auto dir = scratch_dir("anthropic-no-model");
  write_toml(dir, R"(
schema = 1
provider = "byok-anthropic"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind,
            ProviderConfigErrorKind::MissingField);
  fs::remove_all(dir);
}

TEST(ProviderConfig, UnknownProviderRejected) {
  auto dir = scratch_dir("unknown");
  write_toml(dir, R"(
schema = 1
provider = "groq"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind,
            ProviderConfigErrorKind::ProviderUnknown);
  fs::remove_all(dir);
}

TEST(ProviderConfig, MalformedTomlIsTypedError) {
  auto dir = scratch_dir("malformed");
  write_toml(dir, "schema = 1\nprovider = \"ollama\nmodel = ");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind,
            ProviderConfigErrorKind::MalformedToml);
  fs::remove_all(dir);
}
