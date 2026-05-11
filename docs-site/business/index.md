# Business model

souxmar is **open-core**, not closed-source-with-an-open-shell. The
desktop app, CLI, Python library, plugin SDK, and every reference
plugin are all Apache 2.0. Premium services attached to them are
optional.

This page lives at `/business/` on the public site so it's easy to
link to from sales conversations + investor decks. The same content
lives in `docs/BUSINESS_MODEL.md` in the repo; the two stay in sync
via a CI cross-check.

## Tiers at a glance

| Tier               | Price (placeholder, v0.9 alpha) | What you get                                                            |
| ------------------ | ------------------------------- | ----------------------------------------------------------------------- |
| **Free**           | $0                              | Full desktop app + CLI + Python lib + plugin SDK + BYOK AI + local compute. Apache 2.0. |
| **Pro**            | $20 / month / user              | Managed AI proxy (no key management) + cloud sync + priority support.   |
| **Team**           | $35 / month / user, billed yearly | Pro + SSO (SAML / OIDC) + shared project library + per-team audit log. |
| **Enterprise**     | Custom                          | Team + on-prem AI proxy + dedicated support + SLA.                       |

See [the tiers page](/business/tiers) for the full feature matrix.

## What's open and what's hosted

The line is clear:

- **Open source**: anything that runs on the user's machine. The
  desktop app, CLI, libraries, plugin SDK, ALL example plugins
  + reference adapters (OpenCASCADE, Gmsh, OpenFOAM, FEniCSx
  wrappers). Apache 2.0.
- **Hosted (Pro+)**: services that require infrastructure we
  operate. Managed AI proxy (handles your prompts without you
  shipping a key), cloud sync (encrypted-at-rest project storage),
  hosted compute offload (submit a CFD solve to managed HPC). Each
  is **opt-in** per project; the free tier never silently calls
  out.

## Why we won't relicense

The desktop app being open source is a **load-bearing trust
commitment** to engineers in regulated industries (aerospace,
defence, medical devices). They can audit every line. We've
documented the no-relicensing decision in
[`docs/BUSINESS_MODEL.md`](https://github.com/souxmar/souxmar/blob/master/docs/BUSINESS_MODEL.md)
+ in our governance docs; it's not something a future investor
can unilaterally change.

The hosted services are where commercial revenue comes from. The
ratio scales properly because the open-core surface is the entire
product; the Pro tier is convenience layered on top.

## Plugin marketplace

Sprint 16 launches the paid plugin marketplace. Authors set the
price; we take 10 % + payment-processor fees. The marketplace
itself is open-core — the listing format is part of the public
[`docs/plugin-index.toml`](https://github.com/souxmar/souxmar/blob/master/docs/plugin-index.toml)
surface; the paid distribution channel adds Stripe integration +
ed25519-signing for the per-author trust chain.

90 / 10 split was chosen deliberately:

- It's the same split Apple's App Store starts at (15 % after
  year 1, but we don't have year-1 marketing budgets).
- Microsoft Store is 12 % for apps, 0 % for games. We're closer to
  Apple's apps story.
- Steam is 30 %. That's too aggressive for a niche professional
  market.

## Foundation handoff

Sometime around v1.5 (post-marketplace launch, post-Pro-revenue-
profitable) we plan to transfer governance to a software foundation
(Apache Software Foundation, Linux Foundation, or similar). The
hosted services + the for-profit entity that operates them stay
commercial; the open-core trademark + the governance of the
codebase move out.

This is years away. It's documented here because regulated-industry
buyers ask, and the answer affects their decisions.

## How to support souxmar without paying

- **File bugs.** Especially from public-alpha real-world use.
- **Author plugins.** Even free plugins enrich the ecosystem.
- **Cite us.** If you ship a paper or a product on top of souxmar,
  please credit the project + link to docs.souxmar.dev.
- **Spread the word.** Talk at your local FEM/CFD meetup; post your
  studies; share with engineers who'd benefit.
