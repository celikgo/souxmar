# ADR-0026: Account portal — souxmar identity + Stripe checkout integration

- **Status:** Accepted
- **Date:** 2026-05-15 (Sprint 17 push 2)
- **Author:** souxmar platform team
- **Deciders:** platform, billing, security review
- **Tier:** 1 (architecture)
- **Affects:** new `services/account-portal/`; `auth.rs` in the
  proxy + marketplace become real (read tokens from the portal's
  DB); the desktop client's Account → Billing surface.

## Context

ADR-0019 (managed-AI proxy) named opaque `sxm_pro_<...>` tokens
as the auth model + deferred the account portal to Sprint 17.
ADR-0024 (Stripe billing) named the portal as the surface that
embeds Stripe.js checkout. Sprint 17 push 2 (this push) lands
the scaffold + ratifies the architecture.

## Decision

### What the portal contains

`account.souxmar.dev` — a small web app + API:

- Sign-up + sign-in (email-link OAuth; no password; sketched
  in the portal scaffold's openapi.yaml).
- Token issuance + revocation (the canonical store the proxy +
  marketplace's `auth.rs` modules consult).
- Stripe.js checkout iframe for subscription + paid-plugin
  purchases.
- Per-user surfaces: list of issued tokens, plugin licenses,
  cloud-sync project count, invoice history.

### Token shapes

- `sxm_pro_<32-hex>`     — managed-AI proxy access.
- `sxm_lic_<32-hex>`     — paid-plugin license entitlement.
- `sxm_sync_<32-hex>`    — cloud-sync per-device token.

Each token namespace is independent. A user can revoke their
sync token from the portal without affecting their proxy
token; useful for "lost laptop" recovery.

### What the portal does NOT do

- Doesn't run upstream calls (Anthropic / OpenAI). The proxy
  does that.
- Doesn't store plugin payloads. The marketplace does that.
- Doesn't run a CDN for the install path. Each service owns
  its own data plane.

### Tech stack

- Server: Rust + axum (consistent with the other services).
- Database: Postgres with sqlx. The user table is the
  source of truth for all four service-side auth.rs
  validators.
- Email: Postmark transactional API (operator-side config).
- Frontend: HTMX + minimal vanilla JS. No SPA. The portal
  is a thin surface; full SPA infrastructure is overkill for
  ~5 pages.

## Consequences

- New `services/account-portal/` (axum binary + html templates).
- Sprint 17 push 3+'s proxy/marketplace auth.rs modules read
  from the portal's Postgres user table.
- Sprint 22's public beta requires the portal live in
  test mode for ≥ 4 weeks before flipping production.
- Sprint 20+'s SSO/SCIM integration extends this portal with
  enterprise IdP login flows.

## Risks

- **R-031 (single point of failure for all four service-side
  auth.rs validators).** A portal outage takes down quota
  enforcement, license validation, sync uploads in one go.
  **Mitigation:** the validators cache the user-id ↔ token
  mapping for 15 minutes; a portal outage of < 15min is
  invisible to live users. **Likelihood: Low; Impact: Med.**
- **R-032 (email-link OAuth phishing).** A user clicking a
  spoofed email-link can be tricked into authorising a
  third-party. **Mitigation:** the link includes an
  origin-bound CSRF token that the portal validates on
  resolve; expired after 10 minutes. **Likelihood: Med;
  Impact: Med.** Sprint 21's pen-test exercises this.

— Sprint 17 push 2.
