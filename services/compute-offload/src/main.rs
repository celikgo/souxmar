// SPDX-License-Identifier: Apache-2.0
//
// compute-offload — Pro-tier hosted-compute job queue. MVP scaffold.
// Sprint 17 push 3. ADR-0027.

use axum::{
    extract::Path,
    http::StatusCode,
    response::{IntoResponse, Json},
    routing::{delete, get, post},
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

async fn handle_submit() -> impl IntoResponse {
    info!("POST /v1/jobs (stub)");
    nyi("compute-offload MVP scaffold (Sprint 17 push 3). Sprint 19 push 2 lands the job queue + worker pool.")
}

async fn handle_poll(Path(id): Path<String>) -> impl IntoResponse {
    info!(id, "GET /v1/jobs/<id> (stub)");
    nyi("Sprint 19 push 2 wires status polling + result download.")
}

async fn handle_cancel(Path(id): Path<String>) -> impl IntoResponse {
    info!(id, "DELETE /v1/jobs/<id> (stub)");
    nyi("Sprint 19 push 2 wires cancellation + the billing in-flight-hold release per ADR-0024.")
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
    "127.0.0.1:8085".parse().unwrap()
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
        .route("/healthz",        get(handle_health))
        .route("/v1/jobs",        post(handle_submit))
        .route("/v1/jobs/:id",    get(handle_poll))
        .route("/v1/jobs/:id",    delete(handle_cancel));

    info!(?addr, "compute-offload MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind");
    axum::serve(listener, app).await.expect("serve");
}
