// SPDX-License-Identifier: Apache-2.0
//
// managed-ai-proxy — souxmar Pro-tier AI gateway, MVP scaffold.
// Sprint 14 push 3. ADR-0019 documents the architecture.
//
// What this binary does today:
//   - Binds on `--addr` (default 127.0.0.1:8080).
//   - Responds 503 with an honest "MVP — not yet implemented"
//     JSON body to every documented endpoint.
//   - Logs request/response via `tracing`.
//
// What this binary does NOT yet do:
//   - Reach Anthropic / OpenAI (Sprint 15).
//   - Persist a quota counter (Sprint 15).
//   - Emit Stripe billing events (Sprint 16).
//   - Validate real tokens (Sprint 17). The auth stub accepts
//     any `sxm_pro_*`-prefixed Bearer token.
//
// The honest 503 is the load-bearing artefact at MVP — a Pro-
// tier desktop client pointed here gets the "Pro provider
// offline; switch to BYOK" empty state the chat panel was
// designed to render. Silent fallback would surprise the user.

mod auth;
mod quota;
mod upstream;

use axum::{
    extract::Request,
    http::StatusCode,
    response::{IntoResponse, Json},
    routing::{get, post},
    Router,
};
use serde::{Deserialize, Serialize};
use std::net::SocketAddr;
use tracing::{info, warn};

use crate::upstream::{forward_anthropic, ChatMessage, ChatRequest, UpstreamError};

#[derive(Serialize)]
struct NotYetImplemented {
    status: &'static str,
    code:   &'static str,
    detail: &'static str,
}

fn not_yet_implemented(endpoint: &'static str) -> impl IntoResponse {
    (
        StatusCode::SERVICE_UNAVAILABLE,
        Json(NotYetImplemented {
            status: "service_unavailable",
            code:   "mvp_not_yet_implemented",
            detail: endpoint,
        }),
    )
}

#[derive(Deserialize)]
struct ChatBody {
    model:        String,
    messages:     Vec<ChatMessage>,
    #[serde(default)]
    max_tokens:   Option<u32>,
    #[serde(default)]
    temperature:  Option<f32>,
}

#[derive(Serialize)]
struct ChatProblem {
    status: &'static str,
    code:   &'static str,
    detail: String,
}

async fn handle_chat(Json(body): Json<ChatBody>) -> impl IntoResponse {
    info!(model = %body.model, msg_count = body.messages.len(), "/v1/chat");

    let req = ChatRequest {
        model:       body.model,
        messages:    body.messages,
        max_tokens:  body.max_tokens,
        temperature: body.temperature,
    };

    match forward_anthropic(req).await {
        Ok(resp) => (StatusCode::OK, Json(resp)).into_response(),
        Err(e) => {
            warn!(error = ?e, "anthropic forward failed");
            let (status, code, detail) = upstream_to_http(&e);
            (
                status,
                Json(ChatProblem { status: "error", code, detail }),
            )
                .into_response()
        }
    }
}

fn upstream_to_http(e: &UpstreamError) -> (StatusCode, &'static str, String) {
    use UpstreamError::*;
    match e {
        NotConfigured(d)   => (StatusCode::SERVICE_UNAVAILABLE, "not_configured",   d.clone()),
        Timeout            => (StatusCode::GATEWAY_TIMEOUT,     "upstream_timeout", String::new()),
        Unauthorized       => (StatusCode::BAD_GATEWAY,         "upstream_unauthorized", "the proxy's upstream key is invalid; the user's souxmar token is unaffected".into()),
        RateLimited        => (StatusCode::TOO_MANY_REQUESTS,   "upstream_rate_limited", String::new()),
        BadRequest(d)      => (StatusCode::BAD_REQUEST,         "upstream_bad_request",  d.clone()),
        HttpError(code, d) => (StatusCode::BAD_GATEWAY,         "upstream_http_error", format!("status={}, body={}", code, d)),
        Transport(d)       => (StatusCode::BAD_GATEWAY,         "upstream_transport",  d.clone()),
        MalformedResponse(d) => (StatusCode::BAD_GATEWAY,       "upstream_malformed",  d.clone()),
    }
}

async fn handle_account(_req: Request) -> impl IntoResponse {
    info!("/v1/account request received (stub)");
    not_yet_implemented(
        "managed-ai-proxy MVP scaffold (Sprint 14 push 3). \
         The /v1/account endpoint is documented in ADR-0019; \
         the account portal lands in Sprint 17.",
    )
}

async fn handle_quota(_req: Request) -> impl IntoResponse {
    info!("/v1/quota request received (stub)");
    not_yet_implemented(
        "managed-ai-proxy MVP scaffold (Sprint 14 push 3). \
         The /v1/quota counter is documented in ADR-0019 § 3; \
         the counter persistence lands in Sprint 15 push 3.",
    )
}

async fn handle_health() -> impl IntoResponse {
    // Health probe — separate from the v1 surface so a deploy
    // orchestrator can verify the binary is alive without
    // tripping the 503 contract.
    (StatusCode::OK, "ok")
}

fn parse_addr(args: &[String]) -> SocketAddr {
    for (i, a) in args.iter().enumerate() {
        if a == "--addr" {
            if let Some(v) = args.get(i + 1) {
                return v.parse::<SocketAddr>()
                    .expect("--addr must be a valid socket address");
            }
        }
    }
    "127.0.0.1:8080".parse().unwrap()
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info".into()),
        )
        .init();

    let args: Vec<String> = std::env::args().collect();
    let addr = parse_addr(&args);

    let app = Router::new()
        .route("/healthz",    get(handle_health))
        .route("/v1/chat",    post(handle_chat))
        .route("/v1/account", get(handle_account))
        .route("/v1/quota",   get(handle_quota));

    info!(?addr, "managed-ai-proxy MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr)
        .await
        .expect("failed to bind");
    axum::serve(listener, app)
        .await
        .expect("axum serve failed");
}
