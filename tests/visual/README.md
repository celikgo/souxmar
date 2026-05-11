# Visual regression — Sprint 11 push 1

Playwright-based visual coverage for the desktop app. Sprint 10
push 10 landed the onboarding wizard against the dim-theme tokens
but had no automated baseline to detect a token-bump regression.
Sprint 10's R-012 ("desktop app has zero visual-regression
coverage") closes here.

## What this catches

- **Token regressions.** A change in `src/desktop/src/ui/tokens.css`
  that shifts the dim-theme palette beyond the configured pixel
  budget fails the diff.
- **Layout regressions.** A change that re-wraps text, resizes a
  card, or shifts a button by more than ~1500 pixels (default
  budget) fails the diff.
- **Copy regressions.** A change that drops or replaces a heading
  is caught by the explicit `getByRole({ name: /…/i })` assertions
  before the screenshot fires; cheaper signal than a pixel diff.

## What this does NOT catch

- **Functional regressions.** A Tauri command that fails to write
  the keychain entry won't fail this suite — we mock the bridge.
  Functional coverage is for the Sprint 11 dogfood week.
- **Behaviour across platforms.** Today the suite runs Chromium-
  on-Linux-CI only. macOS / Windows font rendering differs; a
  Sprint 12+ extension adds matrix runs.
- **The full Tauri runtime.** The harness runs against `vite
  preview` so it doesn't need a Tauri build per CI run. The
  small Tauri-bridge mock in `mocks/tauri.ts` substitutes for
  the real commands.

## Running

```sh
cd tests/visual
npm ci

# Build the desktop frontend + start vite preview + run the suite:
npm run test

# When you intentionally change a token / layout / copy and need
# to re-bless the baselines:
npm run update-baselines

# CI artefact viewer (HTML report):
npm run show-report
```

Baseline PNGs land under
`tests/visual/specs/onboarding.spec.ts-snapshots/`. Commit them
alongside the change that produced them; a token-bump PR's diff
should make sense to a reviewer who pulls the branch.

## How the bridge mock works

`mocks/tauri.ts` exports a `tauriInitScript` string that Playwright
injects via `page.addInitScript` before each spec navigates. The
script replaces `window.__TAURI_INTERNALS__.invoke` (the hook
`@tauri-apps/api` looks for) with a fakes table. Each command
returns whatever its production counterpart returns on success;
the wizard's UX renders identically.

## Updating baselines

Run `npm run update-baselines` locally; commit the snapshot
changes alongside the source change that produced them. A bare
"update baselines" PR with no source change is suspicious — push
back during review.

## Known limitations

- **CI snapshots can diverge from local.** Font rendering on the
  GitHub Linux runner differs subtly from a developer's local
  Chromium install. Baselines committed from local will likely
  diff on first CI run; commit baselines from CI ("download
  artefacts, scp to repo, commit") on first wire-up. Sprint 12+
  will tighten the rendering toolchain so this stops being an
  issue.
- **No reduced-motion variant covered yet.** `tokens.css` has a
  `prefers-reduced-motion` block; a Sprint 12+ spec captures the
  reduced-motion render path so future motion bugs don't slip
  through.
- **The bridge mock doesn't simulate errors.** A failing
  `byok_store_key` should render the inline error message in
  red; today's mock always succeeds. Sprint 12+ adds explicit
  error-path snapshots.

## Why Playwright (not Cypress, not raw Puppeteer)

- Playwright's `toHaveScreenshot` has a pixel-budget and
  per-platform-snapshot story out of the box; the alternatives
  needed external image-diff plumbing.
- The trace viewer (`npm run show-report`) is the best in class
  for visual debugging; reviewers can step through the failing
  test's clicks frame-by-frame.
- Already a TypeScript-native tool, matching the rest of the
  desktop tree.
