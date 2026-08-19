/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar-c-bridge — provider_call surface. Sprint 14 push 4.
 *
 * Second real FFI surface (after pipeline.h's pipeline_introspection
 * in Sprint 13 push 3). Routes the desktop chat panel through the
 * engine's Provider abstraction (Sprint 10 push 9) via the same
 * libsouxmar-c-bridge surface ADR-0018 established.
 *
 * The provider this surface routes to is chosen by the engine
 * based on the desktop client's per-project `ai.provider` config
 * (BYOK / managed / Ollama / stub). The bridge is dumb about
 * provider selection — that's an engine-layer concern. The
 * bridge just delivers a ChatRequest + brings back a ChatResponse
 * or a typed ProviderError.
 *
 * Stability: every new function declared here is on the bridge
 * ABI contract. The version byte (souxmar_bridge_abi_version() in
 * pipeline.h) bumps from 1 → 2 with this push because the
 * surface grew. Old desktop builds linked against bridge v1
 * cross-check this byte and refuse the call cleanly.
 *
 * Memory ownership: identical contract to pipeline.h —
 *   - String fields in the request are borrowed; the bridge
 *     copies what it needs before returning.
 *   - Returned strings (the assistant's reply text + the error
 *     message if any) are owned by the *response handle* and
 *     valid until the handle is freed.
 *   - Caller frees the response handle via
 *     souxmar_bridge_chat_response_free().
 */

#ifndef SOUXMAR_C_BRIDGE_PROVIDER_H
#define SOUXMAR_C_BRIDGE_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a single chat-completion response. Created by
 * souxmar_bridge_chat_send(); freed by
 * souxmar_bridge_chat_response_free(). */
typedef struct souxmar_bridge_chat_response_t souxmar_bridge_chat_response_t;

/* The provider selection the engine resolved for this call. Reported
 * back to the caller so the desktop client can render "via BYOK
 * Anthropic" / "via managed-ai-proxy" / "via local Ollama" labels
 * in the chat panel without inventing its own provider-name table.
 * Values:
 *   0  unknown / not yet resolved
 *   1  StubProvider             (test paths only)
 *   2  BYOK Anthropic
 *   3  BYOK OpenAI
 *   4  Ollama
 *   5  Managed (proxy.souxmar.invalid — Pro tier, Sprint 15+)
 *   6  OpenAI-compatible (xAI/Grok, DeepSeek, Groq, OpenRouter,
 *      Mistral, Together, or any self-hosted server speaking the
 *      /chat/completions shape). The service is decided by the
 *      base_url in project.ai.toml, not by this constant.
 *
 * Adding a constant here is an additive Tier-0 change per ADR-0016:
 * an older client that does not know value 6 falls through to its
 * default branch, which is the stub.
 */
#define SOUXMAR_BRIDGE_PROVIDER_UNKNOWN 0
#define SOUXMAR_BRIDGE_PROVIDER_STUB 1
#define SOUXMAR_BRIDGE_PROVIDER_ANTHROPIC 2
#define SOUXMAR_BRIDGE_PROVIDER_OPENAI 3
#define SOUXMAR_BRIDGE_PROVIDER_OLLAMA 4
#define SOUXMAR_BRIDGE_PROVIDER_MANAGED 5
#define SOUXMAR_BRIDGE_PROVIDER_OPENAI_COMPATIBLE 6

/* Provider-error kinds. Mirrors `souxmar::ai::ProviderErrorKind`
 * (provider.h on the C++ side). Returned only when the call
 * itself failed; a 200-OK upstream that produced a refusal is
 * still a success at this layer (the refusal text lives in the
 * response's reply_text). */
#define SOUXMAR_BRIDGE_PE_OK 0
#define SOUXMAR_BRIDGE_PE_HTTP_ERROR 1
#define SOUXMAR_BRIDGE_PE_TIMEOUT 2
#define SOUXMAR_BRIDGE_PE_INVALID_RESPONSE 3
#define SOUXMAR_BRIDGE_PE_UNAUTHORIZED 4
#define SOUXMAR_BRIDGE_PE_RATE_LIMITED 5
#define SOUXMAR_BRIDGE_PE_QUOTA_EXHAUSTED 6
#define SOUXMAR_BRIDGE_PE_NOT_CONFIGURED 7
#define SOUXMAR_BRIDGE_PE_INTERNAL 8
/* The turn paused on a tool needing the user's approval. Not a
 * failure: call souxmar_bridge_chat_confirm() to continue. */
#define SOUXMAR_BRIDGE_PE_AWAITING_CONFIRMATION 9

/* Dispatch one chat-completion call. `request_json` is a UTF-8
 * JSON string matching the ChatRequest schema (model, messages,
 * tool_names, temperature, max_tokens). `project_id` is the
 * desktop client's per-project id — used to look up the right
 * provider configuration on the engine side.
 *
 * Returns a non-NULL response handle on success. The handle's
 * `error_kind` field reports SOUXMAR_BRIDGE_PE_OK when the call
 * succeeded; otherwise the handle's `error_text` carries the
 * provider-specific message.
 *
 * Returns NULL only on catastrophic failure (out-of-memory,
 * malformed JSON request). In that case `*out_err` is populated
 * with a caller-frees C-string; on success `*out_err` is NULL.
 *
 * Sprint 14 push 4 — the call is synchronous; streaming + tool-
 * result interleaving (per provider.h's "Streaming + tool-result
 * interleaving land in Sprint 11+") is queued for Sprint 17+
 * once the proxy is stood up. The desktop chat panel waits on
 * the full response today. */
souxmar_bridge_chat_response_t* souxmar_bridge_chat_send(const char* request_json,
                                                         const char* project_id,
                                                         char** out_err);

/* Accessors on the response handle. All strings remain valid
 * until the handle is freed. */
int32_t souxmar_bridge_chat_error_kind(const souxmar_bridge_chat_response_t* r);
const char* souxmar_bridge_chat_error_text(const souxmar_bridge_chat_response_t* r);
const char* souxmar_bridge_chat_reply_text(const souxmar_bridge_chat_response_t* r);
int32_t souxmar_bridge_chat_provider(const souxmar_bridge_chat_response_t* r);
int64_t souxmar_bridge_chat_tokens_in(const souxmar_bridge_chat_response_t* r);
int64_t souxmar_bridge_chat_tokens_out(const souxmar_bridge_chat_response_t* r);

/* ---- Agent loop (additive) -------------------------------------------
 *
 * souxmar_bridge_chat_send() runs a full agent turn: the model is given
 * the tool catalogue, its calls are dispatched against the engine, and
 * results go back until it answers. A turn that reaches a tool needing
 * the user's approval does not block — it suspends and reports
 * SOUXMAR_BRIDGE_PE_AWAITING_CONFIRMATION, because a GUI cannot answer
 * a blocking prompt from inside a synchronous call.
 *
 * The suspended session is held in the library, keyed by project_id, so
 * the mesh and field handles the turn produced survive the pause.
 * Resume it with souxmar_bridge_chat_confirm(). A session is discarded
 * when it completes, when it is resumed, or when a new turn starts for
 * the same project. */

/* Tool calls the turn executed, in order. Valid until the handle is
 * freed. `ok` is 0 for a failure, `refused` is 1 when the user or the
 * policy declined it rather than the tool itself failing. */
int32_t souxmar_bridge_chat_tool_call_count(const souxmar_bridge_chat_response_t* r);
const char* souxmar_bridge_chat_tool_call_name(const souxmar_bridge_chat_response_t* r,
                                               int32_t index);
const char* souxmar_bridge_chat_tool_call_summary(const souxmar_bridge_chat_response_t* r,
                                                  int32_t index);
int32_t souxmar_bridge_chat_tool_call_ok(const souxmar_bridge_chat_response_t* r, int32_t index);
int32_t souxmar_bridge_chat_tool_call_refused(const souxmar_bridge_chat_response_t* r,
                                              int32_t index);

/* Populated iff error_kind == SOUXMAR_BRIDGE_PE_AWAITING_CONFIRMATION.
 * The arguments are the JSON the model produced, for display. */
const char* souxmar_bridge_chat_pending_tool(const souxmar_bridge_chat_response_t* r);
const char* souxmar_bridge_chat_pending_arguments(const souxmar_bridge_chat_response_t* r);

/* Answer a pending confirmation and run the turn to completion (or to
 * the next confirmation). `allow` is 1 to permit, 0 to decline; a
 * decline is reported to the model as a tool error so it can respond
 * rather than stall. Returns NULL with *out_err set when no session is
 * suspended for this project. */
souxmar_bridge_chat_response_t* souxmar_bridge_chat_confirm(const char* project_id,
                                                            int32_t allow,
                                                            char** out_err);

/* Release a response handle. Safe to call with NULL. */
void souxmar_bridge_chat_response_free(souxmar_bridge_chat_response_t* r);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOUXMAR_C_BRIDGE_PROVIDER_H */
