#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail the build when souxmar's version is stated inconsistently.

VERSION is the single source of truth. CMake reads it, so the CLI's
`souxmar version` output follows automatically -- but only if the binary was
built from this tree, which is why --binary checks it directly when CI has one
to hand.

Everything else that repeats the version is checked against VERSION here. The
check is *complete by construction*: it discovers every version-bearing
manifest tracked by git, and fails on one it has not been told about. Adding a
new package to the tree therefore forces a decision -- does it ship as part of
souxmar (PRODUCT: it must carry VERSION) or is it versioned independently
(INDEPENDENT: it must say why) -- rather than letting a fourth value drift in
unnoticed, which is how this repository once ended up stating 0.9.0,
0.9.0-beta3, 0.0.1 and a README claim of v1.0.0 at the same time.

This gate exists because the README once announced a stable v1.0.0 that had no
tag, no release, and a VERSION file reading 0.9.0. That must not recur.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$")

# Manifests that ship as part of souxmar. Every one of these must state
# VERSION exactly. `kind` selects the reader; see read_version().
PRODUCT: dict[str, str] = {
    "vcpkg.json":                                  "json:version-string",
    "docs-site/package.json":                      "json:version",
    "src/desktop/package.json":                    "json:version",
    "src/desktop/src-tauri/tauri.conf.json":       "json:version",
    "src/desktop/src-tauri/Cargo.toml":            "toml",
    "src/desktop/src-tauri/souxmar-bridge/Cargo.toml": "toml",
    "bindings/python/pyproject.toml":              "toml",
}

# Generated lockfiles that restate the version of a crate/package above. They
# are .gitignore'd, so a fresh CI checkout does not have them -- but a
# developer's tree does, and a lockfile left at the old version is exactly the
# kind of stale restatement this gate exists to catch. Checked when present.
PRODUCT_IF_PRESENT: dict[str, str] = {
    "src/desktop/package-lock.json":    "npm-lock",
    "src/desktop/src-tauri/Cargo.lock": "cargo-lock:souxmar-desktop,souxmar-bridge",
}

# Manifests that are deliberately NOT the souxmar product version. Each needs a
# reason, and the reason is the point: an entry here is a claim that this thing
# is released on its own cadence, not a place to park a version you could not
# be bothered to reconcile.
INDEPENDENT: dict[str, str] = {
    "services/account-portal/Cargo.toml":     "hosted service, deployed continuously, not shipped in a souxmar release",
    "services/billing/Cargo.toml":            "hosted service, deployed continuously, not shipped in a souxmar release",
    "services/cloud-sync/Cargo.toml":         "hosted service, deployed continuously, not shipped in a souxmar release",
    "services/compute-offload/Cargo.toml":    "hosted service, deployed continuously, not shipped in a souxmar release",
    "services/managed-ai-proxy/Cargo.toml":   "hosted service, deployed continuously, not shipped in a souxmar release",
    "services/plugin-marketplace/Cargo.toml": "hosted service, deployed continuously, not shipped in a souxmar release",
    "tests/visual/package.json":              "test harness, never published or shipped",
}

# What counts as a version-bearing manifest for the completeness sweep.
MANIFEST_NAMES = {
    "package.json", "package-lock.json", "Cargo.toml", "Cargo.lock",
    "pyproject.toml", "tauri.conf.json", "vcpkg.json",
}

errors: list[str] = []
checks: list[str] = []


def check(label: str, actual: str | None, expected: str) -> None:
    if actual is None:
        errors.append(f"{label}: not found")
    elif actual != expected:
        errors.append(f"{label}: {actual!r} != VERSION {expected!r}")
    else:
        checks.append(f"{label}: {actual}")


def read_version(path: pathlib.Path, kind: str) -> list[tuple[str, str | None]]:
    """Return [(label, version-or-None)] for one manifest."""
    rel = path.relative_to(ROOT)
    if kind.startswith("json:"):
        key = kind.split(":", 1)[1]
        return [(f"{rel} {key}", json.loads(path.read_text()).get(key))]
    if kind == "toml":
        m = re.search(r'^version\s*=\s*"([^"]+)"', path.read_text(), re.M)
        return [(f"{rel} version", m.group(1) if m else None)]
    if kind == "npm-lock":
        # The root project appears twice: top level and as packages[""].
        doc = json.loads(path.read_text())
        return [
            (f"{rel} version", doc.get("version")),
            (f'{rel} packages[""].version', doc.get("packages", {}).get("", {}).get("version")),
        ]
    if kind.startswith("cargo-lock:"):
        text = path.read_text()
        out = []
        for crate in kind.split(":", 1)[1].split(","):
            m = re.search(rf'name = "{re.escape(crate)}"\nversion = "([^"]+)"', text)
            out.append((f"{rel} [{crate}]", m.group(1) if m else None))
        return out
    raise AssertionError(f"unknown manifest kind {kind!r}")


def tracked_manifests() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files"], capture_output=True, text=True, cwd=ROOT, check=True
    ).stdout.split()
    return sorted(p for p in out if pathlib.PurePosixPath(p).name in MANIFEST_NAMES)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", help="path to a built souxmar CLI to interrogate")
    args = ap.parse_args()

    version = (ROOT / "VERSION").read_text().strip()
    if not SEMVER.match(version):
        print(f"VERSION is not semver: {version!r}", file=sys.stderr)
        return 1
    checks.append(f"VERSION: {version}")

    # 1. Every product manifest states VERSION.
    for rel, kind in PRODUCT.items():
        path = ROOT / rel
        if not path.exists():
            errors.append(f"{rel}: listed as a product manifest but missing from the tree")
            continue
        for label, actual in read_version(path, kind):
            check(label, actual, version)

    for rel, kind in PRODUCT_IF_PRESENT.items():
        path = ROOT / rel
        if not path.exists():
            checks.append(f"{rel}: not generated in this checkout (skipped)")
            continue
        for label, actual in read_version(path, kind):
            check(label, actual, version)

    # 2. No version-bearing manifest is unaccounted for. This is what makes
    #    VERSION the single source of truth rather than one source among many.
    known = set(PRODUCT) | set(PRODUCT_IF_PRESENT) | set(INDEPENDENT)
    for rel in tracked_manifests():
        if rel not in known:
            errors.append(
                f"{rel}: version-bearing manifest this gate does not know about. Add it to "
                f"PRODUCT in scripts/check-version-consistency.py (it must then state "
                f"VERSION), or to INDEPENDENT with the reason it is versioned separately."
            )
    for rel in INDEPENDENT:
        if (ROOT / rel).exists():
            checks.append(f"{rel}: independent — {INDEPENDENT[rel]}")
        else:
            errors.append(f"{rel}: listed as independently versioned but missing from the tree")

    # 3. README: the version named on the status line.
    readme = (ROOT / "README.md").read_text()
    stated = re.search(r"<!-- version -->\s*`?v?([0-9][0-9A-Za-z.\-]*)`?", readme)
    check("README.md stated version", stated.group(1) if stated else None, version)

    # 4. CHANGELOG: a version that is not a work in progress needs its own section.
    changelog = (ROOT / "CHANGELOG.md").read_text()
    if re.search(rf"^## \[{re.escape(version)}\]", changelog, re.M):
        checks.append(f"CHANGELOG.md: [{version}] section present")
    else:
        errors.append(
            f"CHANGELOG.md has no '## [{version}]' section — either add one or "
            f"bump VERSION past the released one"
        )

    # 5. git tag, when the checkout is on one.
    tag = subprocess.run(
        ["git", "describe", "--tags", "--exact-match"],
        capture_output=True, text=True, cwd=ROOT,
    )
    if tag.returncode == 0:
        check("git tag", tag.stdout.strip().lstrip("v"), version)
    else:
        checks.append("git tag: not on a tag (skipped)")

    # 6. The built CLI, when CI hands us one.
    if args.binary:
        out = subprocess.run(
            [args.binary, "version"], capture_output=True, text=True,
        ).stdout
        m = re.search(r"souxmar\s+([0-9][0-9A-Za-z.\-]*)", out)
        check(f"{args.binary} version", m.group(1) if m else None, version)
    else:
        checks.append("CLI --version: no binary given (skipped)")

    for c in checks:
        print(f"  ok   {c}")
    for e in errors:
        print(f"  FAIL {e}", file=sys.stderr)
    if errors:
        print(
            f"\n{len(errors)} version inconsistency/ies. VERSION is the single "
            f"source of truth; make everything else agree with it.",
            file=sys.stderr,
        )
        return 1
    print("\nversion is stated consistently everywhere.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
