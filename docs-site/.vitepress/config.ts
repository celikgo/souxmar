// SPDX-License-Identifier: Apache-2.0
//
// Sprint 12 push 3 — Vitepress config for the public docs site at
// docs.souxmar.dev. Built by .github/workflows/docs-site.yml and
// published to GitHub Pages on every push to master.
//
// The site is the canonical public surface for end-user documentation;
// the in-repo `docs/` directory continues to hold ADRs, governance
// docs, sprint plans, retros — the *contributor*-facing artefacts.
// Anything an end user needs to know lives under `docs-site/`.

import { defineConfig } from "vitepress";

export default defineConfig({
  title: "souxmar",
  description: "Open-source CAE platform: CAD, mesh, FEM, CFD, with an agentic AI chat.",
  cleanUrls: true,
  // The site is served at https://docs.souxmar.dev/; with a
  // sub-path deploy under github.com/souxmar/souxmar/gh-pages, the
  // base must match.
  base: process.env.SOUXMAR_DOCS_BASE || "/",

  themeConfig: {
    logo: "/logo.svg",
    siteTitle: "souxmar",
    nav: [
      { text: "Install",  link: "/guide/install" },
      { text: "Guide",    link: "/guide/" },
      { text: "Agents",   link: "/agents/" },
      { text: "Plugins",  link: "/plugins/" },
      { text: "Pricing",  link: "/business/" },
      {
        text: "v0.9.0",
        items: [
          { text: "Changelog",        link: "https://github.com/souxmar/souxmar/blob/master/CHANGELOG.md" },
          { text: "ADRs",             link: "https://github.com/souxmar/souxmar/tree/master/docs/adr" },
          { text: "Sprint retros",    link: "https://github.com/souxmar/souxmar/tree/master/docs/retros" },
        ]
      },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "Getting started",
          items: [
            { text: "Install",            link: "/guide/install" },
            { text: "First pipeline",     link: "/guide/first-pipeline" },
            { text: "Concepts",           link: "/guide/concepts" },
            { text: "BYOK + AI providers", link: "/guide/byok" },
            { text: "Auto-update + rollback", link: "/guide/updates" },
          ],
        },
      ],
      "/agents/": [
        {
          text: "Agent reference",
          items: [
            { text: "Overview",           link: "/agents/" },
            { text: "Tool catalogue",     link: "/agents/tools" },
            { text: "Confirmation policies", link: "/agents/confirmation" },
            { text: "Audit log",          link: "/agents/audit-log" },
          ],
        },
      ],
      "/plugins/": [
        {
          text: "Plugin authoring",
          items: [
            { text: "Overview",           link: "/plugins/" },
            { text: "Your first plugin",  link: "/plugins/first-plugin" },
            { text: "Conformance suite",  link: "/plugins/conformance" },
            { text: "Marketplace",        link: "/plugins/marketplace" },
          ],
        },
      ],
      "/business/": [
        {
          text: "Business model",
          items: [
            { text: "Open core",          link: "/business/" },
            { text: "Free vs Pro vs Team", link: "/business/tiers" },
            { text: "Why open source",    link: "/business/why-open" },
          ],
        },
      ],
    },

    socialLinks: [
      { icon: "github",  link: "https://github.com/souxmar/souxmar" },
      { icon: "discord", link: "https://souxmar.dev/community" },
    ],

    footer: {
      message: "Apache-2.0 licensed. The desktop app + CLI + libraries + plugin SDK are all open source. Pro tier adds managed AI + cloud sync; see /business/.",
      copyright: "© 2026 souxmar contributors",
    },

    search: {
      // Local-only search index built at build time. Doesn't ship
      // user behaviour to a third-party search backend.
      provider: "local",
    },
  },

  // Markdown enhancements. Custom block syntax (`::: tip`,
  // `::: warning`) is enabled by default; we don't add any
  // plugins yet — Sprint 13+ may add a math-renderer for the
  // solver-validation docs.
  markdown: {
    lineNumbers: false,
  },
});
