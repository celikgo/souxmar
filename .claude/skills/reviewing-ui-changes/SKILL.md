---
name: reviewing-ui-changes
description: Use when reviewing or making changes to the desktop app's React frontend (src/desktop/src/). Enforces design tokens, accessibility, dim theme compliance, density rules, and Twitter dim palette conformance. Triggers on "UI change", "React component", "frontend PR", "design system", "tokens.css", "dim theme".
---

# Reviewing UI changes

The desktop app's UI follows the design system in `docs/UI_DESIGN.md`. Every change to `src/desktop/src/` is evaluated against the design tokens, accessibility rules, and density model.

## When to use this skill

- Reviewing a PR that adds or modifies a React component.
- Reviewing a PR that touches CSS / Tailwind classes anywhere in the desktop frontend.
- Implementing a new view (chat panel section, inspector tab, viewport overlay).
- Auditing the dim/lights-out/light theme variants.

## When NOT to use this skill

- Changes to the Tauri Rust shell (no UI tokens involved).
- Backend Tauri command implementations.
- Storybook configuration without component changes.

## Hard rules

CI enforces these. This skill double-checks during review:

1. **Never write raw hex colour values in component code.** All colour values come from CSS custom properties defined in `src/desktop/ui/tokens.css`. A `#15202B` literal anywhere outside `tokens.css` fails the build.
2. **Use spacing tokens, not raw pixel values.** `gap-2` / `gap-4` / etc. — never `gap: 6px;` directly.
3. **Use semantic colour tokens, not palette tokens for state.** `--success`, `--warning`, `--danger` exist — use them, not `--accent` for a "saved" indicator.
4. **All interactive elements must have visible focus rings.** 2 px `border-strong` outline. Removing focus styles "because they look ugly" is forbidden.
5. **All `IconButton` instances require `aria-label`.** Missing labels fail accessibility tests.
6. **Components must work in dim, lights-out, AND light themes.** Test all three before merging.
7. **No skeuomorphism.** No bevels, no glass blur, no neon glow. Engineers read numbers; chrome cost is high.

## Token reference

The Twitter dim palette (from `docs/UI_DESIGN.md`):

```
Surfaces:     --bg-canvas: #15202B  |  --bg-panel: #1E2732  |  --bg-elevated: #273340
Borders:      --border-subtle: #38444D  |  --border-strong: #4C5C6B
Text:         --fg-primary: #F7F9F9  |  --fg-secondary: #8B98A5  |  --fg-tertiary: #5B7083
Accent:       --accent-default: #1D9BF0  |  --accent-hover: #1A8CD8  |  --accent-active: #177CC0
Semantic:     --success: #00BA7C  |  --warning: #FFD400  |  --danger: #F4212E
Viz:          --viz-stress-low: #1D9BF0  |  --viz-stress-mid: #FFD400  |  --viz-stress-high: #F4212E
```

Use `bg-canvas`, `text-fg-primary`, etc. via Tailwind. Never raw hex.

## Review checklist

1. **Token compliance.** Search the diff for `#[0-9a-fA-F]{3,6}` outside `tokens.css`. Block if found.
2. **Spacing scale.** Verify all `padding`, `margin`, `gap` use the 4-px scale (`p-1` / `p-2` / `p-3` / `p-4` / `p-6` / `p-8` / `p-12`). Reject ad-hoc values.
3. **Density support.** Components must respect `data-density="compact" | "comfortable"`. Verify both render correctly.
4. **Theme variants.** Manually toggle through dim → lights-out → light. Are all three readable? Do colours have appropriate contrast?
5. **Keyboard navigation.** Tab through every interactive element. Visible focus ring everywhere? Tab order makes sense?
6. **Screen reader.** Run with VoiceOver (macOS) or NVDA (Windows) for any non-trivial component change. Are roles, labels, and live regions correct?
7. **Reduced motion.** Honour `prefers-reduced-motion`. Animations longer than 150 ms must have a no-motion alternative.
8. **Storybook coverage.** Every new component must have a Storybook story. Variants (sizes, states) covered.
9. **Snapshot tests.** Visual regression via Playwright. Snapshots updated intentionally, not by accident.
10. **No new dependencies** without an ADR. Especially watch for "drop-in" component libraries that bring their own theming system.

## Contrast requirements (WCAG)

Verify these pairs in the dim theme:

- `fg-primary` on `bg-canvas`: ≥ 7:1 (AAA)
- `fg-primary` on `bg-panel`: ≥ 7:1 (AAA)
- `fg-secondary` on `bg-canvas`: ≥ 4.5:1 (AA)
- `fg-on-accent` on `accent-default`: ≥ 4.5:1 (AA)
- `fg-primary` on `danger` (e.g. destructive button text): ≥ 4.5:1 (AA)

Use Storybook's a11y addon to verify automatically.

## Density model

Two density modes:

- `compact` (default): tight spacing, 13 px base font.
- `comfortable`: 1.25× spacing, 14 px base font.

Test new components in both. If a component looks broken in `comfortable`, it is broken — fix the component, do not exclude the user.

## Animation rules

- 150 ms ease-out for state changes (hover, focus, panel open/close).
- No decorative animation. Pulse-on-load is fine for a single load-indicator; pulse-on-success is decoration.
- `transition-colors` is the default. Custom transitions need justification.
- Always check `prefers-reduced-motion: reduce` — provide a static fallback.

## Viewport overlay rules

The Three.js / VTK.js scene is the centrepiece. UI overlays (selection markers, BC pins, scale bars, gizmos) live in HTML and use the same tokens as the rest of the UI:

- Field colour ramps in the legend MUST use `--viz-stress-low/mid/high`. Inspector swatches must visually match in-viewport rendering exactly.
- Overlay text uses `font-mono` and `fg-primary` — never raw colour.
- Selection halo uses `accent-default` at 0.4 opacity.

## Common mistakes to flag

- Hard-coded `#1D9BF0` somewhere ("just for this one button"). Always use the token.
- `transition: all 300ms` — too long, too broad. 150 ms specific properties only.
- Forgetting to test the light theme. The team works in dim; the light theme regresses silently. Visual regression catches it; the author should catch it first.
- New shadows. Elevation is mostly flat in this system; shadows belong to overlay surfaces (menus, modals, popovers) only.
- "Just a touch of opacity" on text for emphasis. Use `fg-secondary` / `fg-tertiary` instead.
- A11y attributes added at the end as an afterthought. Add them while writing the component, not after.

## Reference

- `docs/UI_DESIGN.md` — full design system.
- `docs/DESKTOP_APP.md` — performance budgets including UI surfaces.
- `src/desktop/ui/tokens.css` — token source of truth.
- `src/desktop/ui-storybook/` — component gallery + a11y tests.
