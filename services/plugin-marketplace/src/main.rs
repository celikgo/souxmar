// SPDX-License-Identifier: Apache-2.0
//
// plugin-marketplace — paid-plugin storefront + license verification.
// Sprint 16 push 2 MVP scaffold. ADR-0023.

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
     Json(NotYetImplemented { status: "service_unavailable",
                              code:   "mvp_not_yet_implemented",
                              detail: d }))
}

async fn handle_plugin(Path(id): Path<String>) -> impl IntoResponse {
    info!(id, "GET /v1/plugin/<id> (stub)");
    nyi("plugin-marketplace MVP (Sprint 16 push 2). Sprint 17 lands real storefront.")
}

async fn handle_license_issue() -> impl IntoResponse {
    info!("POST /v1/license/issue (stub)");
    nyi("Sprint 16 push 3 wires Stripe; this endpoint will be the webhook receiver.")
}

async fn handle_license_check() -> impl IntoResponse {
    info!("POST /v1/license/check (stub)");
    nyi("Sprint 16 push 4 wires the CLI side; the entitlement receipt land alongside.")
}

async fn handle_publisher_keys(Path(id): Path<String>) -> impl IntoResponse {
    info!(id, "GET /v1/publisher/<id>/keys (stub)");
    nyi("Sprint 17 wires the publisher portal + the key-fingerprint surface.")
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
    "127.0.0.1:8082".parse().unwrap()
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
        .route("/healthz",                        get(handle_health))
        .route("/v1/plugin/:id",                  get(handle_plugin))
        .route("/v1/license/issue",               post(handle_license_issue))
        .route("/v1/license/check",               post(handle_license_check))
        .route("/v1/publisher/:id/keys",          get(handle_publisher_keys));

    info!(?addr, "plugin-marketplace MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind");
    axum::serve(listener, app).await.expect("serve");
}
