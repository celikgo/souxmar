# account-portal — souxmar identity + Stripe checkout

Sprint 17 push 2 scaffold. ADR-0026.

## What this is

`account.souxmar.dev` — the source of truth for souxmar user
identities + token issuance. Hosts the Stripe.js checkout
iframe for subscriptions + paid-plugin purchases. Consumed by
the proxy + marketplace + cloud-sync's `auth.rs` modules
(Sprint 17 push 3+ wires the validators).

## Endpoints (v1)

See `api/openapi.yaml`:

- `POST /v1/auth/email-link`         — request a sign-in email.
- `POST /v1/auth/email-link/resolve` — exchange the link's
  one-time code for a session.
- `GET  /v1/me`                       — current user + token list.
- `POST /v1/me/tokens/{namespace}/issue`  — issue a new
  `sxm_<namespace>_<hex>` token (pro / lic / sync).
- `POST /v1/me/tokens/{namespace}/revoke` — revoke an existing
  token.
- `GET  /v1/me/billing-link`          — return a Stripe.js
  checkout session URL for the desktop client to embed.

## Status

MVP scaffold. Today's binary binds + 503s on every endpoint.
Sprint 18 push 1 wires the Postgres user table; Sprint 22
public-beta runs in test mode for ≥ 4 weeks before live.

## Token namespaces

| Namespace  | Prefix          | Issued for                              |
| ---------- | --------------- | --------------------------------------- |
| pro        | `sxm_pro_*`     | managed-AI proxy access                 |
| lic        | `sxm_lic_*`     | paid-plugin license entitlement         |
| sync       | `sxm_sync_*`    | cloud-sync per-device token             |

Each is independent — revoking a sync token from the portal
doesn't affect the user's proxy token. Useful for "lost
laptop" recovery without unwinding the user's chat history.

## Tech stack

- axum + tokio (consistent with the other services).
- Postgres + sqlx (Sprint 18 push 1 wires).
- HTMX + minimal vanilla JS. No SPA — the portal is ~5 pages.
- Postmark transactional email (operator-side config).

— Sprint 17 push 2.
