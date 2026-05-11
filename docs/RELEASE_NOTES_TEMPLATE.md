# Release notes template

Copy this file when cutting a release. Fill it in, then attach as the
GitHub release description for the `v<x.y.z>[-prerelease]` tag.

The release-stamping ritual (in order):

1. Update `VERSION` if the SemVer major.minor.patch changed. CMake's
   `project(VERSION ...)` reads from this file.
2. In `CHANGELOG.md`, demote `## [Unreleased]` to `## [<x.y.z>[-prerelease]] - YYYY-MM-DD`
   and open a fresh empty `## [Unreleased]` block at the top with the
   five sub-sections (`### Added` / `Changed` / `Fixed` / `Removed` / `Security`),
   each populated with `- (None this release.)`.
3. Tag: `git tag -a v<x.y.z>[-prerelease] -m "Release <x.y.z>[-prerelease]"`,
   signed by a release maintainer per `docs/GOVERNANCE.md`.
4. Push the tag. `.github/workflows/release.yml` picks it up,
   builds source / CLI / Python artefacts, and opens a draft
   GitHub release with this template pre-filled.
5. Publish the release.

---

## souxmar v<x.y.z>[-prerelease] — <one-line theme>

**Tag:** `v<x.y.z>[-prerelease]`
**ABI:** v<major>.<minor> (frozen final since ADR-0008)
**Date:** YYYY-MM-DD
**Type:** pre-release | release-candidate | stable

### What this release is

One paragraph. Who is this for? What can they do today that they
couldn't on the previous release?

### What landed

Bullet list of headline deliverables. Cross-reference the CHANGELOG
entry rather than duplicating the entire change list.

- ...
- ...

### What's deliberately not here

Honesty section. List the items that the marketing material might
imply are present but aren't. For pre-releases this is long; for
stable releases it tends to shrink.

- ...

### ABI status

State the current `SOUXMAR_ABI_VERSION_MAJOR` / `MINOR` pair + the
ratchet history since the last release. If this release added any
minor surfaces, list them so plugin authors know the floor.

### Download

| Artefact                                          | SHA-256 |
| ------------------------------------------------- | ------- |
| `souxmar-<version>-source.tar.gz`                 | `…`     |
| `souxmar-<version>-linux-x86_64.tar.gz`           | `…`     |
| `pysouxmar-<version>.tar.gz` (Python sdist)       | `…`     |

The GitHub release workflow attaches each artefact's checksum next to
its download. The Python sdist + binary tarballs are stamped with the
release tag so reproducible builds carry the version through.

### Install

```bash
# Source
curl -L https://github.com/souxmar/souxmar/releases/download/v<version>/souxmar-<version>-source.tar.gz | tar xz
cd souxmar-<version>
cmake --preset dev && cmake --build --preset dev

# Linux x86_64 CLI tarball
curl -L https://github.com/souxmar/souxmar/releases/download/v<version>/souxmar-<version>-linux-x86_64.tar.gz | tar xz
./souxmar-<version>-linux-x86_64/bin/souxmar version

# Python
pip install pysouxmar==<version>
```

### Known issues

Bug-aware list of "this works but you should know." Link the GitHub
issues. The release-blocker rules in `docs/GOVERNANCE.md` § Release
process say none of these may be P0 or P1.

### What's next

One paragraph linking the next sprint's plan + the next expected
release window.

### Contributors

`git shortlog -sn v<previous-version>..v<this-version>` — paste here.

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)
