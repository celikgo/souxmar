# plugin-marketplace — paid-plugin storefront + license verification

Sprint 16 push 2 scaffold. Architecture in
[ADR-0023](../../docs/adr/0023-plugin-marketplace-v1.md).

## What this is

The server-side storefront + license-verification service that
fronts the plugin index (`plugin-index.json` from Sprint 10
push 2). Free plugins continue to publish through the existing
PR-gated workflow; paid plugins join the index via this service
+ a publisher onboarding flow (Sprint 17+'s account portal).

## Endpoints (v1)

See `api/openapi.yaml`:

- `GET  /v1/plugin/<id>` — plugin metadata (manifest url +
  signature url + license requirement + pricing).
- `POST /v1/license/issue` — server-side Stripe webhook
  endpoint. Issues an `sxm_lic_<...>` key + sends it to the
  buyer's email.
- `POST /v1/license/check` — desktop client calls this at
  install time; returns a signed entitlement receipt.
- `GET  /v1/publisher/<id>/keys` — list the publisher's
  last-known signing key fingerprints (for the
  unexpected-rotation review per ADR-0023 R-025).

## Status

**MVP scaffold.** Today's binary binds + returns 503s with
the now-standard "MVP not yet implemented" body. Sprint 16
push 3 wires Stripe; Sprint 16 push 4 wires the CLI side;
Sprint 17 lands the publisher portal.

## Build + run

```sh
cd services/plugin-marketplace
cargo build --release
./target/release/plugin-marketplace --addr 127.0.0.1:8082
```

— Sprint 16 push 2.
