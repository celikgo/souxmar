// SPDX-License-Identifier: Apache-2.0
//
// provider_config loader tests. Sprint 15 push 2 (ADR-0020).

#include "souxmar/ai/provider.h"  // openai_compatible_presets()
#include "souxmar/ai/provider_config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <variant>

namespace fs = std::filesystem;
using souxmar::ai::load_provider_config;
using souxmar::ai::ProviderConfig;
using souxmar::ai::ProviderConfigError;
using souxmar::ai::ProviderConfigErrorKind;
using souxmar::ai::ProviderKind;

namespace {

fs::path scratch_dir(std::string_view tag) {
  std::random_device rd;
  fs::path dir =
      fs::temp_directory_path() / ("souxmar-pc-" + std::string(tag) + "-" + std::to_string(rd()));
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
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::NotFound);
  fs::remove_all(dir);
}

TEST(ProviderConfig, MissingSchemaIsRejected) {
  auto dir = scratch_dir("no-schema");
  write_toml(dir, R"(
provider = "ollama"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::SchemaMismatch);
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
  EXPECT_EQ(cfg.model, "llama-3.1:8b");
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
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MissingField);
  fs::remove_all(dir);
}

TEST(ProviderConfig, UnknownProviderRejected) {
  auto dir = scratch_dir("unknown");
  // Was "groq" until that became a real preset. Use a name no service
  // will ever claim, so this test keeps testing rejection rather than
  // quietly becoming a test of the newest provider.
  write_toml(dir, R"(
schema = 1
provider = "definitely-not-a-provider"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::ProviderUnknown);
  fs::remove_all(dir);
}

TEST(ProviderConfig, MalformedTomlIsTypedError) {
  auto dir = scratch_dir("malformed");
  write_toml(dir, "schema = 1\nprovider = \"ollama\nmodel = ");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MalformedToml);
  fs::remove_all(dir);
}

// ---- OpenAI-compatible providers ---------------------------------------
//
// The point of this kind is that connecting a service is configuration,
// not code. These tests pin that: a preset name resolves without a
// base_url, an unknown service works with one, and a key in the file is
// refused outright.

TEST(ProviderConfig, PresetResolvesBaseUrlAndKeyEnv) {
  auto dir = scratch_dir("grok");
  write_toml(dir, R"(
schema = 1
provider = "grok"
model = "grok-test-model"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r)) << "grok should be a known preset";
  const auto& c = std::get<ProviderConfig>(r);
  EXPECT_EQ(c.provider, ProviderKind::OpenAICompatible);
  EXPECT_EQ(c.provider_id, "grok");
  EXPECT_EQ(c.model, "grok-test-model");
  EXPECT_EQ(c.base_url, "https://api.x.ai/v1");
  EXPECT_EQ(c.api_key_env, "XAI_API_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, EveryPresetResolvesOrDemandsABaseUrl) {
  // A preset with neither a built-in base_url nor a documented override
  // would be a dead entry in the UI dropdown.
  for (const auto& preset : souxmar::ai::openai_compatible_presets()) {
    auto dir = scratch_dir("preset");
    write_toml(dir, "schema = 1\nprovider = \"" + std::string(preset.id) + "\"\nmodel = \"m\"\n");
    auto r = load_provider_config(dir);
    if (preset.base_url.empty()) {
      // The escape hatch: must ask for a base_url rather than silently
      // producing a provider that cannot reach anything.
      ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r)) << preset.id;
      EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MissingField)
          << preset.id;
    } else {
      ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r)) << preset.id;
      const auto& c = std::get<ProviderConfig>(r);
      EXPECT_EQ(c.base_url, preset.base_url) << preset.id;
      EXPECT_FALSE(c.api_key_env.empty()) << preset.id;
      // Presets must not carry a trailing slash — the provider appends
      // "/chat/completions" and "//" breaks some gateways.
      EXPECT_NE(c.base_url.back(), '/') << preset.id;
    }
    fs::remove_all(dir);
  }
}

TEST(ProviderConfig, ExplicitBaseUrlOverridesPreset) {
  auto dir = scratch_dir("override");
  write_toml(dir, R"(
schema = 1
provider = "grok"
model = "m"

[openai_compatible]
base_url = "http://localhost:1234/v1"
api_key_env = "MY_OWN_KEY"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r));
  const auto& c = std::get<ProviderConfig>(r);
  EXPECT_EQ(c.base_url, "http://localhost:1234/v1");
  EXPECT_EQ(c.api_key_env, "MY_OWN_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, SelfHostedNeedsBaseUrl) {
  auto dir = scratch_dir("selfhosted");
  write_toml(dir, R"(
schema = 1
provider = "openai-compatible"
model = "local-model"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MissingField);
  fs::remove_all(dir);
}

TEST(ProviderConfig, OpenAICompatibleRequiresModel) {
  auto dir = scratch_dir("nomodel");
  write_toml(dir, R"(
schema = 1
provider = "deepseek"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MissingField);
  fs::remove_all(dir);
}

TEST(ProviderConfig, InlineApiKeyIsRefused) {
  // project.ai.toml sits next to pipeline.yaml and gets committed.
  for (const char* key : {"api_key", "token", "secret"}) {
    auto dir = scratch_dir("secret");
    write_toml(dir,
               "schema = 1\nprovider = \"grok\"\nmodel = \"m\"\n" + std::string(key)
                   + " = \"xai-abc123\"\n");
    auto r = load_provider_config(dir);
    ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r)) << key;
    EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::SecretInConfig)
        << key;
    // The message must not echo the secret back into a log.
    EXPECT_EQ(std::get<ProviderConfigError>(r).message.find("xai-abc123"), std::string::npos);
    fs::remove_all(dir);
  }
}

TEST(ProviderConfig, InlineApiKeyInSubtableIsRefused) {
  auto dir = scratch_dir("secretsub");
  write_toml(dir, R"(
schema = 1
provider = "grok"
model = "m"

[openai_compatible]
api_key = "xai-abc123"
)");
  auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::SecretInConfig);
  fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Anthropic + the byok-openai alias.
//
// Both used to parse into a kind nothing could construct, so a config
// that looked correct produced "cannot run an agent turn yet" at the
// point of use. These pin the resolution that fixed it.
// ---------------------------------------------------------------------------

TEST(ProviderConfig, AnthropicResolvesEndpointAndKeyVariable) {
  const auto dir = scratch_dir("anthropic");
  write_toml(dir, R"(schema = 1
provider = "anthropic"
model = "claude-sonnet-4-20250514"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r)) << "loader rejected a valid config";
  const auto& cfg = std::get<ProviderConfig>(r);
  EXPECT_EQ(cfg.provider, ProviderKind::BYOKAnthropic);
  EXPECT_EQ(cfg.provider_id, "anthropic");
  EXPECT_EQ(cfg.model, "claude-sonnet-4-20250514");
  EXPECT_EQ(cfg.base_url, "https://api.anthropic.com/v1");
  // The key is never read from the file — only the variable's name.
  EXPECT_EQ(cfg.api_key_env, "ANTHROPIC_API_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, ByokAnthropicSpellingStillAccepted) {
  const auto dir = scratch_dir("byok-anthropic");
  write_toml(dir, R"(schema = 1
provider = "byok-anthropic"
model = "claude-opus-4-20250514"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r));
  const auto& cfg = std::get<ProviderConfig>(r);
  EXPECT_EQ(cfg.provider, ProviderKind::BYOKAnthropic);
  EXPECT_EQ(cfg.api_key_env, "ANTHROPIC_API_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, AnthropicSubtableOverridesEndpointAndKeyVariable) {
  const auto dir = scratch_dir("anthropic-gw");
  write_toml(dir, R"(schema = 1
provider = "anthropic"
model = "claude-sonnet-4-20250514"

[anthropic]
base_url = "https://gateway.internal/anthropic/v1"
api_key_env = "WORK_ANTHROPIC_KEY"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r));
  const auto& cfg = std::get<ProviderConfig>(r);
  EXPECT_EQ(cfg.base_url, "https://gateway.internal/anthropic/v1");
  EXPECT_EQ(cfg.api_key_env, "WORK_ANTHROPIC_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, ByokOpenAiAliasesOntoTheOpenAiPreset) {
  const auto dir = scratch_dir("byok-openai");
  write_toml(dir, R"(schema = 1
provider = "byok-openai"
model = "gpt-5"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfig>(r));
  const auto& cfg = std::get<ProviderConfig>(r);
  // The legacy spelling now lands on a kind that can actually be built.
  EXPECT_EQ(cfg.provider, ProviderKind::OpenAICompatible);
  EXPECT_EQ(cfg.provider_id, "openai");
  EXPECT_EQ(cfg.base_url, "https://api.openai.com/v1");
  EXPECT_EQ(cfg.api_key_env, "OPENAI_API_KEY");
  fs::remove_all(dir);
}

TEST(ProviderConfig, AnthropicStillRequiresAModel) {
  const auto dir = scratch_dir("anthropic-nomodel");
  write_toml(dir, R"(schema = 1
provider = "anthropic"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::MissingField);
  fs::remove_all(dir);
}

TEST(ProviderConfig, AnthropicRefusesAnInlineKey) {
  const auto dir = scratch_dir("anthropic-secret");
  write_toml(dir, R"(schema = 1
provider = "anthropic"
model = "claude-sonnet-4-20250514"
api_key = "sk-ant-leaked"
)");

  const auto r = load_provider_config(dir);
  ASSERT_TRUE(std::holds_alternative<ProviderConfigError>(r));
  EXPECT_EQ(std::get<ProviderConfigError>(r).kind, ProviderConfigErrorKind::SecretInConfig);
  fs::remove_all(dir);
}
