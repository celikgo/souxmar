# managed-ai-proxy — Pro-tier server-side AI gateway

Sprint 14 push 3 scaffold. Architecture is named in
[ADR-0019](../../docs/adr/0019-managed-ai-proxy-architecture.md);
this README is the operator's quick reference.

## What this is

A stateless HTTP service that fronts Anthropic / OpenAI for
souxmar Pro-tier users. The desktop client speaks the same
`ChatRequest` / `ChatResponse` shapes (from
`include/souxmar/ai/provider.h`) as it does to BYOK Anthropic /
OpenAI / Ollama; the proxy is one more `Provider`
implementation (Sprint 15 lands `ManagedProvider` as a real
adapter).

## What this is NOT

- Not shipped to end-users. End-users get the desktop client.
- Not a replacement for BYOK; the two coexist per-project.
- Not the agent tool dispatcher — tools live on the desktop
  client; the proxy is dumb about what tools exist.

## Endpoints (v1)

See `api/openapi.yaml` for the full schema. At MVP:

- `POST /v1/chat` — forward a ChatRequest to the upstream
  provider; return the ChatResponse.
- `GET  /v1/account` — return the user's tier + the email
  address the token is bound to.
- `GET  /v1/quota` — return the per-user counter shape from
  ADR-0019 § 3.

Auth: opaque souxmar token in `Authorization: Bearer sxm_pro_...`
format. Tokens issued at sign-up; revocable from the account
portal (Sprint 17+).

## Status

**MVP scaffold.** Today's binary binds + returns 503 with an
"MVP — not yet implemented" body for every endpoint. The 503
shape is honest: a Pro-tier client pointed here gets a clear
"Pro provider offline; switch to BYOK" prompt, not a silent
failure.

Sprint 15 push 1 lands the upstream Anthropic forwarder; Sprint
15 push 3 lands the quota counter + Stripe billing-event
emitter; Sprint 16 wires Stripe end-to-end. Sprint 17+ stands
up the account portal.

## Build + run

```sh
cd services/managed-ai-proxy
cargo build --release
./target/release/managed-ai-proxy --addr 127.0.0.1:8080
```

The binary is **not** part of the workspace today — it builds
out-of-tree on purpose so the desktop client's CI doesn't pull
the server's deps. Sprint 17+'s deployment ADR-0021 picks the
hosting target (Cloud Run / Fly.io / Kubernetes).

## Deployment posture

- Stateless except for the DB connection (Postgres at MVP;
  ADR-0021 ratifies the hosting choice).
- Logs to stdout; a deployment-side aggregator (Datadog,
  Grafana Loki, etc.) is the operator's choice.
- TLS is the load-balancer's job; the binary speaks plain HTTP.
- Zero-downtime deploys via the standard "drain in-flight,
  switch traffic, drain old" pattern — no in-process state to
  migrate.

## Why a separate Rust binary, not a Python or Node service?

- The team already knows Rust (`souxmar-bridge` crate).
- `axum` + `tokio` give a small, well-tested HTTP stack with no
  external runtime to install.
- Shared types with the engine — when the engine's
  `ChatRequest` schema grows a field, the proxy picks it up
  via the OpenAPI generator (Sprint 15 push 1 wires that).

## Source layout

```
services/managed-ai-proxy/
├── README.md            # this file
├── Cargo.toml           # workspace-independent binary
├── api/openapi.yaml     # v1 HTTP contract
└── src/
    ├── main.rs          # bind + serve
    ├── auth.rs          # token validation stub
    ├── upstream.rs      # Anthropic client stub
    └── quota.rs         # counter schema + in-flight holds stub
```

## Sprint 14 acceptance

This push lands the scaffold and the ADR. The acceptance bar
for Sprint 14 is:

- `cargo build` succeeds locally.
- The binary binds + responds 503 to every endpoint with an
  "MVP — not yet implemented" JSON body.
- The contract in `api/openapi.yaml` matches the request /
  response shapes the engine's `provider.h` defines today.

It does **not** yet:

- Reach Anthropic — that's Sprint 15.
- Persist the quota counter — that's Sprint 15.
- Emit Stripe events — that's Sprint 16.
- Authenticate real tokens (the stub accepts any
  `sxm_pro_*`-prefixed string) — that's Sprint 17.

— Sprint 14 push 3.
