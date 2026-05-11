# ADR-0003: BYOK is the default AI configuration; managed AI is the paid upgrade

- **Status:** Accepted
- **Date:** 2026-05-11
- **Author:** Founders
- **Deciders:** All maintainers
- **Tier:** 3
- **Affects:** AI, BUSINESS_MODEL, security, governance

## Context

The desktop app's chat panel needs to talk to a large-language-model provider. There are two coherent ways to ship this:

1. **Managed-first:** souxmar holds API keys; the user gets a free-trial allowance and pays a subscription for sustained usage. The user never sees a key.
2. **BYOK-first:** the user supplies their own Anthropic / OpenAI / local-Ollama key; their requests go directly to the provider; souxmar's servers are not in the path. A managed option exists as a paid convenience tier.

This decision interacts with the business model, the security posture, the trust commitment to regulated industries, and the operational complexity of running the service.

## Decision

souxmar **defaults to BYOK** at install. The user is prompted on first chat use to supply a provider key (Anthropic recommended, OpenAI and local Ollama also supported). Keys are stored in the OS keychain. Network calls go directly from the user's machine to the provider; no souxmar server is in the path.

A managed-AI option is offered as part of the Pro tier (`BUSINESS_MODEL.md`), which proxies requests through souxmar infrastructure, handles key rotation, and bills monthly. Managed AI is opt-in; BYOK never goes away.

## Alternatives considered

### Managed-AI default with a free-trial allowance

The standard SaaS pattern. **Rejected.** Three problems:

- It requires us to operate a billing-grade proxy on day 1, which is a meaningful infrastructure ramp before we have product-market fit.
- It conflicts with the "free desktop app, no telemetry, no phoning home" trust commitment to regulated-industry users (aerospace, defence, ITAR-controlled work). A user who must run air-gapped cannot use a managed-AI default; we would functionally cripple the free tier for them.
- It constrains our pricing model to "pay-as-you-go before you can try it," which is a worse onboarding than Cursor's BYOK free-tier-from-day-1.

### No AI at all in the open-source desktop; AI only in the paid version

**Rejected.** This is the open-core anti-pattern we explicitly reject in `BUSINESS_MODEL.md`. Moving the AI feature behind a paywall after telling the world the desktop is open source destroys the trust premise of the project.

### BYOK only; no managed offering

**Rejected.** A meaningful slice of users (smaller engineering teams, individual professional engineers) would prefer to pay for the convenience of not handling keys, and that revenue is the cleanest funding source for the open-source core. Closing the door on managed AI gives up money we should take.

### Anthropic-only

Pick one provider and avoid the abstraction. **Rejected.** Provider concentration risk is too high; OpenAI parity is required from day 1; local-Ollama option is required for air-gapped users. The provider-abstraction cost is small (one interface, three implementations) and the option value is large.

## Consequences

### Positive

- The free desktop app is genuinely free and genuinely complete. No nag screens, no upsell modals, no functional restrictions.
- Regulated-industry users can deploy souxmar without souxmar's servers being in their data-flow path. Procurement-tractable.
- Day-1 launch does not require billing infrastructure or a proxy fleet.
- Multiple-provider support keeps us insulated from any one vendor's pricing or policy changes.
- The managed-AI Pro tier becomes a clean, value-aligned upgrade — convenience and cost-amortisation, not feature unlock.

### Negative

- Onboarding has an extra step (paste a key) that managed-default products do not. Mitigated by a clear first-launch wizard with one-click provider account links and a sample project that works pre-key (read-only).
- We do not get to amortise prompt-cache hits across users (each user has their own cache lineage with their own provider). Acceptable; the per-user cache hit rate alone is sufficient (>70 % typical, per `ENGINEERING_PRACTICES.md`).
- Customer support burden: users will paste expired keys, hit rate limits, blame us. Mitigated by clear in-app diagnostics and per-provider error messages; the provider's own dashboard is a click away.
- We do not collect usage data the way a managed-default product does. Acceptable — that was a trust commitment we already made.

### Risks

- **A naive user pastes a key into the wrong field and it ends up in logs.** Mitigation: log redaction is mandatory; CI scans for `sk-` and `xoxb-` patterns in test fixtures; the key entry field has an explicit "this stays on your machine" notice.
- **Key compromise via a malicious plugin.** Mitigation: plugins do not have access to the keychain; keychain access is gated through `libsouxmar-ai`'s Rust shell code only.
- **Managed AI becomes a strategic liability if we cannot operate the proxy reliably.** Mitigation: launching managed AI is sequenced for Sprint 14, after the free tier is solid; we do not gate the v1 launch on managed-AI readiness.
- **Pricing of managed AI undercut by direct-API economics.** Mitigation: priced for convenience, not arbitrage. Documented in `BUSINESS_MODEL.md`.

## Pre-mortem

*One year from today.* The BYOK-default proved a friction wall for individual users who wanted "just try it." Conversion from download to first chat dropped by 40 % vs comparable managed-default products. We added a souxmar-supplied "demo key" with rate limits as a stopgap, which created a free-tier-abuse problem, which we then patched with mandatory email signup, which contradicted our "no phoning home" commitment. The decision held conceptually but we underestimated the activation friction. Leading indicators to watch: drop-off rate at the key-entry step in the first-launch wizard; ratio of downloads to first chat; customer-support volume with the words "how do I get a key."

## References

- `docs/AI_INTEGRATION.md` — full AI architecture.
- `docs/BUSINESS_MODEL.md` — tier breakdown, why we reject the managed-default model.
- Comparable-project precedent: Cursor (BYOK-friendly with managed-default), Continue.dev (BYOK-only).

## History

- 2026-05-11: Proposed and accepted with the Phase-0 design.
