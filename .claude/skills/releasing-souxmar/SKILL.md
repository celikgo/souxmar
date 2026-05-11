---
name: releasing-souxmar
description: Use when cutting a souxmar release (alpha, beta, RC, or stable). Walks through tagging, signing (Apple notarisation, Windows EV, Linux GPG), publishing, plugin index update, and announcement. Triggers on "release", "tag a release", "v0.9", "v1.0", "publish", "ship".
---

# Releasing souxmar

souxmar releases follow an 8-week cadence (per `docs/SPRINT_PLAN.md` and `docs/GOVERNANCE.md`). Releases are time-based, not feature-based: anything that is not green at cut-time ships in the next train.

## When to use this skill

- Cutting a planned release on the cadence.
- Cutting an emergency security release.
- Cutting a backport release (P0/P1 fix to the previous minor).
- Investigating a release-pipeline failure.

## When NOT to use this skill

- Day-to-day merging — that is the standard `main`-is-shippable flow.
- Promoting a Pro/Team/Enterprise service version — those have their own deployment cadence.

## Pre-release checklist (T-3 days)

1. **CI green for 3 consecutive nights** including determinism, sanitizers, perf gates, agent eval suite.
2. **All P0 / P1 bugs closed** (P2/P3 may roll forward).
3. **Changelog drafted** in `CHANGELOG.md` — author bullets per area; copy-edit in `docs/release-notes/`.
4. **Migration notes written** if any deprecation lapses or any user-visible default changes.
5. **Plugin conformance suite re-run** on all in-tree plugins to confirm ABI stability.
6. **License scan clean** in `THIRD_PARTY_LICENSES.md`.
7. **Docs site preview** built and reviewed.
8. **Security disclosures** (if any) coordinated with the disclosure timeline.

## Cutting the release

1. **Bump version** in `CMakeLists.txt`, `pyproject.toml`, `package.json` (desktop), `src-tauri/tauri.conf.json`. Use `tools/bump-version.sh <version>`.
2. **Tag the release** on `main`:
   ```bash
   git tag -s v1.0.0 -m "Release v1.0.0"
   git push origin v1.0.0
   ```
   Tags must be GPG-signed by a maintainer's release key.
3. **Trigger the release workflow** — push of a `v*` tag triggers `.github/workflows/release.yml` which:
   - Builds across Linux / macOS / Windows × x86_64 / arm64.
   - Runs the full test + conformance suite at the tag.
   - Builds Python wheels for the supported Python versions.
   - Builds desktop installers per OS.
4. **Wait for build matrix.** Typical wall time: 45–60 min. Failures here halt the release; investigate, do not push through.

## Signing

### macOS

- Apple Developer ID certificate stored in CI secrets (rotated annually).
- App is signed: `codesign --deep --force --sign "$DEV_ID" --options runtime souxmar.app`.
- Submitted for notarisation: `xcrun notarytool submit souxmar.dmg --apple-id ... --wait`.
- Stapled: `xcrun stapler staple souxmar.dmg`.
- **Failure mode (R-004):** notarisation queue stalls. Auto-retry with backoff; manual escalation if > 4 h.

### Windows

- EV code-signing certificate stored in HSM-backed CI vault (Sectigo or DigiCert).
- Sign with `signtool`: `signtool sign /tr http://timestamp... /td sha256 /fd sha256 /a souxmar.exe`.
- Same for the `.msi`.
- SmartScreen reputation builds slowly even with EV — anticipate "Windows protected your PC" prompts on the first downloads after a fresh build cert.

### Linux

- GPG-sign the tarballs and the `.deb` / `.rpm` packages.
- Detached signatures alongside artefacts (`*.tar.gz.sig`, `*.deb.sig`).
- AppImage signing via `appimage-sign`.

## Publishing

1. **Upload artefacts** to the static CDN (S3 + CloudFront mirror).
2. **Update the auto-update manifest** (`updates.json`) with the new version, signed.
3. **Publish Python wheels** to PyPI: `twine upload --sign dist/*.whl`.
4. **Create the GitHub release** with the changelog body, attach checksums, link to docs site.
5. **Update the docs site** — rebuild from the tag; deploy.
6. **Update the plugin index** if any in-tree plugin gained/lost capabilities at this release.

## Announcement

1. **Project blog** — short release-notes post.
2. **Discord / GitHub Discussions** — pinned post.
3. **Mailing list** — release announcement.
4. **Hacker News / r/CFD / r/FEA** — only for milestone releases (`v0.9`, `v1.0`, `v1.5` etc.), not point releases.
5. **Security disclosures** (if any) published to the security advisories page on the same day.

## Post-release

1. **Monitor Sentry** for new crash signatures in the first 24 h.
2. **Watch the auto-updater telemetry** for Pro-tier customers — % of clients on the new version after 48 h.
3. **Triage external bug reports** within the first week SLA.
4. **Schedule the post-release retro** — what was hard about this release? What needs to change for next time?

## Backport release

For P0 / P1 fixes to the previous minor:

1. Cherry-pick the fix onto the `release/<minor>` branch.
2. Run the same CI matrix.
3. Cut a patch release (e.g. `v1.0.1`) following the same signing + publishing flow.
4. **Do not** backport features. Only fixes.
5. Backport window: 1 minor cycle. Older releases are best-effort.

## Common mistakes

- Pushing the version-bump commit and the tag in the same operation. Push the bump, wait for CI, *then* tag.
- Tagging an unsigned commit. Tags must be GPG-signed.
- Forgetting to update the auto-update manifest. Users on auto-update will not see the release.
- Publishing wheels to PyPI before the GitHub release exists. Reverse the order; PyPI is hard to revoke.
- Releasing on Friday afternoon. There is no on-call urgency. Tuesday morning is the standard release day.
- Skipping the post-release retro. Releases are how we learn about our release process.

## Emergency security release

For severe security issues (CVSS > 7):

1. Coordinate with the reporter on disclosure timeline.
2. Develop the fix on a private fork to avoid premature disclosure.
3. Cut the release following the standard flow — but compress timelines.
4. Publish the security advisory **simultaneously** with the release artefacts. Not before; not after.
5. Backport to all supported releases (current minor + previous minor).
6. Post-mortem within 5 working days.

## Reference

- `docs/SPRINT_PLAN.md` — release cadence.
- `docs/GOVERNANCE.md` — release policy.
- `docs/ENGINEERING_PRACTICES.md` — incident response process.
- `.github/workflows/release.yml` — the release pipeline.
- `tools/bump-version.sh` — version-bump helper.
