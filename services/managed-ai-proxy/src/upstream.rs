// SPDX-License-Identifier: Apache-2.0
//
// upstream.rs — Anthropic forwarder. Sprint 15 push 3.
//
// First real upstream call from the managed-ai-proxy. Forwards
// the souxmar ChatRequest shape (from openapi.yaml) to Anthropic's
// /v1/messages endpoint, returns the response in our ChatResponse
// shape.
//
// Today this only covers Anthropic. OpenAI lands in Sprint 16+.
// The upstream API key comes from the env var
// `SOUXMAR_PROXY_ANTHROPIC_KEY` (operator-side configuration);
// the user-facing souxmar token (sxm_pro_*) is validated by
// `auth.rs` and never reaches Anthropic.
//
// Errors are typed via UpstreamError → handled by main.rs's
// route handler + mapped to HTTP status codes.

#![allow(dead_code)]

use reqwest::StatusCode;
use serde::{Deserialize, Serialize};
use std::time::Duration;

#[derive(Debug, Clone, Serialize)]
pub struct ChatMessage {
    pub role:    String,    // "user" | "assistant" | "system" | "tool"
    pub content: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct ChatRequest {
    pub model:        String,
    pub messages:     Vec<ChatMessage>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max_tokens:   Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub temperature:  Option<f32>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ChatResponse {
    pub content:    String,
    pub tokens_in:  u64,
    pub tokens_out: u64,
}

#[derive(Debug, Clone)]
pub enum UpstreamError {
    NotConfigured(String),
    Timeout,
    Unauthorized,
    RateLimited,
    BadRequest(String),
    HttpError(u16, String),
    Transport(String),
    MalformedResponse(String),
}

#[derive(Debug, Clone)]
pub enum UpstreamProvider {
    Anthropic,
    OpenAI,
}

/// Forward a ChatRequest to Anthropic /v1/messages.
///
/// Sprint 15 push 3 — first real upstream call. The API key is
/// read from the env at call time, not stashed in the proxy's
/// process state; this matches the no-state policy in ADR-0019.
/// Replacing the key is a deploy-time concern.
pub async fn forward_anthropic(req: ChatRequest) -> Result<ChatResponse, UpstreamError> {
    let key = match std::env::var("SOUXMAR_PROXY_ANTHROPIC_KEY") {
        Ok(k) if !k.is_empty() => k,
        _ => {
            return Err(UpstreamError::NotConfigured(
                "SOUXMAR_PROXY_ANTHROPIC_KEY env var is not set on the proxy".into(),
            ))
        }
    };

    // Anthropic's /v1/messages body shape diverges slightly from
    // ours — they want `system` as a top-level field, not a
    // message with role=system. Translate.
    let (system_prompt, conv) = split_system(req.messages);

    #[derive(Serialize)]
    struct AnthropicReq {
        model: String,
        messages: Vec<ChatMessage>,
        #[serde(skip_serializing_if = "Option::is_none")]
        system: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        max_tokens: Option<u32>,
        #[serde(skip_serializing_if = "Option::is_none")]
        temperature: Option<f32>,
    }

    let body = AnthropicReq {
        model:       req.model,
        messages:    conv,
        system:      system_prompt,
        max_tokens:  req.max_tokens.or(Some(1024)),
        temperature: req.temperature,
    };

    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(60))
        .build()
        .map_err(|e| UpstreamError::Transport(e.to_string()))?;

    let resp = client
        .post("https://api.anthropic.com/v1/messages")
        .header("x-api-key", key)
        .header("anthropic-version", "2023-06-01")
        .header("content-type", "application/json")
        .json(&body)
        .send()
        .await
        .map_err(|e| {
            if e.is_timeout() {
                UpstreamError::Timeout
            } else {
                UpstreamError::Transport(e.to_string())
            }
        })?;

    let status = resp.status();
    if status == StatusCode::UNAUTHORIZED {
        return Err(UpstreamError::Unauthorized);
    }
    if status == StatusCode::TOO_MANY_REQUESTS {
        return Err(UpstreamError::RateLimited);
    }
    if !status.is_success() {
        let body = resp.text().await.unwrap_or_default();
        if status == StatusCode::BAD_REQUEST {
            return Err(UpstreamError::BadRequest(body));
        }
        return Err(UpstreamError::HttpError(status.as_u16(), body));
    }

    #[derive(Deserialize)]
    struct AnthropicResp {
        content: Vec<AnthropicContentBlock>,
        usage:   AnthropicUsage,
    }
    #[derive(Deserialize)]
    struct AnthropicContentBlock {
        #[serde(rename = "type")]
        kind: String,
        text: Option<String>,
    }
    #[derive(Deserialize)]
    struct AnthropicUsage {
        input_tokens:  u64,
        output_tokens: u64,
    }

    let parsed: AnthropicResp = resp
        .json()
        .await
        .map_err(|e| UpstreamError::MalformedResponse(e.to_string()))?;

    let mut content = String::new();
    for block in &parsed.content {
        if block.kind == "text" {
            if let Some(t) = &block.text {
                content.push_str(t);
            }
        }
    }

    Ok(ChatResponse {
        content,
        tokens_in:  parsed.usage.input_tokens,
        tokens_out: parsed.usage.output_tokens,
    })
}

fn split_system(messages: Vec<ChatMessage>) -> (Option<String>, Vec<ChatMessage>) {
    let mut system  = None::<String>;
    let mut others  = Vec::with_capacity(messages.len());
    for m in messages {
        if m.role == "system" {
            // Multiple system messages: join with two newlines.
            system = match system {
                Some(prev) => Some(format!("{}\n\n{}", prev, m.content)),
                None       => Some(m.content),
            };
        } else {
            others.push(m);
        }
    }
    (system, others)
}
