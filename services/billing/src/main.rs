// SPDX-License-Identifier: Apache-2.0
//
// billing — Stripe gateway. Sprint 16 push 3 scaffold. ADR-0024.

use axum::{
    http::StatusCode,
    response::{IntoResponse, Json},
    routing::{get, post},
    Router,
};
use serde::Serialize;
use std::net::SocketAddr;
use tracing::{info, warn};

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

async fn handle_webhook() -> impl IntoResponse {
    info!("POST /v1/webhook/stripe (stub)");
    nyi("billing MVP scaffold (Sprint 16 push 3). Sprint 22 public-beta runs this in test mode for >= 4 weeks before live.")
}

async fn handle_quota_refill() -> impl IntoResponse {
    info!("POST /internal/quota/refill (stub)");
    nyi("internal endpoint — proxy calls at month rollover. Sprint 17 wires.")
}

async fn handle_license_issue() -> impl IntoResponse {
    info!("POST /internal/license/issue (stub)");
    nyi("internal endpoint — marketplace calls after payment-intent. Sprint 17 wires.")
}

async fn handle_health() -> impl IntoResponse {
    (StatusCode::OK, "ok")
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Mode {
    Disabled,
    Test,
    Live,
}

fn parse_mode() -> Mode {
    match std::env::var("SOUXMAR_BILLING_MODE").as_deref() {
        Ok("live") => Mode::Live,
        Ok("test") => Mode::Test,
        _          => Mode::Disabled,
    }
}

fn key_consistency_check(mode: Mode) {
    let key = std::env::var("SOUXMAR_STRIPE_API_KEY").unwrap_or_default();
    match mode {
        Mode::Live if key.starts_with("sk_test_") => {
            panic!("SOUXMAR_BILLING_MODE=live but key is sk_test_*; refusing to start");
        }
        Mode::Test if key.starts_with("sk_live_") => {
            panic!("SOUXMAR_BILLING_MODE=test but key is sk_live_*; refusing to start");
        }
        Mode::Disabled if !key.is_empty() => {
            warn!("SOUXMAR_BILLING_MODE=disabled but a key is set; key will be ignored");
        }
        _ => {}
    }
}

fn parse_addr(args: &[String]) -> SocketAddr {
    for (i, a) in args.iter().enumerate() {
        if a == "--addr" {
            if let Some(v) = args.get(i + 1) {
                return v.parse::<SocketAddr>().expect("--addr must be a valid socket address");
            }
        }
    }
    "127.0.0.1:8083".parse().unwrap()
}

#[cfg(test)]
mod tests {
    use super::*;

    // Sprint 17 push 1 — unit test the mode/key consistency
    // check (ADR-0024 R-030 mitigation). The check panics on
    // the failure mode that would create real charges in test
    // (mode=live + sk_test_*); we verify the panic fires.

    #[test]
    fn parse_mode_disabled_by_default() {
        std::env::remove_var("SOUXMAR_BILLING_MODE");
        assert_eq!(parse_mode(), Mode::Disabled);
    }

    #[test]
    #[should_panic(expected = "refusing to start")]
    fn live_mode_with_test_key_panics() {
        std::env::set_var("SOUXMAR_BILLING_MODE", "live");
        std::env::set_var("SOUXMAR_STRIPE_API_KEY", "sk_test_abc");
        key_consistency_check(Mode::Live);
    }

    #[test]
    #[should_panic(expected = "refusing to start")]
    fn test_mode_with_live_key_panics() {
        std::env::set_var("SOUXMAR_BILLING_MODE", "test");
        std::env::set_var("SOUXMAR_STRIPE_API_KEY", "sk_live_abc");
        key_consistency_check(Mode::Test);
    }

    #[test]
    fn test_mode_with_test_key_is_fine() {
        std::env::set_var("SOUXMAR_BILLING_MODE", "test");
        std::env::set_var("SOUXMAR_STRIPE_API_KEY", "sk_test_xyz");
        key_consistency_check(Mode::Test);  // no panic
    }
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info".into()),
        )
        .init();

    let mode = parse_mode();
    key_consistency_check(mode);
    info!(?mode, "billing service starting");

    let args: Vec<String> = std::env::args().collect();
    let addr = parse_addr(&args);

    let app = Router::new()
        .route("/healthz",                  get(handle_health))
        .route("/v1/webhook/stripe",        post(handle_webhook))
        .route("/internal/quota/refill",    post(handle_quota_refill))
        .route("/internal/license/issue",   post(handle_license_issue));

    info!(?addr, "billing MVP scaffold — binding");
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind");
    axum::serve(listener, app).await.expect("serve");
}
