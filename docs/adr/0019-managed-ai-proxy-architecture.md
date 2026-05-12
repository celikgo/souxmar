# ADR-0019: Managed-AI proxy — architecture for the Pro tier

- **Status:** Accepted
- **Date:** 2026-05-12 (Sprint 14 push 3)
- **Author:** souxmar platform team
- **Deciders:** platform, AI, billing, security review
- **Tier:** 1 (architecture — names what the Pro-tier server-side
  component is, what it isn't, and what shape its API has at
  Sprint 14's MVP scaffold; the *contract* with desktop clients
  is named here, the implementation lands incrementally)
- **Affects:** new `services/managed-ai-proxy/` directory tree;
  `include/souxmar/ai/provider.h` (no header change today, but
  this ADR queues a `ManagedProvider` adapter for Sprint 15);
  desktop chat panel routing; the billing-counter shape Sprint
  16's Stripe POC consumes.

## Context

The business model from `BUSINESS_MODEL.md` § "Pro tier" promises
"managed AI — souxmar fronts a frontier model so the user
doesn't manage API keys / billing themselves; we pay the upstream
costs + bill the user monthly." Sprints 1-13 built every Pro-tier
*prerequisite* (provider abstraction, BYOK keychain, session
budget tracking, tool dispatcher, agent eval suite) but the
managed-AI half has stayed deferred.

Sprint 14's exit criterion per SPRINT_PLAN.md § Sprint 14 is
"Managed-AI proxy MVP (Pro tier infrastructure); billing
integration POC." Push 3 (this push) lands the **architecture**
+ the **scaffold** — the proxy directory tree, the API contract,
the configuration model, the auth shape, the billing-counter
schema. Push 4's `provider_call` FFI surfaces the new
`ManagedProvider` path. The full proxy implementation (token
streaming, per-tier rate limits, observability) accumulates
through Sprint 15.

The decisions worth recording in an ADR rather than the proxy's
own readme:

1. **What's in the proxy vs. what's still in the desktop client?**
2. **Auth model — souxmar-issued opaque tokens, not direct
   Anthropic/OpenAI keys.**
3. **Billing-counter shape — pre-charge per token, not
   post-charge per session.**
4. **Why a separate process / repo subtree, not a library
   linked into the CLI.**
5. **Compatibility with BYOK — both must coexist.**

## Decision

### 1. What the proxy contains

The proxy hosts:

- A small HTTP server (POST `/v1/chat`, `/v1/account`,
  `/v1/quota`) — request schema matches souxmar's existing
  `ChatRequest` (provider.h) so the desktop client speaks the
  same shape it speaks to the StubProvider / OllamaProvider.
- A connection pool to upstream providers (Anthropic at MVP;
  OpenAI + Gemini queued post-1.0).
- A per-user quota tracker keyed by the opaque token in the
  Authorization header. Counter shape is documented in § 3.
- A billing-event emitter that posts to the Stripe webhook
  endpoint Sprint 14 push 4 / Sprint 16 wire end-to-end.

The proxy does **NOT** contain:

- The agent tool dispatcher or any tool definitions — those
  stay on the desktop client, where they always lived. The
  proxy is dumb about what tools exist; it just forwards
  ChatRequest payloads.
- Long-term conversation history — the proxy keeps zero state
  beyond what's needed for the current in-flight request.
- User project files / pipelines / meshes / fields. Per the
  "no telemetry beyond opt-in crash reports" promise in
  `docs/COMMUNITY.md`, the proxy sees only the chat messages
  + the tool-name list the user already had on disk.

### 2. Auth — opaque souxmar tokens, not upstream keys

The desktop client never sees an Anthropic / OpenAI API key when
operating in Pro-tier mode. Instead:

- The user signs in to `account.souxmar.dev` (Sprint 17+ wires
  the account portal; for the MVP, tokens come from a CLI
  command).
- The proxy issues a souxmar-scoped opaque token, format
  `sxm_pro_<32-hex-bytes>`. Tokens are revocable from the
  account portal; lifespan = 90 days with sliding refresh on
  every successful `/v1/chat`.
- The desktop client stores the token in the OS keychain
  alongside today's BYOK keys (same `keyring` crate path,
  different service-name string).
- The proxy validates the token + maps to the user's tier
  (Free / Pro / Team / Enterprise) + the user's per-month
  quota balance.

Reasons:

- **Revocability without contacting upstream providers.** A
  compromised souxmar token is rotated by hitting one row in
  our database; rotating an Anthropic key on someone's behalf
  is not a thing we can do.
- **Tier mapping in one place.** Free-tier "managed-AI taster"
  (Sprint 17+ ratchet) needs the same auth pipeline as Pro;
  a tier flag on the token row drives the policy.
- **Billing-event attribution.** Every `/v1/chat` request
  produces a billing event keyed by the token's user_id; no
  reverse-mapping of Anthropic request-ids needed.

### 3. Billing-counter shape — pre-charge per token

Two viable shapes for "how does the proxy bill the user":

- **A) Post-charge per session.** Aggregate the session's
  input + output tokens at chat-end, send one billing event.
- **B) Pre-charge per request, eventually-consistent.** On
  every `/v1/chat`, the proxy:
    1. Estimates the request's max-output tokens from
       `ChatRequest.max_tokens`.
    2. Checks the user's quota balance against
       (input_tokens + max_output_tokens) * tier_price.
    3. Holds the estimate against the balance.
    4. After the upstream provider returns, reconciles to
       the actual `Response.tokens_in/out` counts and
       releases / charges the difference.

This ADR picks **(B)**.

Reasons:

- A user whose Pro tier hit zero balance mid-conversation
  should fail-loud-up-front, not fail-after-the-LLM-already-
  generated-the-reply. Pre-charging catches "you ran out of
  Pro tokens" *before* the upstream API spend happens.
- Session-end aggregation requires the proxy to track
  session state, which conflicts with the
  no-conversation-history rule in § 1.
- The reconciliation step makes the final bill accurate even
  if the user's `max_tokens` was wildly off the actual
  output length.

Counter schema (per-user, persisted in the proxy's DB):

```
{
  "user_id":           "<uuid>",
  "tier":              "free" | "pro" | "team" | "enterprise",
  "balance_tokens":    <int64>,
  "balance_dollars":   <decimal>,
  "last_reset_at":     "<rfc3339>",
  "next_reset_at":     "<rfc3339>",
  "in_flight_holds":   [{ "request_id": "<uuid>", "estimate": <int64> }]
}
```

Stripe webhooks reconcile the dollar balance at the monthly
boundary; the proxy serves `balance_*` to the desktop client's
account panel so the user sees their burn rate inside the app.

### 4. Separate process / repo subtree

The proxy lives at `services/managed-ai-proxy/` in this repo.
Reasons:

- **Shared code with the engine where it matters.** The
  `ChatRequest` / `ChatResponse` types come from
  `include/souxmar/ai/provider.h`; the proxy reads + emits the
  same shapes. A monorepo means a type change is one PR, not a
  cross-repo coordinate.
- **Separate process so a proxy outage doesn't take the
  desktop app down.** When the proxy can't be reached, the
  desktop client falls back to its "no managed-AI available"
  empty state (see § 5).
- **The proxy is deployable as a stateless service.** Kubernetes
  Deployment / Cloud Run / Fly.io are all fine; the
  state-of-record is the database (Postgres) + the upstream
  providers. The Sprint 17+ deployment ADR-0021 fills in the
  hosting choice once we know the early-Pro user load.

The proxy *binary* is **not** shipped to end-users. End-users
get the desktop client; the desktop client either talks to the
hosted proxy at `proxy.souxmar.dev` (Pro tier) or to their own
local Ollama / BYOK keys (Free tier) — never to a local copy of
the proxy.

### 5. Compatibility with BYOK

BYOK stays exactly as it is today. The Pro-tier path is **opt-in
per-project**:

- A new field `ai.provider: "managed" | "byok-anthropic" |
  "byok-openai" | "ollama"` lands on `project.souxmar.toml`.
- Default: `byok-anthropic` (unchanged from Sprint 10).
- The desktop client's chat panel routes per-project: a
  project configured `managed` calls the proxy; a project
  configured `byok-*` uses the existing BYOK keychain
  path.
- The two are mutually exclusive per-project but coexist
  across the user's project list — a user can have a
  managed-AI Pro project alongside a BYOK research project.

When `managed` is configured but the proxy is unreachable, the
chat panel renders an explicit "Pro provider offline; chat
disabled for this project. (Switch to BYOK in Settings →
Provider, or wait for the proxy to come back.)" — *not* a
silent fallback to BYOK, which would surprise the user.

## Consequences

- New `services/managed-ai-proxy/` directory tree (scaffold
  this push):
    - `services/managed-ai-proxy/README.md` — architecture
      summary linking back to this ADR.
    - `services/managed-ai-proxy/api/openapi.yaml` — the v1
      HTTP contract: `/v1/chat`, `/v1/account`, `/v1/quota`.
    - `services/managed-ai-proxy/src/main.rs` — minimal stub
      service (binds, returns 503 with an "MVP — not yet
      implemented" payload). Builds; doesn't ship.
    - `services/managed-ai-proxy/Cargo.toml` — `axum` +
      `tokio` + `serde`. New Rust binary crate, not part of
      the workspace today.
- `include/souxmar/ai/provider.h` gets a queued
  `ManagedProvider` declaration (commented out — lands as a
  real adapter in Sprint 15).
- Sprint 14 push 4 (`provider_call` second real FFI) routes
  via the engine-side Provider abstraction; whether the chosen
  provider is BYOK or Managed is a configuration concern at
  the engine layer, not visible to the desktop client beyond
  the `ai.provider` field.
- Sprint 16's Stripe POC consumes the billing-counter schema
  in § 3.

## Risks

- **R-017 (managed-AI proxy MVP is incomplete by Sprint 14
  retro).** The scaffold lands this push; the real
  implementation accumulates. Likelihood: High; Impact: Low
  (the BYOK path stays the headline user-experience until
  Sprint 17). **Mitigation:** the MVP scaffold returns honest
  503 errors, so a user accidentally pointing at the proxy
  during Sprint 14-15 gets a clear "not yet implemented"
  message, not a silent failure.
- **Token revocation race.** A revoked token might still be
  cached in an in-flight Anthropic call. **Mitigation:**
  upstream calls carry the souxmar token as
  `x-souxmar-request-id`; Sprint 15's proxy.rs aborts
  in-flight upstream futures when the token revokes. Until
  then, the worst case is one extra Anthropic spend before
  the token's revocation propagates — accepted.
- **Per-tier price drift.** The token-to-dollar mapping in §
  3 is per-tier. If upstream prices shift, our pricing breaks.
  **Mitigation:** Sprint 14's MVP hard-codes a single tier's
  rate; Sprint 16+ moves rates into a config table.

## Alternatives considered (and rejected)

- **No proxy at all** — Pro users supply their own keys, we just
  charge a monthly "convenience fee." Rejected: undermines the
  Pro tier's value proposition ("souxmar handles the AI
  account") + leaves users with multiple bills.
- **Each desktop client talks to upstream directly with a
  shared key in CI.** Wildly unsafe (the key would be
  exfiltrable from the binary); rejected.
- **A WebSocket-based proxy with persistent connections.**
  Tempting for streaming, but TCP-keepalive + connection
  pooling complicates auth + horizontal scaling. Sprint 16+
  revisits once we know the per-Pro-user concurrency profile.

## Related ADRs

- ADR-0003 (BYOK as AI default) — the policy this proxy is
  the *alternative* to, not the replacement for.
- ADR-0011 (tool contract v1 final freeze) — the proxy
  forwards ChatRequests whose tool list this contract names;
  it does not redefine the tool set.
- ADR-0016 (BridgeFeatureSet contract) — the
  `provider_call` flag the next push (push 4) flips structural.
- ADR-0018 (libsouxmar-c-bridge) — the FFI surface
  `provider_call` routes through, same template as
  `pipeline_introspection`.
- ADR-0017 (public-alpha bug-discovery model) — proxy
  reachability is a synthetic-load-harness target candidate
  for Sprint 15+ once the proxy has real responses.

— Sprint 14 push 3.
