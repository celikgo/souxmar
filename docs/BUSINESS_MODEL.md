# Business Model

souxmar is **open-core**. The library, plugin SDK, CLI, Python bindings, **and the desktop application itself** are open source under Apache 2.0. Optional managed services (managed AI, cloud sync, hosted compute, team features) are commercial. This document spells out exactly where the line sits, why, and how the project sustains itself.

The model is deliberately similar to GitLab, Sentry, and PostHog (open core) and to Cursor (free desktop app + paid managed services), and deliberately *not* similar to dual-licensed projects that gate basic functionality behind a paid tier.

## What is open source (Apache 2.0)

- `libsouxmar-core`, `libsouxmar-plugin`, `libsouxmar-pipeline` — the C++ libraries.
- `souxmar` CLI.
- `pysouxmar` Python bindings.
- The plugin SDK headers and CMake helpers.
- The desktop application (Tauri shell + React frontend) source and build scripts.
- All in-tree adapters (OpenCASCADE, Gmsh, FEniCSx, OpenFOAM, Blender, VTK).
- All in-tree reference plugins (native mesher, linear elasticity, heat).
- All documentation.

A user can clone the repository, build everything, run the desktop app, configure their own AI key, and have a fully functional product without ever paying us a cent. This is not a crippled "community edition." It is the product.

## What is commercial

souxmar Inc. (or the eventual entity behind the project) sells **services that the open-source desktop app calls into**:

| Service                          | What it adds                                                                  |
| -------------------------------- | ----------------------------------------------------------------------------- |
| **Managed AI**                   | We hold and rotate the provider keys; user pays a flat monthly fee plus overage. No BYOK setup, automatic model failover, prompt-cache amortisation across users. |
| **Cloud project sync**           | Encrypted sync of `pipelines/` and `geometry/` across the user's machines. Versioned, time-travel restore. Zero-knowledge optional for Enterprise. |
| **Hosted compute offload**       | Push a `solve` stage to a managed HPC backend; results stream back into the desktop app. Billed by node-hour. |
| **Plugin marketplace (paid)**    | Hosting + payment infrastructure for plugin authors who want to sell their plugins. Free plugins are listed in the open index regardless. |
| **Team features**                | Shared workspaces, role-based access to projects, centralised plugin policy, audit log export. |
| **Enterprise**                   | SSO (SAML/OIDC), SCIM, on-prem managed-AI proxy, support SLA, indemnification, training. |

None of these are required for the product to work. The desktop app on the free tier has no nag screens, no upsell modals, no telemetry, and no functional restriction beyond "use your own AI key and your own compute."

## Tiers

| Tier            | Price (placeholder)         | For                                                                                          |
| --------------- | --------------------------- | -------------------------------------------------------------------------------------------- |
| **Free**        | $0                          | Individuals, students, OSS contributors. Full desktop app. BYOK AI. Local compute. Plugin SDK. |
| **Pro**         | $20 / user / month          | Working engineers. Managed AI with monthly token allowance, cloud sync, priority plugin index publishing. |
| **Team**        | $40 / user / month, min 5   | Engineering teams. Shared workspaces, SSO-lite (Google/Microsoft), centralised plugin policy, usage analytics. |
| **Enterprise**  | Annual contract             | Regulated industries. Full SSO, on-prem AI proxy option, audit export, indemnification, named support. |

Numbers are placeholders for design discussion; final pricing is set when the product reaches GA.

Academic, classroom, and qualified open-source-contributor licenses are free at every tier — not a discount, free. The plugin marketplace gives 90% of every sale to the plugin author.

## Why open-source the desktop app

This is the question that gets asked most. Three reasons:

1. **Trust.** Engineers in regulated industries (aerospace, civil, automotive) need to audit what runs on their machines and what data leaves them. A closed app is a non-starter for many target customers; an open one is a procurement-tractable artefact.
2. **Plugin ecosystem leverage.** A closed app fights its plugin authors for the user's wallet. An open app aligns with them — third parties build plugins that make the platform more valuable, and we monetise services orthogonal to plugins.
3. **Defensibility comes from operations, not source.** The hard parts of the paid tier — proxy infrastructure, billing, compliance, SSO, on-prem support — are operational moats, not code moats. Closing the source would buy us nothing and cost us trust.

## How does this not get destroyed by a fork?

The standard "AWS will fork it and resell it" fear. Mitigations:

- **The trademark is not Apache-licensed.** `souxmar` (the name and any logo) is held by the project entity. A fork can ship the same code; it cannot call itself souxmar or claim compatibility branding.
- **The plugin index is curated by the project.** A fork can publish its own; ours is canonical because we keep it good.
- **Managed services are an operations business, not a code business.** Cloning Tauri + React + a build script does not give you our managed-AI proxy, billing, or SOC 2 compliance.
- **Apache 2.0 is permissive on purpose.** We would rather be widely adopted than narrowly protected. The model that beat MongoDB's SSPL was permissive licensing combined with a strong managed offering — that is the bet.

## Plugin marketplace economics

Out-of-tree plugin authors choose:

- **Free + listed in the open index.** Apache 2.0 (or any OSI-approved license) source, listed on `souxmar.dev/plugins`, indexed by `souxmar plugin search`. No fee, no revenue share.
- **Paid via marketplace.** We host the binaries, handle Stripe billing, issue licenses, run the conformance suite in CI. Author keeps 90% of revenue. We keep 10%.

We do not gatekeep on quality (other than the conformance badge) and we do not exclude plugins that compete with our managed services. A third party can sell a hosted-compute plugin that competes with ours; that is fair game.

## What we will not do

These are commitments, not vague preferences. Departures require a public announcement and 12 months of notice.

- **No "open-core but the useful parts are closed."** No moving features from open source into the paid tier. New features land in the open tier first; paid features are net-new managed services.
- **No telemetry without explicit opt-in.** The free desktop app does not phone home. Crash reports require a checkbox on first launch and can be revoked.
- **No proprietary file formats for free-tier users.** Project files, pipeline YAML, and exported results are open and human-readable, forever. A user who churns off Pro keeps their data.
- **No vendor lock-in via the AI integration.** BYOK is permanent. Managed AI is a convenience, not a moat.
- **No license relicensing.** Apache 2.0 stays. We will not pull a Hashicorp / Elastic / Mongo move.
- **No anti-fork clauses, no "non-compete" license terms, no Common Clause additions.** Ever.

## Sustainability runway

The project's success scenarios:

1. **Self-sustaining open source.** Core development funded by Pro/Team/Enterprise revenue. Happy state.
2. **Foundation-hosted.** If the project outgrows a single entity, donate the core (libraries + CLI + Python + plugin SDK + conformance suite) to a neutral foundation (Linux Foundation, NumFOCUS), and keep the commercial entity around the managed services. The Apache 2.0 license makes this trivial.
3. **Stewardship handoff.** If the commercial entity fails, the open-source artefacts continue under whatever maintainer set the community elects. Apache 2.0 + permissive contribution model + DCO sign-off make the project survivable independent of any one company.

The model is designed so that a worst-case outcome for the commercial entity is not an extinction event for users.

## Comparable projects

| Project       | Model                        | What we copy                               |
| ------------- | ---------------------------- | ------------------------------------------ |
| Cursor        | Free desktop + paid managed AI | Desktop is free; managed AI is the paid product. |
| GitLab        | Open core, generous free tier | Operations and team features are paid; the product itself is open. |
| Sentry        | BSL-after-3-years for some, FOSS for the rest | (We don't copy BSL. Apache stays.)         |
| HashiCorp     | Was open, then BSL           | (Counter-example. We commit not to do this.) |
| Blender       | LF-stewarded, donations + commercial cloud | Donation-friendly governance even in the commercial scenario. |
| OpenFOAM      | GPL + foundation             | Adapter target; not a model we follow ourselves. |

## Tracking issues

- Pricing finalisation: tracked in a marketing doc, not here.
- Trademark filing: pre-1.0 deliverable.
- Foundation-readiness: a checklist of things that must be true (governance doc, CoC, DCO, neutral CI, no closed dependencies in the open core) so the option remains open.
