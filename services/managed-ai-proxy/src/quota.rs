// SPDX-License-Identifier: Apache-2.0
//
// quota.rs — per-user quota counter. Sprint 14 push 3 stub.
//
// Schema matches ADR-0019 § 3. Persistence lands in Sprint 15
// push 3; today the struct exists to anchor the contract and
// keep downstream code typed.

#![allow(dead_code)]

use serde::{Deserialize, Serialize};

/// Per-user counter persisted in the proxy's database.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QuotaCounter {
    pub user_id:          String,
    pub tier:             String,
    pub balance_tokens:   i64,
    pub balance_dollars:  String,   // decimal-as-string so JSON round-trips deterministically
    pub last_reset_at:    String,   // RFC-3339 UTC
    pub next_reset_at:    String,
    pub in_flight_holds:  Vec<InFlightHold>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InFlightHold {
    pub request_id: String,
    pub estimate:   i64,
}

/// Errors the quota tracker raises. Typed enum, no string
/// parsing in callers (same pattern as Sprint 6+ engine code).
#[derive(Debug, Clone)]
pub enum QuotaError {
    InsufficientBalance { requested: i64, available: i64 },
    UserNotFound,
    StorageUnavailable,
}

/// Hold an estimate against the user's balance. Reconciled on
/// `commit_hold` with the actual usage. Sprint 14 push 3 stub
/// always returns `StorageUnavailable` — the proxy returns 503
/// upstream of this anyway.
pub fn hold_estimate(
    _user_id:  &str,
    _estimate: i64,
) -> Result<String /* request_id */, QuotaError> {
    Err(QuotaError::StorageUnavailable)
}

pub fn commit_hold(
    _request_id:  &str,
    _actual:      i64,
) -> Result<(), QuotaError> {
    Err(QuotaError::StorageUnavailable)
}

pub fn release_hold(
    _request_id:  &str,
) -> Result<(), QuotaError> {
    Err(QuotaError::StorageUnavailable)
}
