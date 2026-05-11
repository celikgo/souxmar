# docs-site — souxmar's Vitepress documentation

Sprint 12 push 3. The end-user-facing documentation site served at
**docs.souxmar.dev** (CNAME pointing at GitHub Pages, configured
out-of-band on the souxmar.dev DNS).

This directory is the public docs surface. The repo's `docs/`
directory holds **contributor-facing** documents (ADRs, governance,
sprint plans, retros, RFCs); the split lets each evolve at its own
cadence without conflating audiences.

## Running locally

```sh
cd docs-site
npm install       # one-time
npm run dev       # http://localhost:5173/
```

## Building

```sh
npm run build
# Produces docs-site/.vitepress/dist/ — what the CI workflow uploads.

npm run preview
# Serves the production bundle locally for sanity-checking before
# pushing.
```

## Where things live

| Section            | URL prefix      | Source                             |
| ------------------ | --------------- | ---------------------------------- |
| Landing page       | `/`             | `index.md`                         |
| Install + guide    | `/guide/`       | `guide/install.md`, `guide/first-pipeline.md`, etc. |
| Agent reference    | `/agents/`      | `agents/index.md`, etc.            |
| Plugin authoring   | `/plugins/`     | `plugins/index.md`, etc.           |
| Business model     | `/business/`    | `business/index.md`, etc.          |

Adding a page:

1. Drop the `.md` under the matching directory.
2. Add an entry to `.vitepress/config.ts`'s `sidebar` map.
3. Cross-link from the relevant index page so it's discoverable
   without the sidebar.

## Publishing

`.github/workflows/docs-site.yml` rebuilds + publishes on every
push to `master` that touches this directory. The deploy URL is
`https://souxmar.github.io/souxmar/` (GitHub Pages default);
the `docs.souxmar.dev` CNAME points at it.

## What's missing (Sprint 13+)

- **API reference auto-generation.** The CLI's tool catalogue +
  the agent tool surface should generate from the C ABI headers
  + the v1 tool registry. Currently the `/agents/tools` page is
  hand-curated. Sprint 13 task: replace it with generated content
  from `souxmar agent list --json`.
- **Versioned docs.** Today docs.souxmar.dev serves whatever's on
  `master`. Once we cut v1.0 we'll need `/v0.9/` and `/v1.0/`
  branches — Vitepress's versioning support is straightforward,
  defer to Sprint 14.
- **Search-engine optimisation.** Vitepress generates clean
  `<title>` + `<meta description>` but we haven't set up
  Google Search Console or a sitemap.xml. Sprint 13+.
- **i18n.** Out of scope for v1.0. Sprint 18+ explores after the
  community has a sense of language demand.
