#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail the build on documentation links that cannot resolve.

Two classes of defect, both of which this repository has shipped before:

  1. Hostnames that do not exist. `souxmar.dev` and every subdomain of it were
     referenced 107 times while resolving NXDOMAIN, and `github.com/souxmar`
     96 times against an organisation that was never created. Placeholders for
     services that are not deployed must use the RFC 6761 reserved `.invalid`
     TLD, which is guaranteed never to resolve and so cannot be mistaken for a
     live endpoint.

  2. Relative markdown links pointing at files that are not there — usually
     the aftermath of moving or deleting a document.

Run it with no arguments from anywhere in the tree.
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Hostnames this project must never reference again, with what to use instead.
FORBIDDEN = {
    r"souxmar\.dev": (
        "souxmar.dev and its subdomains are unregistered (NXDOMAIN). Use a real "
        "target — https://github.com/celikgo/souxmar, a relative path into docs/, "
        "or https://celikgo.github.io/souxmar — or, for an endpoint that is not "
        "deployed, a *.souxmar.invalid placeholder."
    ),
    r"github\.com/souxmar\b": (
        "The github.com/souxmar organisation does not exist. The repository is "
        "github.com/celikgo/souxmar."
    ),
}

# Relative link targets that are valid on github.com but are not files on disk.
GITHUB_RELATIVE = re.compile(r"^\.\./\.\./(issues|pulls?|discussions|wiki|releases|security)\b")


def tracked(*globs: str) -> list[pathlib.Path]:
    out = subprocess.run(
        ["git", "ls-files", *globs], capture_output=True, text=True, cwd=ROOT, check=True
    ).stdout.split()
    return [ROOT / f for f in out]


def main() -> int:
    errors: list[str] = []

    # 1. Dead hostnames, anywhere in the tree. This file necessarily names the
    #    patterns it bans, so it exempts itself.
    self_path = pathlib.Path(__file__).resolve()
    for path in tracked():
        if not path.is_file() or path.resolve() == self_path:
            continue
        try:
            text = path.read_text()
        except (UnicodeDecodeError, OSError):
            continue
        for pattern, advice in FORBIDDEN.items():
            for n, line in enumerate(text.splitlines(), 1):
                if re.search(pattern, line):
                    rel = path.relative_to(ROOT)
                    errors.append(f"{rel}:{n}: dead reference — {advice}")

    # 2. Relative markdown links.
    for path in tracked("*.md"):
        if not path.is_file():
            continue
        text = path.read_text()
        for m in re.finditer(r"\]\(([^)]+)\)", text):
            target = m.group(1).split("#")[0].strip()
            if not target or target.startswith(("http://", "https://", "mailto:", "#", "/")):
                continue
            if GITHUB_RELATIVE.match(target):
                continue
            if not (path.parent / target).exists():
                line = text[: m.start()].count("\n") + 1
                errors.append(
                    f"{path.relative_to(ROOT)}:{line}: link target does not exist: {target}"
                )

    if errors:
        for e in errors[:60]:
            print(f"  FAIL {e}", file=sys.stderr)
        if len(errors) > 60:
            print(f"  ... and {len(errors) - 60} more", file=sys.stderr)
        print(f"\n{len(errors)} broken documentation reference(s).", file=sys.stderr)
        return 1

    print("No dead hostnames and no broken relative links.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
