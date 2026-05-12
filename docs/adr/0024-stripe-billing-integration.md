# ADR-0024: Stripe billing — unified customer record across AI proxy + marketplace

- **Status:** Accepted
- **Date:** 2026-05-14 (Sprint 16 push 3)
- **Author:** souxmar platform team
- **Deciders:** platform, billing, security review
- **Tier:** 1 (architecture)
- **Affects:** new `services/billing/`; Sprint 14 push 3's
  managed-ai-proxy quota counter; Sprint 16 push 2's
  plugin-marketplace license issuance.

## Context

Sprint 14 push 3's ADR-0019 left the proxy's pre-charge token
model in place but punted Stripe to "Sprint 16." Sprint 16 push
2's ADR-0023 added paid plugins. Both bill the same user from
the same Stripe customer record — but the two services were
designed independently. This ADR ratifies the shared shape so
they don't diverge.

## Decision

### Single billing service mediates Stripe

A new `services/billing/` service:

- Subscribes to all relevant Stripe webhooks (`customer.created`,
  `customer.subscription.*`, `invoice.payment_succeeded`,
  `charge.refunded`).
- Owns the canonical user-id ↔ Stripe-customer-id mapping in
  Postgres.
- Exposes two internal-only endpoints:
    - `POST /internal/quota/refill` — proxy calls this when
      a Pro user's monthly token allotment resets.
    - `POST /internal/license/issue` — marketplace calls this
      when a chargeback unwinds + license needs revocation.
- Emits structured events to a Kafka topic that the proxy +
  marketplace consume for quota / license updates.

The proxy and marketplace **don't talk to Stripe directly**.
This keeps the Stripe key in one process; the other services
hold only internal tokens.

### Subscription plans

- **Pro:** $30/mo. Includes 10M tokens of managed-AI usage.
  Overage at $2 per 1M tokens. Cloud sync 1GB.
- **Team:** $100/seat/mo. Same managed-AI as Pro per seat.
  Cloud sync 10GB per seat, shared team namespace.
- **Enterprise:** custom pricing. Includes Pro features +
  E2E cloud sync + SSO/SCIM (Sprint 20) + dedicated upstream
  AI capacity (Sprint 21).

Paid-plugin purchases are separate one-time / monthly line
items on the same invoice.

### Test mode vs live mode

The billing service runs in three modes via
`SOUXMAR_BILLING_MODE` env:

- `disabled` — every endpoint returns 503. Default in dev.
- `test`     — Stripe test keys. CI runs in this mode.
- `live`     — real keys. Operator-only.

Mode is a load-bearing setting because mis-pointing a test
client at a live key creates real charges. The service refuses
to start with both `SOUXMAR_BILLING_MODE=live` and a `sk_test_*`
key (or vice versa).

### What the desktop client sees

The desktop client never sees a Stripe key. The account portal
(Sprint 17) embeds a Stripe.js checkout iframe; the user enters
card details directly to Stripe; the portal receives the
`customer_id` via the success redirect; the billing service is
told via webhook.

The user's invoice link is surfaced through `souxmar account
billing-portal` (CLI) + the desktop's Account → Billing panel
(Sprint 17).

## Consequences

- New `services/billing/` directory; same axum scaffold.
- Sprint 17's account portal integrates Stripe.js + the
  billing service's `/v1/account/billing-link` endpoint.
- The proxy's quota counter (Sprint 15+'s in-flight holds)
  consults the billing service's Kafka events for the
  monthly reset.
- Sprint 22's public beta exercises the full path in
  Stripe test mode for at least 4 weeks before flipping live.

## Risks

- **R-028 (Stripe key leak).** A single billing service
  process holding the live key is a single point of
  failure. **Mitigation:** the live key lives in the
  deployment platform's secret manager (AWS Secrets
  Manager / GCP Secret Manager); the process reads it at
  startup; rotating the key is a deploy operation.
- **R-029 (webhook replay).** Stripe's webhook signature
  validates source; an in-process replay would re-issue
  licenses / refill quotas. **Mitigation:** Stripe webhook
  events carry an idempotency id; the billing service
  records consumed ids in a 30-day-TTL table.
- **R-030 (test/live mode mix-up).** As named. **Mitigation:**
  the mode/key consistency check at startup.

— Sprint 16 push 3.
