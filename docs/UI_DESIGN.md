# UI Design System

souxmar's desktop app uses a dense, dark, calm visual language inspired by Twitter's "dim" theme. This document is the design contract: design tokens, component patterns, accessibility rules, and the reasoning behind them.

The system is implemented in Tailwind CSS via design tokens in `src/desktop/ui/tokens.css`, consumed by Radix-based primitives in `src/desktop/ui/`.

## Principles

1. **Dim, not black.** Pure black on a backlit display is high-contrast and tiring. Twitter dim (#15202B base) is the sweet spot for long simulation sessions.
2. **Information density beats whitespace.** Engineers comparing five mesh metrics or scrubbing a result field do not want chrome that consumes 40% of the panel. The grid is dense by default; a Comfortable density toggle exists for users who want it.
3. **Structure through hierarchy, not borders.** Three background tints stack panels visually without drawing every divider. Borders are reserved for interactive separations (resize handles, table cells).
4. **Accent sparingly.** A single brand blue, used only for primary actions, active selections, and current viewport selection. If everything is highlighted, nothing is.
5. **Motion is functional.** 150 ms ease-out for state changes; nothing decorative. Honour `prefers-reduced-motion`.

## Color tokens (Twitter dim spectrum)

The base palette is the Twitter "dim" set, anchored on `#15202B`. Tokens are CSS custom properties; never use raw hex outside `tokens.css`.

```css
/* tokens.css — dim theme (default) */
:root[data-theme="dim"] {
  /* Surfaces */
  --bg-canvas:        #15202B;  /* viewport / app background       */
  --bg-panel:         #1E2732;  /* sidebars, cards, inspector      */
  --bg-elevated:      #273340;  /* menus, dialogs, hover surfaces  */
  --bg-overlay:       rgba(21, 32, 43, 0.85);  /* modal scrim     */

  /* Borders */
  --border-subtle:    #38444D;  /* default divider                 */
  --border-strong:    #4C5C6B;  /* selected, focused outlines      */

  /* Text */
  --fg-primary:       #F7F9F9;  /* body text, headings             */
  --fg-secondary:     #8B98A5;  /* labels, metadata, placeholders  */
  --fg-tertiary:      #5B7083;  /* disabled, hint text             */
  --fg-on-accent:     #FFFFFF;  /* text on accent surfaces         */

  /* Accent (Twitter blue) */
  --accent-default:   #1D9BF0;
  --accent-hover:     #1A8CD8;
  --accent-active:    #177CC0;
  --accent-soft:      rgba(29, 155, 240, 0.12);

  /* Semantic */
  --success:          #00BA7C;
  --success-soft:     rgba(0, 186, 124, 0.12);
  --warning:          #FFD400;
  --warning-soft:     rgba(255, 212, 0, 0.12);
  --danger:           #F4212E;
  --danger-soft:      rgba(244, 33, 46, 0.12);

  /* Field visualisation (matches in-viewport overlays) */
  --viz-stress-low:   #1D9BF0;
  --viz-stress-mid:   #FFD400;
  --viz-stress-high:  #F4212E;
}

/* Lights-out variant for OLED / focus mode */
:root[data-theme="lights-out"] {
  --bg-canvas:        #000000;
  --bg-panel:         #16181C;
  --bg-elevated:      #1E1E1E;
  /* …other tokens unchanged from dim… */
}

/* Light theme — minimal, used for screenshots and printed reports */
:root[data-theme="light"] {
  --bg-canvas:        #FFFFFF;
  --bg-panel:         #F7F9F9;
  --bg-elevated:      #EFF3F4;
  --border-subtle:    #EFF3F4;
  --border-strong:    #CFD9DE;
  --fg-primary:       #0F1419;
  --fg-secondary:     #536471;
  --fg-tertiary:      #8B98A5;
  /* …accent + semantic carry over… */
}
```

Contrast ratios verified against WCAG AA at 14 px and AAA at 16 px+:

| Pair                          | Ratio  | WCAG    |
| ----------------------------- | ------ | ------- |
| `fg-primary` on `bg-canvas`   | 14.8:1 | AAA     |
| `fg-secondary` on `bg-canvas` | 5.6:1  | AA      |
| `fg-tertiary` on `bg-canvas`  | 3.4:1  | AA Large|
| `fg-on-accent` on `accent`    | 4.7:1  | AA      |

## Typography

```css
:root {
  --font-ui:        "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
  --font-mono:      "JetBrains Mono", ui-monospace, "SF Mono", Consolas, monospace;
  --font-num:       "Inter Tight";  /* tabular-nums variant for inspector */

  --text-xs:        11px / 16px;    /* metadata, axis labels        */
  --text-sm:        12px / 18px;    /* secondary UI, table cells    */
  --text-base:      13px / 20px;    /* default UI                   */
  --text-md:        14px / 22px;    /* prose, chat                  */
  --text-lg:        16px / 24px;    /* section headers              */
  --text-xl:        20px / 28px;    /* page headers                 */

  --weight-regular: 400;
  --weight-medium:  500;
  --weight-semibold:600;
}
```

Numerical values in inspector tables and pipeline-stage outputs use `font-feature-settings: "tnum"` for column alignment.

## Spacing & layout grid

4-px base. Tailwind spacing aliases:

| Token | px | Use                              |
| ----- | -- | -------------------------------- |
| `0.5` | 2  | Hairlines inside compound icons  |
| `1`   | 4  | Inline gap                       |
| `2`   | 8  | Within a card / row              |
| `3`   | 12 | Between rows in dense tables     |
| `4`   | 16 | Card padding                     |
| `6`   | 24 | Panel padding                    |
| `8`   | 32 | Section separator                |
| `12`  | 48 | Page-level breathing             |

**Density toggles.** `data-density="compact"` (default) uses the scale above. `data-density="comfortable"` multiplies by 1.25 and bumps base font to 14 px.

## Border radii and elevation

```css
:root {
  --radius-sm: 4px;   /* badges, chips                */
  --radius-md: 6px;   /* buttons, inputs              */
  --radius-lg: 8px;   /* cards, modals                */
  --radius-xl: 12px;  /* major panels                 */

  --elevation-1: 0 1px 0 0 var(--border-subtle);
  --elevation-2: 0 4px 12px rgba(0, 0, 0, 0.30);
  --elevation-3: 0 16px 32px rgba(0, 0, 0, 0.40);
}
```

Elevation is conservative. Most surfaces are flat, separated by background tint, not shadow. Shadows belong to overlay surfaces (menus, modals, popovers).

## Component patterns

| Component         | Behaviour                                                                    |
| ----------------- | ---------------------------------------------------------------------------- |
| `Button`          | Variants: `primary` (accent), `secondary` (panel-tint), `ghost` (transparent), `danger`. Sizes `sm`/`md`. Focus: 2 px `border-strong` outline + accent inset. |
| `IconButton`      | 28 × 28 hit target (32 × 32 in comfortable). Icon 16 × 16. Tooltip after 400 ms. |
| `Input`/`Select`  | `bg-panel`, `border-subtle`, `border-strong` on focus. Error state: `danger` border + helper text. |
| `Tabs`            | Underlined active state (2 px `accent`), `fg-secondary` inactive labels.     |
| `DataTable`       | Sticky header, monospaced numeric cells, row hover `bg-elevated`, single-click row select. Sortable columns show direction arrow only when active. |
| `Tooltip`         | `bg-elevated`, 400 ms delay, max 32 ch wide, no decorative animation.        |
| `CommandPalette`  | `⌘K`/`Ctrl+K`. Fuzzy-search across actions, files, tags, plugins, and AI prompts. Always available. |
| `ContextMenu`     | Right-click on entities and pipeline stages. Mirrors palette commands relevant to the target. |
| `Toast`           | Top-right, 4 s default. `success`/`warning`/`danger` accent stripe on left.  |
| `Dialog`          | Centred, `bg-elevated`, escape-to-dismiss, focus-trapped. Confirmations have explicit primary action (no "OK"). |

## Chat panel patterns

The chat is a Radix `ScrollArea` of message blocks. Each block is one of:

- **User message** — right-aligned, `bg-elevated`, `radius-lg`.
- **Assistant message** — left-aligned, no bubble, `fg-primary` prose. Markdown rendered.
- **Tool invocation card** — left-aligned, bordered card, header shows tool name + status (pending / done / failed), expandable to show inputs and outputs. Cards stream in as the model emits tool calls.
- **Confirmation chip** — inline, accent border, two buttons (`Confirm` / `Cancel`). Inline because asking the user to look elsewhere breaks the conversation.
- **Cost tick** — every N turns, a small `fg-tertiary` line shows running token spend.

See [`AI_INTEGRATION.md`](AI_INTEGRATION.md) for what the agent can actually do.

## Viewport overlays

Viewport rendering is Three.js / VTK.js, but UI overlays (selection markers, BC pins, scale bars, gizmos) live in HTML on top, so they share tokens with the rest of the UI. Field colour ramps use the `--viz-*` tokens so legends in inspector panels match the in-viewport rendering exactly.

## Iconography

Lucide icon set, two sizes:

- 16 px in dense UI (buttons, inputs, table cells).
- 20 px in panel headers and chat.

Custom icons (mesh element shapes, BC types, solver kinds) live in `src/desktop/ui/icons/` as inline SVGs. They use `currentColor` and inherit text colour for free dark/light theming.

## Accessibility checklist (every PR)

- All interactive elements have visible focus rings (`border-strong`, 2 px, never removed even when "ugly").
- All `IconButton`s have `aria-label`.
- Modal `Dialog`s trap focus and restore on close.
- Keyboard equivalents exist for every mouse action; documented in command palette.
- Colour is never the only carrier of information (icons + colour for severity, patterns + colour for field overlays).
- Live regions (`aria-live="polite"`) for streaming chat tokens and progress events.
- Screen-reader testing: VoiceOver on macOS, NVDA on Windows, every release.

## Things this design system does not do

- **No skeuomorphism.** No fake bevels, no glass blur, no neon glow. Engineers are reading numbers; chrome cost is high.
- **No custom font shipped per release.** Inter and JetBrains Mono are bundled once; we do not chase font fashion.
- **No animation libraries.** Radix's built-in transitions plus a single `transition-colors` utility cover everything.
- **No theme builder.** Three themes (dim, lights-out, light) are the universe. A user wanting a custom theme overrides tokens via a single CSS file the app loads from `~/.config/souxmar/theme.css`.

## Reference

Live token preview, component gallery, and accessibility checks live in `src/desktop/ui-storybook/` and are published to a static site per release. Every component there is exercised by a Playwright snapshot test.
