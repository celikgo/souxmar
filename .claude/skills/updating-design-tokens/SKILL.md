---
name: updating-design-tokens
description: Use when changing design tokens in src/desktop/ui/tokens.css — colors, spacing, typography, radii, elevation. Tokens are a project-wide contract; changes touch every component. Includes the visual-regression review process. Triggers on "design tokens", "tokens.css", "update palette", "color change", "Twitter dim palette".
---

# Updating design tokens

Design tokens in `src/desktop/ui/tokens.css` are the source of truth for the desktop app's visual language. Changing them touches every component, every screen, every screenshot. This skill governs how those changes happen safely.

## When to use this skill

- Adding a new token (e.g. a new viz colour for a new field type).
- Changing a token value (e.g. shifting `--accent-default` slightly for contrast).
- Adding a new theme variant (e.g. a high-contrast theme).
- Auditing the tokens for accessibility compliance or palette drift.

## When NOT to use this skill

- Adding a one-off colour for "this single component." That is exactly what tokens prevent. Use an existing token or propose a new one through this skill.
- Changing component-level styles. Use `reviewing-ui-changes` instead.

## The token contract

Tokens are CSS custom properties scoped to the theme via `data-theme`. The default theme is `dim` (Twitter dim palette). Variants: `lights-out`, `light`.

```css
:root[data-theme="dim"] {
  /* Surfaces, borders, text, accent, semantic, viz — see UI_DESIGN.md */
}
```

Three rules govern token changes:

1. **Tokens are the only source of colour, spacing, radius, and elevation in the codebase.** A raw `#15202B` outside `tokens.css` is a CI failure.
2. **All theme variants must define every token.** Adding a token means defining it in `dim`, `lights-out`, AND `light` simultaneously.
3. **Token changes are visual-regression-tested across every component in Storybook.** A token change PR includes the regenerated snapshots.

## Workflow for adding a token

1. **Justify it.** Write a 1–2 sentence justification in the PR description. "Why does an existing token not cover this case?" If the answer is unclear, you do not need a new token.
2. **Name it semantically, not visually.** `--success` not `--green`. `--bg-elevated` not `--gray-2`. Names that describe purpose survive palette swaps.
3. **Define for all themes.** Even if the value is identical across `dim` and `lights-out`, write it explicitly — implicit fallbacks are footguns.
4. **Verify contrast.** New text/background pairs must hit WCAG AA in every theme. New accent pairs need ≥ 4.5:1 against text colours used on them.
5. **Add to Storybook tokens page.** The `Foundations / Tokens` story lists every token; add yours.
6. **Update the table in `docs/UI_DESIGN.md`.** Tokens are documented; undocumented tokens are forbidden.

## Workflow for changing a token

1. **Surface the rationale.** Token changes propagate everywhere; the PR description must explain why.
2. **Run the visual regression suite locally** before pushing:
   ```bash
   pnpm -C src/desktop test:visual --update-snapshots
   ```
3. **Review the snapshot diff.** Every changed component is in the diff. Look for:
   - Components that broke unexpectedly (regressions).
   - Components whose new appearance is *less* readable (contrast regression).
   - Components that look the same — check whether they should have changed.
4. **Update WCAG verification table** in `docs/UI_DESIGN.md` with new contrast ratios.
5. **Tag a Desktop maintainer for review.** Token changes are Tier-2 normally; Tier-3 if they affect accessibility or break a long-standing visual idiom.

## Workflow for adding a theme variant

1. **RFC required.** A new theme is Tier-3 — it doubles the per-token maintenance burden.
2. **Define every token** that exists in `dim`. CI checks that no token is missing from any theme.
3. **Add a Storybook story per variant.** The `Foundations / Themes` page renders the same components in every theme.
4. **Visual regression** for every component, every theme. Big snapshot regen.
5. **Document the rationale** in `UI_DESIGN.md` — when a user would pick this theme over the others.

## Twitter dim palette discipline

The default `dim` theme is anchored on the Twitter dim spectrum (`#15202B` base, `#1D9BF0` accent, etc.). Per the legal review at Sprint 0 (R-007), our tokens are similar but not identical to Twitter's. Two implications:

- We do not claim Twitter compatibility; we credit Twitter dim as inspiration in `UI_DESIGN.md`, no more.
- We do not slavishly track Twitter's palette evolution. If they change their colours, ours stay put.

## Common mistakes

- **Adding `--accent-soft-2` "just for one component."** The `*-soft` tokens already exist. Use them or propose a new semantic name.
- **Changing a token value without running visual regression.** Snapshots will diverge silently, then a future engineer will re-baseline them and lose the audit trail.
- **Defining a token for `dim` only and assuming the others will inherit.** They won't; CI will fail.
- **Adding a colour outside the named-tokens model.** No `var(--gray-300)`-style numeric scales. Semantic names only.
- **Naming a token visually.** `--blue-500` is forbidden. `--accent-default` is fine.
- **Forgetting to update `docs/UI_DESIGN.md`.** The doc is the contract; the code follows it.
- **Touching the Twitter dim palette values without legal sign-off** if the change makes them more visually similar to Twitter's current palette.

## Validation checklist

Before merging a token change PR:

- [ ] Token defined in `dim`, `lights-out`, AND `light`.
- [ ] Storybook tokens page updated.
- [ ] `docs/UI_DESIGN.md` table updated.
- [ ] Visual regression snapshots updated and reviewed.
- [ ] WCAG contrast ratios verified for any text/background pair affected.
- [ ] CI lint passes (no raw hex outside `tokens.css`).
- [ ] PR description explains *why* (not *what* — the diff shows what).

## Reference

- `docs/UI_DESIGN.md` — full design system, palette tables, contrast verification.
- `docs/DESKTOP_APP.md` — performance budgets including UI surfaces.
- `src/desktop/ui/tokens.css` — token source of truth.
- `src/desktop/ui-storybook/` — component gallery and visual regression base.
- `docs/SPRINT_PLAN.md` — risk R-007 (palette legal review).
