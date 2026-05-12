// SPDX-License-Identifier: Apache-2.0
//
// upstream.rs — Anthropic / OpenAI forwarder. Sprint 14 push 3
// stub. Real implementation lands in Sprint 15 push 1.

#![allow(dead_code)]

#[derive(Debug, Clone)]
pub enum UpstreamProvider {
    Anthropic,
    OpenAI,
}

#[derive(Debug, Clone)]
pub enum UpstreamError {
    NotYetImplemented,
    Timeout,
    Unauthorized,
    RateLimited,
    BadRequest(String),
    HttpError(u16),
}

pub fn forward_chat_stub() -> Result<(), UpstreamError> {
    Err(UpstreamError::NotYetImplemented)
}
