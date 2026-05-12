// SPDX-License-Identifier: Apache-2.0
//
// account-portal — souxmar identity + Stripe checkout. MVP scaffold.
// Sprint 17 push 2. ADR-0026.

use axum::{
    extract::Path,
    http::StatusCode,
    response::{IntoResponse, Json},
    routing::{get, post},
    Router,
};
use serde::Serialize;
use std::net::SocketAddr;
use tracing::info;

#[derive(Serialize)]
struct NotYetImplemented {
    status: &'static str,
    code:   &'static str,
    detail: &'static str,
}

fn nyi(d: &'static str) -> impl IntoResponse {
    (StatusCode::SERVICE_UNAVAILABLE,
     Json(NotYetImplemented {
         status: "service_unavailable",
         code:   "mvp_not_yet_implemented",
         detail: d,
     }))
}

async fn handle_email_link() -> impl IntoResponse {
    info!("POST /v1/auth/email-link (stub)");
    nyi("account-portal MVP scaffold (Sprint 17 push 2). Postgres + Postmark wire-up: Sprint 18 push 1.")
}

async fn handle_email_link_resolve() -> impl IntoResponse {
    info!("POST /v1/auth/email-link/resolve (stub)");
    nyi("Sprint 18 push 1 wires the email-link resolver.")
}

async fn handle_me() -> impl IntoResponse {
    info!("GET /v1/me (stub)");
    nyi("Sprint 18 push 1 wires the user lookup.")
}

async fn handle_token_issue(Path(namespace): Path<String>) -> impl IntoResponse {
    info!(namespace, "POST /v1/me/tokens/.../issue (stub)");
    nyi("Sprint 18 push 1 wires token issuance.")
}

async fn handle_token_revoke(Path(namespace): Path<String>) -> impl IntoResponse {
    info!(namespace, "POST /v1/me/tokens/.../revoke (stub)");
    nyi("Sprint 18 push 1 wires token revocation.")
}

async fn handle_billing_link() -> impl IntoResponse {
    info!("GET /v1/me/billing-link (stub)");
    nyi("Sprint 18 push 2 wires the Stripe.js checkout session creation.")
}

async fn handle_health() -> impl IntoResponse {
    (StatusCode::OK, "ok")
}

fn parse_addr(args: &[String]) -> SocketAddr {
    for (i, a) in args.iter().enumerate() {
        if a == "--addr" {
            if let Some(v) = args.get(i + 1) {
                return v.parse::<SocketAddr>().expect("--addr must be a valid socket address");
            }
        }
    }
    "127.0.0.1:8084".parse().unwrap()
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
        .route("/healthz",                            get(handle_health))
        .route("/v1/auth/email-link",                 post(handle_email_link))
        .route("/v1/auth/email-link/resolve",         post(handle_email_link_resolve))
        .route("/v1/me",                              get(handle_me))
        .route("/v1/me/tokens/:namespace/issue",      post(handle_token_issue))
        .route("/v1/me/tokens/:namespace/revoke",     post(handle_token_revoke))
        .route("/v1/me/billing-link",                 get(handle_billing_link));

    info!(?addr, "account-portal MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind");
    axum::serve(listener, app).await.expect("serve");
}
