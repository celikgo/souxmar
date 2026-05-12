# ADR-0031: SSO (SAML/OIDC) + SCIM provisioning for Team/Enterprise tiers

- **Status:** Accepted
- **Date:** 2026-05-18 (Sprint 20 push 1)
- **Tier:** 1 (architecture)
- **Affects:** account-portal gains `/v1/sso/{provider}` +
  `/v1/scim/v2/*` surfaces; the proxy + marketplace +
  cloud-sync's auth.rs accept SSO-issued tokens; the Team
  tier's seat-management surface depends on SCIM.

## Decision

### SSO providers — SAML 2.0 + OIDC

Account portal accepts both. Per-org admins configure their
IdP (Okta, Google Workspace, Azure AD, ...) via the portal's
`/v1/sso/{provider}` endpoint. SAML 2.0 covers ~80% of
enterprise IdPs; OIDC covers the modern remainder.

### SCIM v2.0 for user provisioning

Enterprise customers want auto-provision + auto-deprovision
when an employee joins/leaves. SCIM v2.0 is the de-facto
standard.

The portal exposes `/v1/scim/v2/Users` + `/v1/scim/v2/Groups`
endpoints. The customer's IdP (Okta etc.) drives provisioning
via SCIM PUT/PATCH calls.

### Token shape stays unchanged

SSO sign-in still issues `sxm_pro_<...>` tokens. The
difference is the issuance path: an SSO-signed-in user gets
the token via the portal's SAML/OIDC callback rather than via
email-link. Downstream services see no difference.

### Tier mapping

- **Free tier:** SSO not available (email-link only).
- **Pro tier:** SSO not available (email-link only). The
  business decision per BUSINESS_MODEL.md.
- **Team tier:** SSO + SCIM available.
- **Enterprise tier:** SSO + SCIM + custom domain (e.g.
  `acme.souxmar.dev` as a CNAME of `account.souxmar.dev`).

## Consequences

- account-portal scaffold (Sprint 17 push 2) gains the SSO +
  SCIM routes. Honest-503 today; Sprint 22 push 1 wires
  against okta / google-workspace integration libraries.
- Sprint 21's pen-test scopes the SAML response-validation
  path (CVE-2017-11427 class of bugs).

## Risks

- **R-039 (SAML signature-validation bugs).** The 2017
  XML-signature-wrapping CVE class. **Mitigation:** Sprint 22
  push 1 picks a well-audited library (samly / saml2 crate);
  Sprint 21 pen-test specifically exercises this.

— Sprint 20 push 1.
