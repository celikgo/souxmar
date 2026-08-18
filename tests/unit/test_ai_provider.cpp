// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 9 — unit tests for the AI provider abstraction.
// Covers StubProvider behaviour, OllamaProvider request encoding,
// and OllamaProvider response decoding. We do NOT exercise the
// curl-subprocess path here — that needs a live daemon and lives in
// docs/ai-providers/ollama-compatibility.md as a soak result.

#include "souxmar/ai/provider.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

using namespace souxmar::ai;

namespace {

ChatMessage user(std::string_view s) {
  return {ChatMessage::Role::User, std::string(s), {}, {}};
}

// These helpers return BY VALUE on purpose. Returning a reference into the
// variant meant that `const auto& x = expect_ok(f(...))` bound a reference
// into a temporary that died at the end of the full expression: the tests
// then read freed memory and either passed by luck or reported garbage
// strings. A copy of a small struct costs nothing here and the trap cannot
// come back.
ChatResponse expect_ok(const ChatResult& r) {
  if (auto* err = std::get_if<ProviderError>(&r)) {
    ADD_FAILURE() << "provider error: " << to_string(err->kind) << ": " << err->message;
  }
  return std::get<ChatResponse>(r);
}

ProviderError expect_err(const ChatResult& r) {
  if (std::holds_alternative<ChatResponse>(r)) {
    ADD_FAILURE() << "expected ProviderError, got ChatResponse";
  }
  return std::get<ProviderError>(r);
}

}  // namespace

// ===========================================================================
// StubProvider
// ===========================================================================

TEST(StubProvider, RoleStringRoundtrip) {
  EXPECT_EQ(to_string(ChatMessage::Role::System), "system");
  EXPECT_EQ(to_string(ChatMessage::Role::User), "user");
  EXPECT_EQ(to_string(ChatMessage::Role::Assistant), "assistant");
  EXPECT_EQ(to_string(ChatMessage::Role::Tool), "tool");
}

TEST(StubProvider, ErrorStringRoundtrip) {
  EXPECT_EQ(to_string(ProviderErrorKind::ProviderHttpError), "provider-http-error");
  EXPECT_EQ(to_string(ProviderErrorKind::LocalDaemonUnreachable), "local-daemon-unreachable");
  EXPECT_EQ(to_string(ProviderErrorKind::ModelNotFound), "model-not-found");
  EXPECT_EQ(to_string(ProviderErrorKind::BadRequest), "bad-request");
}

TEST(StubProvider, ProgrammedReplyMatchesByTriggerSubstring) {
  StubProvider s;
  ChatResponse r;
  r.text = "Hi, calling list_plugins.";
  ToolCall tc;
  tc.id = "c1";
  tc.name = "list_plugins";
  tc.arguments_json = "{}";
  r.tool_calls.push_back(tc);
  s.program_reply("test-model", "what plugins", r);

  ChatRequest req;
  req.model = "test-model";
  req.messages = {user("Hey, what plugins are available?")};
  const auto& got = expect_ok(s.chat_completion(req));
  EXPECT_EQ(got.text, "Hi, calling list_plugins.");
  ASSERT_EQ(got.tool_calls.size(), 1u);
  EXPECT_EQ(got.tool_calls[0].name, "list_plugins");
}

TEST(StubProvider, EmptyMessagesRejected) {
  StubProvider s;
  ChatRequest req;
  req.model = "any";
  const auto& err = expect_err(s.chat_completion(req));
  EXPECT_EQ(err.kind, ProviderErrorKind::BadRequest);
}

TEST(StubProvider, UnmatchedTriggerReturnsProtocolMismatch) {
  StubProvider s;
  // Programmed reply only for "specific phrase".
  s.program_reply("m", "specific phrase", ChatResponse{});
  ChatRequest req;
  req.model = "m";
  req.messages = {user("something completely unrelated")};
  EXPECT_EQ(expect_err(s.chat_completion(req)).kind, ProviderErrorKind::ProtocolMismatch);
}

TEST(StubProvider, AvailableModelsDeduplicatesAndPreservesInsertOrder) {
  StubProvider s;
  s.program_reply("a", "x", ChatResponse{});
  s.program_reply("b", "y", ChatResponse{});
  s.program_reply("a", "z", ChatResponse{});
  const auto ms = s.available_models();
  ASSERT_EQ(ms.size(), 2u);
  EXPECT_EQ(ms[0], "a");
  EXPECT_EQ(ms[1], "b");
}

// ===========================================================================
// OllamaProvider — request rendering
// ===========================================================================

TEST(OllamaRequest, RendersMinimalChat) {
  ChatRequest req;
  req.model = "llama3.1:8b";
  req.messages = {{ChatMessage::Role::User, "hello", {}, {}}};
  const auto body = OllamaProvider::render_request_body(req, {});
  EXPECT_NE(body.find("\"model\":\"llama3.1:8b\""), std::string::npos) << body;
  EXPECT_NE(body.find("\"stream\":false"), std::string::npos) << body;
  EXPECT_NE(body.find("\"role\":\"user\""), std::string::npos) << body;
  EXPECT_NE(body.find("\"content\":\"hello\""), std::string::npos) << body;
}

TEST(OllamaRequest, EscapesQuotesAndNewlines) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "say \"hi\"\nthere", {}, {}}};
  const auto body = OllamaProvider::render_request_body(req, {});
  EXPECT_NE(body.find("say \\\"hi\\\"\\nthere"), std::string::npos) << body;
}

TEST(OllamaRequest, IncludesToolDefinitionsAsFunctionShape) {
  ChatRequest req;
  req.model = "m";
  req.messages = {user("call something")};
  std::vector<Tool> tools;
  Tool t;
  t.name = "list_plugins";
  t.description = "list installed plugins";
  tools.push_back(t);
  const auto body = OllamaProvider::render_request_body(req, tools);
  EXPECT_NE(body.find("\"tools\":["), std::string::npos);
  EXPECT_NE(body.find("\"type\":\"function\""), std::string::npos);
  EXPECT_NE(body.find("\"name\":\"list_plugins\""), std::string::npos);
  EXPECT_NE(body.find("\"parameters\":{\"type\":\"object\""), std::string::npos);
}

TEST(OllamaRequest, OmitsToolsArrayWhenEmpty) {
  ChatRequest req;
  req.model = "m";
  req.messages = {user("hi")};
  const auto body = OllamaProvider::render_request_body(req, {});
  EXPECT_EQ(body.find("\"tools\":"), std::string::npos)
      << "must not emit `tools:[]` when no tools are registered";
}

TEST(OllamaRequest, EmitsTemperatureAndMaxTokensUnderOptionsKey) {
  ChatRequest req;
  req.model = "m";
  req.messages = {user("hi")};
  req.temperature = 0.5;
  req.max_tokens = 256;
  const auto body = OllamaProvider::render_request_body(req, {});
  EXPECT_NE(body.find("\"options\":"), std::string::npos) << body;
  EXPECT_NE(body.find("\"temperature\":"), std::string::npos) << body;
  EXPECT_NE(body.find("\"num_predict\":256"), std::string::npos) << body;
}

// ===========================================================================
// OllamaProvider — response parsing
// ===========================================================================

TEST(OllamaResponse, ParsesPlainAssistantText) {
  constexpr const char* kBody = R"({
    "model":"llama3.1:8b",
    "message":{"role":"assistant","content":"Hello!"},
    "done":true,
    "prompt_eval_count":12,
    "eval_count":3
  })";
  auto r = OllamaProvider::parse_response_body(kBody);
  const auto& resp = expect_ok(r);
  EXPECT_EQ(resp.text, "Hello!");
  EXPECT_TRUE(resp.tool_calls.empty());
  EXPECT_EQ(resp.input_tokens, 12u);
  EXPECT_EQ(resp.output_tokens, 3u);
}

TEST(OllamaResponse, ParsesToolCalls) {
  constexpr const char* kBody = R"({
    "model":"llama3.1:8b",
    "message":{
      "role":"assistant",
      "content":"",
      "tool_calls":[
        {"function":{"name":"list_plugins","arguments":"{}"}}
      ]
    },
    "done":true
  })";
  auto r = OllamaProvider::parse_response_body(kBody);
  const auto& resp = expect_ok(r);
  ASSERT_EQ(resp.tool_calls.size(), 1u);
  EXPECT_EQ(resp.tool_calls[0].name, "list_plugins");
}

TEST(OllamaResponse, MapsErrorBodyToModelNotFound) {
  constexpr const char* kBody = R"({"error":"model 'frobnitz' not found"})";
  auto r = OllamaProvider::parse_response_body(kBody);
  const auto& err = expect_err(r);
  EXPECT_EQ(err.kind, ProviderErrorKind::ModelNotFound);
}

TEST(OllamaResponse, RejectsMalformedTopLevel) {
  auto r = OllamaProvider::parse_response_body("not-json-at-all");
  // YAML-cpp may accept "not-json-at-all" as a scalar; the response
  // parser treats anything that isn't a map as malformed.
  const auto& err = expect_err(r);
  EXPECT_EQ(err.kind, ProviderErrorKind::MalformedResponse);
}

TEST(OllamaResponse, MarksTruncatedWhenDoneIsFalse) {
  constexpr const char* kBody = R"({
    "message":{"role":"assistant","content":"..."},
    "done":false
  })";
  auto r = OllamaProvider::parse_response_body(kBody);
  EXPECT_TRUE(std::get<ChatResponse>(r).truncated);
}

// ---- OpenAICompatibleProvider -----------------------------------------
//
// One provider serves every service speaking OpenAI's
// /chat/completions shape, so the tests below pin the wire format
// rather than any single vendor.

TEST(OpenAICompatiblePresets, GrokIsPresentAndWellFormed) {
  const auto* grok = find_openai_compatible_preset("grok");
  ASSERT_NE(grok, nullptr);
  EXPECT_EQ(grok->base_url, "https://api.x.ai/v1");
  EXPECT_FALSE(grok->display.empty());
  EXPECT_FALSE(grok->key_env.empty());
}

TEST(OpenAICompatiblePresets, IdsAreUniqueAndUrlsAreHttps) {
  const auto& presets = openai_compatible_presets();
  ASSERT_FALSE(presets.empty());
  for (std::size_t i = 0; i < presets.size(); ++i) {
    for (std::size_t j = i + 1; j < presets.size(); ++j) {
      EXPECT_NE(presets[i].id, presets[j].id) << "duplicate preset id";
    }
    if (!presets[i].base_url.empty()) {
      // A hosted service reached over cleartext would leak the bearer
      // token; the escape-hatch entry (empty base_url) is where a
      // local http:// endpoint gets configured.
      EXPECT_EQ(presets[i].base_url.rfind("https://", 0), 0u) << presets[i].id;
    }
  }
}

TEST(OpenAICompatibleRequest, RendersOpenAiShape) {
  ChatRequest req;
  req.model = "grok-test";
  req.messages = {{ChatMessage::Role::System, "be brief", "", {}},
                  {ChatMessage::Role::User, "mesh the beam", "", {}}};
  const auto body = OpenAICompatibleProvider::render_request_body(req, {});
  EXPECT_NE(body.find("\"model\":\"grok-test\""), std::string::npos);
  EXPECT_NE(body.find("\"role\":\"system\""), std::string::npos);
  EXPECT_NE(body.find("\"content\":\"mesh the beam\""), std::string::npos);
  EXPECT_NE(body.find("\"stream\":false"), std::string::npos);
  // No tools were supplied, so none must be advertised — a `tools` key
  // with an empty array makes some services reject the request.
  EXPECT_EQ(body.find("\"tools\""), std::string::npos);
}

TEST(OpenAICompatibleRequest, AdvertisesToolsWhenGiven) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", "", {}}};
  Tool t;
  t.name = "mesh";
  t.description = "Generate a mesh";
  const auto body = OpenAICompatibleProvider::render_request_body(req, {t});
  EXPECT_NE(body.find("\"tools\":["), std::string::npos);
  EXPECT_NE(body.find("\"type\":\"function\""), std::string::npos);
  EXPECT_NE(body.find("\"name\":\"mesh\""), std::string::npos);
}

TEST(OpenAICompatibleResponse, ParsesTextReply) {
  constexpr const char* kBody = R"({
    "choices":[{"index":0,"message":{"role":"assistant","content":"hello"},
                "finish_reason":"stop"}],
    "usage":{"prompt_tokens":11,"completion_tokens":3}
  })";
  const auto r = OpenAICompatibleProvider::parse_response_body(kBody);
  const auto resp = expect_ok(r);
  EXPECT_EQ(resp.text, "hello");
  EXPECT_EQ(resp.input_tokens, 11u);
  EXPECT_EQ(resp.output_tokens, 3u);
  EXPECT_FALSE(resp.truncated);
}

TEST(OpenAICompatibleResponse, ParsesToolCall) {
  constexpr const char* kBody = R"({
    "choices":[{"message":{"role":"assistant","content":null,
      "tool_calls":[{"id":"call_1","type":"function",
        "function":{"name":"mesh","arguments":"{\"target_size\":0.05}"}}]},
      "finish_reason":"tool_calls"}]
  })";
  const auto r = OpenAICompatibleProvider::parse_response_body(kBody);
  const auto resp = expect_ok(r);
  ASSERT_EQ(resp.tool_calls.size(), 1u);
  EXPECT_EQ(resp.tool_calls[0].id, "call_1");
  EXPECT_EQ(resp.tool_calls[0].name, "mesh");
  EXPECT_NE(resp.tool_calls[0].arguments_json.find("target_size"), std::string::npos);
}

TEST(OpenAICompatibleResponse, MarksTruncatedOnLengthFinish) {
  constexpr const char* kBody = R"({
    "choices":[{"message":{"role":"assistant","content":"partial"},
                "finish_reason":"length"}]
  })";
  EXPECT_TRUE(expect_ok(OpenAICompatibleProvider::parse_response_body(kBody)).truncated);
}

TEST(OpenAICompatibleResponse, TypedErrorsMapToKinds) {
  struct Case {
    const char* body;
    ProviderErrorKind kind;
  };

  const Case cases[] = {
      {R"({"error":{"message":"Rate limit reached","type":"rate_limit_error"}})",
       ProviderErrorKind::RateLimited},
      {R"({"error":{"message":"maximum context length is 8192 tokens"}})",
       ProviderErrorKind::ContextLengthExceeded},
      {R"({"error":{"message":"The model `nope` does not exist","type":"invalid_request_error"}})",
       ProviderErrorKind::ModelNotFound},
      {R"({"error":{"message":"Something else"}})", ProviderErrorKind::ProviderHttpError},
  };
  for (const auto& c : cases) {
    EXPECT_EQ(expect_err(OpenAICompatibleProvider::parse_response_body(c.body)).kind, c.kind)
        << c.body;
  }
}

TEST(OpenAICompatibleResponse, MissingChoicesIsProtocolMismatch) {
  // A gateway that answers 200 with something else entirely.
  const auto r = OpenAICompatibleProvider::parse_response_body(R"({"ok":true})");
  EXPECT_EQ(expect_err(r).kind, ProviderErrorKind::ProtocolMismatch);
}

TEST(OpenAICompatibleResponse, MalformedBodyDoesNotThrow) {
  // Pointing `base_url` at a web server that returns HTML is a normal
  // user mistake; it must produce a typed error, not an exception
  // escaping through the C bridge.
  for (const char* body : {"not-json-at-all", "<html><body>502</body></html>", "", "[1,2,3]"}) {
    EXPECT_NO_THROW({
      const auto r = OpenAICompatibleProvider::parse_response_body(body);
      EXPECT_TRUE(std::holds_alternative<ProviderError>(r)) << body;
    }) << body;
  }
}

TEST(OpenAICompatibleCurlConfig, KeepsCredentialOffTheCommandLine) {
  const auto cfg =
      OpenAICompatibleProvider::render_curl_config("https://api.x.ai/v1/chat/completions",
                                                   "xai-secret-123",
                                                   R"({"model":"m"})",
                                                   std::chrono::seconds(30));
  // The config is fed to `curl -K -` on stdin. What matters is that it
  // carries the credential and the body, so neither has to be an argv
  // element visible in `ps`.
  EXPECT_NE(cfg.find("Authorization: Bearer xai-secret-123"), std::string::npos);
  EXPECT_NE(cfg.find("url = \"https://api.x.ai/v1/chat/completions\""), std::string::npos);
  EXPECT_NE(cfg.find("max-time = 30"), std::string::npos);
  // The JSON body must be escaped for curl's config grammar: every
  // embedded quote backslash-escaped, so curl reconstructs it exactly.
  EXPECT_NE(cfg.find(R"(data-binary = "{\"model\":\"m\"}")"), std::string::npos);
}

TEST(OpenAICompatibleCurlConfig, EscapesBackslashesAndNewlines) {
  const auto cfg = OpenAICompatibleProvider::render_curl_config(
      "https://x/y",
      "",
      "{\"a\":\"back\\\\slash\",\"b\":\"line\\nbreak\"}",
      std::chrono::seconds(5));
  // A literal backslash in the JSON must reach curl as a literal
  // backslash, so it is doubled in the config.
  EXPECT_NE(cfg.find(R"(\\\\)"), std::string::npos);
  // No key configured → no Authorization header at all, rather than an
  // empty bearer that reads as a malformed credential upstream.
  EXPECT_EQ(cfg.find("Authorization"), std::string::npos);
}

TEST(OpenAICompatibleProviderCalls, RefusesEmptyBaseUrl) {
  OpenAICompatibleOptions opts;
  opts.provider_id = "grok";
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", "", {}}};
  OpenAICompatibleProvider p(opts);
  EXPECT_EQ(expect_err(p.chat_completion(req)).kind, ProviderErrorKind::BadRequest);
}

TEST(OpenAICompatibleProviderCalls, ReportsItsConfiguredName) {
  OpenAICompatibleOptions opts;
  opts.provider_id = "grok";
  opts.base_url = "https://api.x.ai/v1";
  OpenAICompatibleProvider p(opts);
  EXPECT_EQ(p.name(), "grok");
}

// ---- Live round-trip (opt-in) ------------------------------------------
//
// Skipped unless SOUXMAR_TEST_OPENAI_BASE_URL is set, so the suite stays
// hermetic. It exists because the two things most likely to be wrong in
// this provider — the curl-config escaping and the request shape — can
// only be proven by a server actually accepting them.
//
// Against a local echo server it validates the wire format. Against a
// real endpoint it validates the service:
//
//   SOUXMAR_TEST_OPENAI_BASE_URL=https://api.x.ai/v1 \
//   SOUXMAR_TEST_OPENAI_KEY=$XAI_API_KEY \
//   SOUXMAR_TEST_OPENAI_MODEL=<a model your account can reach> \
//   ./souxmar_unit_tests --gtest_filter='OpenAICompatibleLive.*'

TEST(OpenAICompatibleLive, RoundTripsAgainstConfiguredEndpoint) {
  const char* base = std::getenv("SOUXMAR_TEST_OPENAI_BASE_URL");
  if (base == nullptr || *base == '\0') {
    GTEST_SKIP() << "set SOUXMAR_TEST_OPENAI_BASE_URL to run";
  }
  OpenAICompatibleOptions opts;
  opts.provider_id = "live-test";
  opts.base_url = base;
  if (const char* k = std::getenv("SOUXMAR_TEST_OPENAI_KEY"); k != nullptr) {
    opts.api_key = k;
  }
  opts.timeout = std::chrono::seconds(30);

  ChatRequest req;
  const char* model = std::getenv("SOUXMAR_TEST_OPENAI_MODEL");
  req.model = (model != nullptr && *model != '\0') ? model : "test-model";
  req.messages = {{ChatMessage::Role::User, "Reply with the single word: ok", "", {}}};

  OpenAICompatibleProvider p(std::move(opts));
  const auto r = p.chat_completion(req);
  if (const auto* err = std::get_if<ProviderError>(&r)) {
    FAIL() << "live call failed: " << to_string(err->kind) << ": " << err->message;
  }
  const auto& resp = std::get<ChatResponse>(r);
  EXPECT_FALSE(resp.text.empty() && resp.tool_calls.empty())
      << "endpoint answered with neither text nor a tool call";
}

// ===========================================================================
// AnthropicProvider
//
// The Messages API differs from the OpenAI shape in four ways that are
// each a 400 when you get them wrong, so every one of them gets a test:
// top-level `system`, mandatory `max_tokens`, `input_schema` on tools,
// and tool calls as content blocks rather than a parallel array.
// ===========================================================================

namespace {

// Anthropic requires that every tool_result reference a tool_use that
// appeared earlier in the conversation. Checking id pairing by hand in
// each test would be noise, so it lives here.
bool tool_result_ids_are_paired(const std::string& body) {
  std::vector<std::string> uses;
  for (std::size_t p = body.find("\"tool_use\""); p != std::string::npos;
       p = body.find("\"tool_use\"", p + 1)) {
    const auto id = body.find("\"id\":\"", p);
    if (id == std::string::npos)
      return false;
    const auto start = id + 6;
    uses.push_back(body.substr(start, body.find('"', start) - start));
  }
  for (std::size_t p = body.find("\"tool_use_id\":\""); p != std::string::npos;
       p = body.find("\"tool_use_id\":\"", p + 1)) {
    const auto start = p + 15;
    const auto ref = body.substr(start, body.find('"', start) - start);
    if (std::find(uses.begin(), uses.end(), ref) == uses.end())
      return false;
  }
  return true;
}

}  // namespace

TEST(AnthropicRequest, HoistsSystemTurnToTopLevelField) {
  ChatRequest req;
  req.model = "claude-sonnet-4-20250514";
  req.messages = {{ChatMessage::Role::System, "you are souxmar", {}, {}},
                  {ChatMessage::Role::User, "hello", {}, {}}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);

  EXPECT_NE(body.find("\"system\":\"you are souxmar\""), std::string::npos);
  // A system *message* is a hard 400 — it must not survive into the array.
  EXPECT_EQ(body.find("\"role\":\"system\""), std::string::npos);
}

TEST(AnthropicRequest, JoinsMultipleSystemTurnsInOrder) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::System, "first", {}, {}},
                  {ChatMessage::Role::System, "second", {}, {}},
                  {ChatMessage::Role::User, "hi", {}, {}}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);
  EXPECT_NE(body.find("\"system\":\"first\\n\\nsecond\""), std::string::npos);
}

TEST(AnthropicRequest, AlwaysEmitsMaxTokens) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}}};

  // Unset on the request: the provider default fills in, because
  // omitting the field entirely is rejected.
  EXPECT_NE(AnthropicProvider::render_request_body(req, {}, 4096).find("\"max_tokens\":4096"),
            std::string::npos);

  req.max_tokens = 128;
  EXPECT_NE(AnthropicProvider::render_request_body(req, {}, 4096).find("\"max_tokens\":128"),
            std::string::npos);
}

TEST(AnthropicRequest, AdvertisesToolsWithInputSchemaNotFunctionParameters) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}}};
  req.tools = {{"mesh", "Mesh the current geometry"}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);

  EXPECT_NE(body.find("\"name\":\"mesh\""), std::string::npos);
  EXPECT_NE(body.find("\"input_schema\""), std::string::npos);
  // The OpenAI tool shape here is a 400.
  EXPECT_EQ(body.find("\"parameters\""), std::string::npos);
  EXPECT_EQ(body.find("\"type\":\"function\""), std::string::npos);
}

TEST(AnthropicRequest, OmitsToolsKeyWhenCatalogueEmpty) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}}};

  EXPECT_EQ(AnthropicProvider::render_request_body(req, {}, 4096).find("\"tools\""),
            std::string::npos);
}

TEST(AnthropicRequest, RendersAssistantToolCallAsToolUseBlock) {
  ChatMessage assistant;
  assistant.role = ChatMessage::Role::Assistant;
  assistant.content = "Checking.";
  assistant.tool_calls = {{"toolu_7", "list_plugins", "{\"verbose\":true}"}};

  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "how many?", {}, {}}, assistant};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);

  EXPECT_NE(body.find("\"type\":\"tool_use\""), std::string::npos);
  EXPECT_NE(body.find("\"id\":\"toolu_7\""), std::string::npos);
  EXPECT_NE(body.find("\"name\":\"list_plugins\""), std::string::npos);
  // `input` must be an object, not a JSON-encoded string.
  EXPECT_NE(body.find("\"input\":{\"verbose\":true}"), std::string::npos);
}

TEST(AnthropicRequest, RendersToolResultAsUserBlockReferencingTheCall) {
  ChatMessage assistant;
  assistant.role = ChatMessage::Role::Assistant;
  assistant.content = "";
  assistant.tool_calls = {{"toolu_1", "list_plugins", "{}"}};

  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "how many?", {}, {}},
                  assistant,
                  {ChatMessage::Role::Tool, "registry: 30 capabilities", "toolu_1", {}}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);

  EXPECT_NE(body.find("\"type\":\"tool_result\""), std::string::npos);
  EXPECT_NE(body.find("\"tool_use_id\":\"toolu_1\""), std::string::npos);
  // A tool result is a user turn, never a `tool` role.
  EXPECT_EQ(body.find("\"role\":\"tool\""), std::string::npos);
  EXPECT_TRUE(tool_result_ids_are_paired(body));
}

TEST(AnthropicRequest, MergesParallelToolResultsIntoOneUserTurn) {
  ChatMessage assistant;
  assistant.role = ChatMessage::Role::Assistant;
  assistant.tool_calls = {{"toolu_a", "mesh", "{}"}, {"toolu_b", "solve", "{}"}};

  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "go", {}, {}},
                  assistant,
                  {ChatMessage::Role::Tool, "meshed", "toolu_a", {}},
                  {ChatMessage::Role::Tool, "solved", "toolu_b", {}}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);

  // Both results belong to a single user message: user, assistant, user.
  std::size_t user_turns = 0;
  for (std::size_t p = body.find("\"role\":\"user\""); p != std::string::npos;
       p = body.find("\"role\":\"user\"", p + 1)) {
    ++user_turns;
  }
  EXPECT_EQ(user_turns, 2u);
  EXPECT_NE(body.find("\"tool_use_id\":\"toolu_a\""), std::string::npos);
  EXPECT_NE(body.find("\"tool_use_id\":\"toolu_b\""), std::string::npos);
  EXPECT_TRUE(tool_result_ids_are_paired(body));
}

TEST(AnthropicRequest, SkipsAssistantTurnWithNeitherTextNorCalls) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}},
                  {ChatMessage::Role::Assistant, "", {}, {}}};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);
  // An empty content array is a 400; the turn is dropped instead.
  EXPECT_EQ(body.find("\"role\":\"assistant\""), std::string::npos);
  EXPECT_EQ(body.find("\"content\":[]"), std::string::npos);
}

TEST(AnthropicRequest, MalformedToolArgumentsFallBackToEmptyObject) {
  ChatMessage assistant;
  assistant.role = ChatMessage::Role::Assistant;
  assistant.tool_calls = {{"toolu_1", "mesh", "not json at all"}};

  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "go", {}, {}}, assistant};

  const auto body = AnthropicProvider::render_request_body(req, {}, 4096);
  // Better the tool's own "missing field" error than a 400 from the API.
  EXPECT_NE(body.find("\"input\":{}"), std::string::npos);
  EXPECT_EQ(body.find("not json at all"), std::string::npos);
}

TEST(AnthropicResponse, ParsesTextReply) {
  const auto r = expect_ok(AnthropicProvider::parse_response_body(R"({
    "id":"msg_1","type":"message","role":"assistant","stop_reason":"end_turn",
    "content":[{"type":"text","text":"30 plugins are installed."}],
    "usage":{"input_tokens":412,"output_tokens":19}})"));

  EXPECT_EQ(r.text, "30 plugins are installed.");
  EXPECT_TRUE(r.tool_calls.empty());
  EXPECT_EQ(r.input_tokens, 412u);
  EXPECT_EQ(r.output_tokens, 19u);
  EXPECT_FALSE(r.truncated);
}

TEST(AnthropicResponse, ParsesToolUseAlongsideText) {
  const auto r = expect_ok(AnthropicProvider::parse_response_body(R"({
    "id":"msg_2","type":"message","role":"assistant","stop_reason":"tool_use",
    "content":[{"type":"text","text":"Let me look."},
               {"type":"tool_use","id":"toolu_9","name":"list_plugins",
                "input":{"verbose":true}}]})"));

  EXPECT_EQ(r.text, "Let me look.");
  ASSERT_EQ(r.tool_calls.size(), 1u);
  EXPECT_EQ(r.tool_calls[0].id, "toolu_9");
  EXPECT_EQ(r.tool_calls[0].name, "list_plugins");
  // Re-emitted as the JSON string the dispatcher parses.
  EXPECT_NE(r.tool_calls[0].arguments_json.find("verbose"), std::string::npos);
}

TEST(AnthropicResponse, ConcatenatesMultipleTextBlocks) {
  const auto r = expect_ok(AnthropicProvider::parse_response_body(R"({
    "content":[{"type":"text","text":"one"},{"type":"text","text":"two"}]})"));
  EXPECT_EQ(r.text, "one\ntwo");
}

TEST(AnthropicResponse, MarksTruncatedOnMaxTokensStop) {
  const auto r = expect_ok(AnthropicProvider::parse_response_body(R"({
    "stop_reason":"max_tokens","content":[{"type":"text","text":"cut off"}]})"));
  EXPECT_TRUE(r.truncated);
}

TEST(AnthropicResponse, TypedErrorsMapToKinds) {
  struct Case {
    const char* type;
    const char* message;
    ProviderErrorKind expected;
  };

  const Case cases[] = {
      {"rate_limit_error", "slow down", ProviderErrorKind::RateLimited},
      {"overloaded_error", "try later", ProviderErrorKind::RateLimited},
      {"not_found_error", "model: nope", ProviderErrorKind::ModelNotFound},
      {"authentication_error", "bad key", ProviderErrorKind::ProviderHttpError},
      {"invalid_request_error", "malformed body", ProviderErrorKind::BadRequest},
      {"invalid_request_error",
       "context window exceeded",
       ProviderErrorKind::ContextLengthExceeded},
  };
  for (const auto& c : cases) {
    const std::string body = std::string(R"({"type":"error","error":{"type":")") + c.type
                             + R"(","message":")" + c.message + R"("}})";
    const auto err = expect_err(AnthropicProvider::parse_response_body(body));
    EXPECT_EQ(err.kind, c.expected) << "type=" << c.type << " message=" << c.message;
    EXPECT_NE(err.message.find(c.message), std::string::npos);
  }
}

TEST(AnthropicResponse, MissingContentIsProtocolMismatch) {
  const auto err = expect_err(AnthropicProvider::parse_response_body(R"({"id":"msg_3"})"));
  EXPECT_EQ(err.kind, ProviderErrorKind::ProtocolMismatch);
}

TEST(AnthropicResponse, MalformedBodyDoesNotThrow) {
  for (const char* body : {"", "[]", "\"just a string\"", "{\"content\":\"not an array\"}"}) {
    const auto r = AnthropicProvider::parse_response_body(body);
    EXPECT_TRUE(std::holds_alternative<ProviderError>(r)) << "body=" << body;
  }
}

TEST(AnthropicCurlConfig, KeepsCredentialOffTheCommandLineAndSetsRequiredHeaders) {
  const auto cfg = AnthropicProvider::render_curl_config("https://api.anthropic.com/v1/messages",
                                                         "sk-ant-secret",
                                                         "2023-06-01",
                                                         "{}",
                                                         std::chrono::seconds(30));

  // The credential is in the stdin config, which is the whole point.
  EXPECT_NE(cfg.find("x-api-key: sk-ant-secret"), std::string::npos);
  // Bearer auth would be silently ignored by this API.
  EXPECT_EQ(cfg.find("Authorization"), std::string::npos);
  // Both mandatory headers present.
  EXPECT_NE(cfg.find("anthropic-version: 2023-06-01"), std::string::npos);
  EXPECT_NE(cfg.find("Content-Type: application/json"), std::string::npos);
  EXPECT_NE(cfg.find("max-time = 30"), std::string::npos);
}

TEST(AnthropicProviderCalls, RefusesEmptyRequestsWithoutTouchingTheNetwork) {
  AnthropicProviderOptions opts;
  opts.api_key = "sk-ant-test";
  AnthropicProvider p(std::move(opts));

  ChatRequest no_model;
  no_model.messages = {{ChatMessage::Role::User, "hi", {}, {}}};
  EXPECT_EQ(expect_err(p.chat_completion(no_model)).kind, ProviderErrorKind::BadRequest);

  ChatRequest no_messages;
  no_messages.model = "claude-sonnet-4-20250514";
  EXPECT_EQ(expect_err(p.chat_completion(no_messages)).kind, ProviderErrorKind::BadRequest);
}

TEST(AnthropicProviderCalls, MissingKeyIsReportedNotAttempted) {
  AnthropicProvider p{AnthropicProviderOptions{}};  // no api_key
  ChatRequest req;
  req.model = "claude-sonnet-4-20250514";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}}};

  const auto err = expect_err(p.chat_completion(req));
  EXPECT_EQ(err.kind, ProviderErrorKind::BadRequest);
  EXPECT_NE(err.message.find("ANTHROPIC_API_KEY"), std::string::npos);
}

TEST(AnthropicProviderCalls, ReportsNameAndKnownModels) {
  AnthropicProvider p{AnthropicProviderOptions{}};
  EXPECT_EQ(p.name(), "anthropic");
  // Never empty — `--list-models` stays useful with no network.
  EXPECT_FALSE(p.available_models().empty());
}

// ===========================================================================
// OpenAI-compatible: the assistant turn must carry its tool calls
// ===========================================================================

TEST(OpenAICompatibleRequest, EchoesAssistantToolCallsSoToolResultsAreNotOrphaned) {
  ChatMessage assistant;
  assistant.role = ChatMessage::Role::Assistant;
  assistant.content = "";
  assistant.tool_calls = {{"call_1", "list_plugins", "{\"verbose\":true}"}};

  ChatRequest req;
  req.model = "gpt-5";
  req.messages = {{ChatMessage::Role::User, "how many?", {}, {}},
                  assistant,
                  {ChatMessage::Role::Tool, "registry: 30 capabilities", "call_1", {}}};

  const auto body = OpenAICompatibleProvider::render_request_body(req, {});

  // Without this, OpenAI rejects the request: a `tool` message must
  // follow an assistant message carrying the matching call.
  EXPECT_NE(body.find("\"tool_calls\""), std::string::npos);
  EXPECT_NE(body.find("\"id\":\"call_1\""), std::string::npos);
  EXPECT_NE(body.find("\"tool_call_id\":\"call_1\""), std::string::npos);
  // `arguments` is a JSON-encoded *string* per the spec, so the braces
  // arrive escaped rather than inlined as an object.
  EXPECT_NE(body.find("\\\"verbose\\\""), std::string::npos);
}

TEST(OpenAICompatibleRequest, OmitsToolCallsKeyForPlainAssistantTurn) {
  ChatRequest req;
  req.model = "gpt-5";
  req.messages = {{ChatMessage::Role::User, "hi", {}, {}},
                  {ChatMessage::Role::Assistant, "hello", {}, {}}};

  EXPECT_EQ(OpenAICompatibleProvider::render_request_body(req, {}).find("\"tool_calls\""),
            std::string::npos);
}
