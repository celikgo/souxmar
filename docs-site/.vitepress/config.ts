// SPDX-License-Identifier: Apache-2.0
//
// Vitepress config for the public docs site at celikgo.github.io/souxmar.
// Built by .github/workflows/docs-site.yml and published to GitHub Pages on
// every push to master that touches docs-site/.
//
// The site is the canonical public surface for end-user documentation;
// the in-repo `docs/` directory continues to hold ADRs, governance
// docs and RFCs — the *contributor*-facing artefacts. Superseded
// planning artefacts live in docs/attic/ and are not published here.
// Anything an end user needs to know lives under `docs-site/`.

import { defineConfig } from "vitepress";

export default defineConfig({
  title: "souxmar",
  description: "Open-source CAE platform: CAD, mesh, FEM, CFD, with an agentic AI chat.",
  cleanUrls: true,
  // Served at https://celikgo.github.io/souxmar/ — a *project* Pages site,
  // so every asset sits under the /souxmar/ sub-path and the base must say
  // so. Override with SOUXMAR_DOCS_BASE when serving from somewhere else.
  base: process.env.SOUXMAR_DOCS_BASE || "/souxmar/",

  themeConfig: {
    logo: "/logo.svg",
    siteTitle: "souxmar",
    nav: [
      { text: "Install",  link: "/guide/install" },
      { text: "Guide",    link: "/guide/" },
      { text: "Agents",   link: "/agents/" },
      { text: "Plugins",  link: "/plugins/" },
      { text: "Manufacturing", link: "/manufacturing/" },
      { text: "Pricing",  link: "/business/" },
      {
        text: "v0.9.0",
        items: [
          { text: "Changelog",        link: "https://github.com/celikgo/souxmar/blob/master/CHANGELOG.md" },
          { text: "ADRs",             link: "https://github.com/celikgo/souxmar/tree/master/docs/adr" },
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
      // Manufacturing + marine vertical. Every capability documented
      // here is a closed-form or heuristic screening model, not a
      // calibrated process simulation; the pages say so on themselves.
      // Only three pages exist — do not add sidebar links ahead of the
      // markdown.
      "/manufacturing/": [
        {
          text: "Manufacturing + marine",
          items: [
            { text: "Overview",                link: "/manufacturing/" },
            { text: "Additive manufacturing",  link: "/manufacturing/additive" },
            { text: "Marine + subsea",         link: "/manufacturing/marine" },
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
      { icon: "github",  link: "https://github.com/celikgo/souxmar" },
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
