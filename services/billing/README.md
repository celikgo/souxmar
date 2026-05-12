# billing — Stripe gateway

Sprint 16 push 3 scaffold. ADR-0024 documents the architecture.

## What this is

The only process that holds the Stripe API key. Owns the
user-id ↔ Stripe-customer-id mapping (Postgres). Subscribes to
Stripe webhooks; emits structured events to a Kafka topic that
the proxy + marketplace consume.

## Modes

`SOUXMAR_BILLING_MODE` env:

- `disabled` — 503 on every endpoint. Default in dev.
- `test`     — Stripe test keys; CI runs in this mode.
- `live`     — real keys; operator-only.

The mode/key consistency check at startup refuses to run in
`live` with a `sk_test_*` key (or vice versa).

## Endpoints

Public:
- `POST /v1/webhook/stripe` — Stripe webhook receiver. Signature
  verified per Stripe-Signature header.

Internal (other souxmar services only, mTLS or VPC-internal):
- `POST /internal/quota/refill` — called by the proxy at month
  rollover.
- `POST /internal/license/issue` — called by the marketplace
  after a successful payment-intent + before issuing
  `sxm_lic_<...>`.

The desktop client and CLI never talk to this service.

## Status

MVP scaffold. Sprint 22 (public beta) runs the full path in
test mode for ≥ 4 weeks before flipping live.

— Sprint 16 push 3.
