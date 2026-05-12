// SPDX-License-Identifier: Apache-2.0
//
// auth.rs — token validation. Sprint 14 push 3 stub.
//
// The MVP accepts any Bearer token prefixed `sxm_pro_`. Sprint
// 17 lands real validation against the account portal's
// issuance database. ADR-0019 § 2 documents the design.

#![allow(dead_code)]

#[derive(Debug, Clone)]
pub struct AuthDecision {
    pub user_id: String,
    pub tier:    Tier,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Tier {
    Free,
    Pro,
    Team,
    Enterprise,
}

#[derive(Debug, Clone)]
pub enum AuthError {
    Missing,
    Malformed,
    Revoked,
    Expired,
}

pub fn check_token(token: &str) -> Result<AuthDecision, AuthError> {
    if token.is_empty() {
        return Err(AuthError::Missing);
    }
    if !token.starts_with("sxm_pro_") {
        return Err(AuthError::Malformed);
    }
    // Sprint 17+ replaces this with a real DB lookup. Today's
    // stub returns a synthetic "pro-tier user" so downstream
    // code can flow through the happy path during local dev.
    Ok(AuthDecision {
        user_id: format!("stub-user-{}", &token["sxm_pro_".len()..token.len().min(16)]),
        tier:    Tier::Pro,
    })
}
