#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail the build when souxmar's version is stated inconsistently.

VERSION is the single source of truth. CMake reads it, so the CLI's
`souxmar version` output follows automatically -- but only if the binary was
built from this tree, which is why --binary checks it directly when CI has one
to hand.

Everything else that repeats the version is checked against VERSION here:

  VERSION                 the canonical string
  vcpkg.json              "version-string"
  docs-site/package.json  "version"
  README.md               the version named in the status line
  CHANGELOG.md            a released version must have its own section
  git tag                 when run on a tag, the tag must match

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

errors: list[str] = []
checks: list[str] = []


def check(label: str, actual: str | None, expected: str) -> None:
    if actual is None:
        errors.append(f"{label}: not found")
    elif actual != expected:
        errors.append(f"{label}: {actual!r} != VERSION {expected!r}")
    else:
        checks.append(f"{label}: {actual}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", help="path to a built souxmar CLI to interrogate")
    args = ap.parse_args()

    version = (ROOT / "VERSION").read_text().strip()
    if not SEMVER.match(version):
        print(f"VERSION is not semver: {version!r}", file=sys.stderr)
        return 1
    checks.append(f"VERSION: {version}")

    # vcpkg.json
    vcpkg = json.loads((ROOT / "vcpkg.json").read_text())
    check("vcpkg.json version-string", vcpkg.get("version-string"), version)

    # docs-site/package.json
    pkg_path = ROOT / "docs-site" / "package.json"
    if pkg_path.exists():
        check("docs-site/package.json version", json.loads(pkg_path.read_text()).get("version"), version)

    # README: the version named on the status line.
    readme = (ROOT / "README.md").read_text()
    stated = re.search(r"<!-- version -->\s*`?v?([0-9][0-9A-Za-z.\-]*)`?", readme)
    check("README.md stated version", stated.group(1) if stated else None, version)

    # CHANGELOG: a version that is not a work in progress needs its own section.
    changelog = (ROOT / "CHANGELOG.md").read_text()
    if re.search(rf"^## \[{re.escape(version)}\]", changelog, re.M):
        checks.append(f"CHANGELOG.md: [{version}] section present")
    else:
        errors.append(
            f"CHANGELOG.md has no '## [{version}]' section — either add one or "
            f"bump VERSION past the released one"
        )

    # git tag, when the checkout is on one.
    tag = subprocess.run(
        ["git", "describe", "--tags", "--exact-match"],
        capture_output=True, text=True, cwd=ROOT,
    )
    if tag.returncode == 0:
        check("git tag", tag.stdout.strip().lstrip("v"), version)
    else:
        checks.append("git tag: not on a tag (skipped)")

    # The built CLI, when CI hands us one.
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
