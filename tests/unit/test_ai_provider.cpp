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
  return {ChatMessage::Role::User, std::string(s), {}};
}

const ChatResponse& expect_ok(const ChatResult& r) {
  if (auto* err = std::get_if<ProviderError>(&r)) {
    ADD_FAILURE() << "provider error: " << to_string(err->kind) << ": " << err->message;
  }
  return std::get<ChatResponse>(r);
}

const ProviderError& expect_err(const ChatResult& r) {
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
  auto& got = expect_ok(s.chat_completion(req));
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
  req.messages = {{ChatMessage::Role::User, "hello", {}}};
  const auto body = OllamaProvider::render_request_body(req, {});
  EXPECT_NE(body.find("\"model\":\"llama3.1:8b\""), std::string::npos) << body;
  EXPECT_NE(body.find("\"stream\":false"), std::string::npos) << body;
  EXPECT_NE(body.find("\"role\":\"user\""), std::string::npos) << body;
  EXPECT_NE(body.find("\"content\":\"hello\""), std::string::npos) << body;
}

TEST(OllamaRequest, EscapesQuotesAndNewlines) {
  ChatRequest req;
  req.model = "m";
  req.messages = {{ChatMessage::Role::User, "say \"hi\"\nthere", {}}};
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
  auto& resp = expect_ok(r);
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
  auto& resp = expect_ok(r);
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
