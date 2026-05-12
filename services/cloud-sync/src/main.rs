// SPDX-License-Identifier: Apache-2.0
//
// cloud-sync — souxmar project synchronisation, MVP scaffold.
// Sprint 15 push 3. ADR-0021 documents the architecture.
//
// Returns 503 with an honest "MVP not yet implemented" body on
// every v1 endpoint. Sprint 16 push 1 lands the S3-backed write
// path. Same shape as the managed-AI proxy's Sprint 14 push 3
// scaffold.

use axum::{
    extract::Path,
    http::StatusCode,
    response::{IntoResponse, Json},
    routing::{get, put},
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

async fn put_file(Path((project_id, file_name)): Path<(String, String)>) -> impl IntoResponse {
    info!(project_id, file_name, "PUT /v1/project/.../file/... (stub)");
    not_yet_implemented(
        "cloud-sync MVP scaffold (Sprint 15 push 3). The write \
         path lands in Sprint 16 push 1; ADR-0021 documents the \
         architecture.",
    )
}

async fn get_file(Path((project_id, file_name)): Path<(String, String)>) -> impl IntoResponse {
    info!(project_id, file_name, "GET /v1/project/.../file/... (stub)");
    not_yet_implemented(
        "cloud-sync MVP scaffold (Sprint 15 push 3). The read \
         path lands in Sprint 16 push 1.",
    )
}

async fn get_manifest(Path(project_id): Path<String>) -> impl IntoResponse {
    info!(project_id, "GET /v1/project/.../manifest (stub)");
    not_yet_implemented(
        "cloud-sync MVP scaffold (Sprint 15 push 3). Per-file \
         server timestamps drive Sprint 17's conflict panel.",
    )
}

async fn handle_health() -> impl IntoResponse {
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
    "127.0.0.1:8081".parse().unwrap()
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
        .route("/healthz", get(handle_health))
        .route("/v1/project/:project_id/file/:file_name", put(put_file).get(get_file))
        .route("/v1/project/:project_id/manifest", get(get_manifest));

    info!(?addr, "cloud-sync MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr)
        .await
        .expect("failed to bind");
    axum::serve(listener, app)
        .await
        .expect("axum serve failed");
}
