# Changelog

All notable changes to souxmar are documented here. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The plugin C ABI version is tracked separately and is independent of the project version. ABI v1 is frozen for the entire 1.x series; see [ADR-0001](docs/adr/0001-c-abi-for-plugins.md).

## [Unreleased]

### Added

- Sprint 0 scaffolding: top-level `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` manifest with feature-gated heavy dependencies (OpenCASCADE, Gmsh, PETSc, VTK, Eigen, pybind11).
- `cmake/` helper modules: options, warnings, sanitisers (ASan / TSan / UBSan / coverage).
- `libsouxmar-core` skeleton with version introspection (`souxmar::version()`, `version_string()`, `abi_version()`).
- Unit-test harness (GoogleTest) with version smoke tests.
- CI workflows: build matrix across Linux (gcc-13, clang-17), macOS (arm64 AppleClang), Windows (MSVC); clang-format check; secret-shaped-string scan; DCO sign-off enforcement.
- Nightly workflow: ASan and TSan runs on Linux clang-17.
- Repository governance: CODEOWNERS, PR template, issue templates (bug / feature / RFC), Dependabot for GitHub Actions.
- Contribution docs: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1 by reference), RFC template at `docs/rfcs/0000-template.md`.
- `THIRD_PARTY_LICENSES.md` baseline; `.editorconfig`, `.clang-format`, `.clang-tidy`, `.gitattributes`.

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## How to read this file

- `[Unreleased]` accumulates changes between releases. At release time, the section is renamed to `[X.Y.Z] - YYYY-MM-DD` and a fresh `[Unreleased]` is opened.
- Sections are added only when they have content; empty sections are omitted from a tagged release.
- Each entry is one line, present tense, references the PR or issue when meaningful.
- ABI / pipeline-format / agent-tool changes are called out explicitly with a `**[ABI v1]**` / `**[pipeline-format v1]**` / `**[agent-tool v1]**` prefix so plugin authors and integrators can scan the file for impact.
