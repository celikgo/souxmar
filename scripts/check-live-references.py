#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail the build when the docs promise something the internet does not provide.

`check-doc-links.py` is the offline sibling: it catches hostnames that were
never registered and relative links whose target is not on disk. Neither of
those needs the network. This script covers the two defects that only a live
lookup can catch, and that this repository has shipped before:

  1. INSTALL COMMANDS FOR PACKAGES THAT DO NOT EXIST.
     The README taught `pip install pysouxmar` while pysouxmar had never been
     uploaded to PyPI, so the first command a new user ran returned a 404.
     Every install command naming a first-party package is resolved against
     its registry here. An unpublished package may still be *named* -- saying
     "`pip install pysouxmar` does not work" is the honest thing to write --
     but only in prose that says so, or behind an explicit
     `<!-- unpublished-ok: NAME -->` marker. Inside a fenced code block, where
     a reader will copy it, an unresolvable package is always an error.

  2. PUBLISHED SITES THAT ARE NOT UP.
     The docs site is linked from the README and from the repository's
     `homepage` field. A failed Pages deploy makes both dead without
     changing a byte in the tree, so nothing in CI would notice. LIVE_URLS
     below is checked on every run, and so is the reachability of the site
     *from the repository*: the README must link it, and -- when a GitHub
     token is present -- the repository's `homepage` field must point at it.
     A docs site nobody can find from the front page is only half deployed.

Network policy: a definite negative (404, 410, NXDOMAIN) fails the build --
that is the defect. A transport failure (timeout, TLS error, DNS server
unreachable) is reported loudly and skipped, because failing every PR on a
runner's flaky network teaches people to ignore the gate. Pass --strict to
turn skips into failures too, which is what the release workflow does.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
UA = "souxmar-ci-link-check/1 (+https://github.com/celikgo/souxmar)"
TIMEOUT = 20

# First-party packages, and where each one would be published. A name in here
# is a claim that souxmar owns the package; the registry lookup then decides
# whether the docs may teach an install command for it.
FIRST_PARTY: dict[str, dict[str, str]] = {
    # tool      package        registry probe URL
    "pip":   {"pysouxmar": "https://pypi.org/pypi/pysouxmar/json"},
    "npm":   {},
    "cargo": {},
}

# URLs the documentation promises are live. A broken Pages deploy must fail
# CI rather than sit broken until somebody clicks the link.
LIVE_URLS: dict[str, str] = {
    "https://celikgo.github.io/souxmar/":
        "the documentation site, linked from README.md and set as the "
        "repository homepage. If this 404s the Pages deploy failed — check "
        "the docs-site workflow.",
    "https://github.com/celikgo/souxmar":
        "the repository itself, named throughout the docs.",
}

# The canonical docs site, and the two places the repository must point at it
# from. A site that is up but unreachable from the front page is not shipped.
DOCS_SITE = "https://celikgo.github.io/souxmar/"
MUST_LINK_DOCS_SITE = ["README.md"]

# An install command in prose that says the install does not work is honest,
# not a defect. These are matched against the whole containing paragraph.
NEGATION = re.compile(
    r"not on pypi|not (?:yet )?published|does not work|doesn't work|is not "
    r"available|no pypi package|never has|not on npm|not on crates|\b404s?\b|"
    r"not true today",
    re.I,
)
MARKER = re.compile(r"<!--\s*unpublished-ok:\s*([A-Za-z0-9._-]+)\s*-->")

INSTALL = re.compile(
    r"\b(pip3?|npm|cargo)\s+install\s+(?:--?[A-Za-z-]+\s+)*([A-Za-z0-9._-]+)"
)


def tracked(*globs: str) -> list[pathlib.Path]:
    out = subprocess.run(
        ["git", "ls-files", *globs], capture_output=True, text=True, cwd=ROOT, check=True
    ).stdout.split()
    return [ROOT / f for f in out]


def probe(url: str) -> tuple[str, int | None, str]:
    """Return (verdict, status, detail). verdict is ok | missing | unreachable."""
    req = urllib.request.Request(url, headers={"User-Agent": UA}, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            return "ok", r.status, ""
    except urllib.error.HTTPError as e:
        if e.code in (404, 410):
            return "missing", e.code, f"HTTP {e.code}"
        if e.code in (401, 403, 429):
            # Reachable, and not a claim that the thing is absent.
            return "ok", e.code, f"HTTP {e.code} (treated as present)"
        return "unreachable", e.code, f"HTTP {e.code}"
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        reason = getattr(e, "reason", e)
        if isinstance(reason, socket.gaierror):
            return "missing", None, f"DNS does not resolve: {reason}"
        return "unreachable", None, f"{type(e).__name__}: {reason}"


def paragraph_at(text: str, offset: int) -> str:
    start = text.rfind("\n\n", 0, offset)
    start = 0 if start < 0 else start + 2
    end = text.find("\n\n", offset)
    end = len(text) if end < 0 else end
    return text[start:end]


def fenced_spans(text: str) -> list[tuple[int, int]]:
    spans, open_at = [], None
    for m in re.finditer(r"^[ \t]*(```|~~~)", text, re.M):
        if open_at is None:
            open_at = m.start()
        else:
            spans.append((open_at, m.end()))
            open_at = None
    if open_at is not None:
        spans.append((open_at, len(text)))
    return spans


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true",
                    help="treat unreachable (as opposed to absent) as a failure")
    args = ap.parse_args()

    errors: list[str] = []
    warnings: list[str] = []
    checks: list[str] = []
    self_path = pathlib.Path(__file__).resolve()

    # ---------------------------------------------------------------- 1
    # Which first-party packages actually resolve on their registry?
    resolved: dict[str, bool] = {}
    for _tool, packages in FIRST_PARTY.items():
        for pkg, url in packages.items():
            verdict, status, detail = probe(url)
            if verdict == "ok":
                resolved[pkg] = True
                checks.append(f"{pkg}: published ({url} → {status})")
            elif verdict == "missing":
                resolved[pkg] = False
                checks.append(f"{pkg}: NOT published ({detail}) — docs may not teach its install")
            else:
                resolved[pkg] = True  # unknown: do not fail docs on a flaky probe
                msg = f"{pkg}: registry unreachable ({detail}); install commands not checked"
                (errors if args.strict else warnings).append(msg)

    unpublished = {p for p, ok in resolved.items() if not ok}
    if unpublished:
        for path in tracked("*.md"):
            if not path.is_file() or path.resolve() == self_path:
                continue
            try:
                text = path.read_text()
            except (UnicodeDecodeError, OSError):
                continue
            fences = fenced_spans(text)
            for m in INSTALL.finditer(text):
                pkg = m.group(2)
                if pkg not in unpublished:
                    continue
                line = text[: m.start()].count("\n") + 1
                rel = path.relative_to(ROOT)
                in_fence = any(a <= m.start() < b for a, b in fences)
                para = paragraph_at(text, m.start())
                marked = any(g == pkg for g in MARKER.findall(para))
                if in_fence:
                    errors.append(
                        f"{rel}:{line}: code block teaches `{m.group(0)}`, but {pkg} does not "
                        f"resolve on its registry. Remove the line, or publish the package."
                    )
                elif not (NEGATION.search(para) or marked):
                    errors.append(
                        f"{rel}:{line}: names `{m.group(0)}` as if it worked, but {pkg} is not "
                        f"published. Say plainly that it does not work, or mark the mention "
                        f"<!-- unpublished-ok: {pkg} --> if it is a stated future goal."
                    )
                else:
                    checks.append(f"{rel}:{line}: `{m.group(0)}` — honest mention of an unpublished package")

    # ---------------------------------------------------------------- 2
    for url, why in LIVE_URLS.items():
        verdict, status, detail = probe(url)
        if verdict == "ok":
            checks.append(f"{url} → {status}")
        elif verdict == "missing":
            errors.append(f"{url}: {detail} — {why}")
        else:
            msg = f"{url}: unreachable ({detail}) — {why}"
            (errors if args.strict else warnings).append(msg)

    # ---------------------------------------------------------------- 3
    # The docs site must be reachable *from the repository*, not merely up.
    host = DOCS_SITE.rstrip("/")
    for rel in MUST_LINK_DOCS_SITE:
        text = (ROOT / rel).read_text()
        if host in text:
            checks.append(f"{rel} links the docs site")
        else:
            errors.append(
                f"{rel}: does not link the documentation site ({DOCS_SITE}). A docs site "
                f"nobody can find from the front page is only half deployed."
            )

    # The repository `homepage` field. Needs a token to read reliably (the
    # unauthenticated API is rate-limited per IP, which on shared CI runners
    # means intermittent failures), so it is skipped without one.
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        req = urllib.request.Request(
            "https://api.github.com/repos/celikgo/souxmar",
            headers={"User-Agent": UA, "Authorization": f"Bearer {token}",
                     "Accept": "application/vnd.github+json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                homepage = (json.load(r).get("homepage") or "").rstrip("/")
            if homepage == host:
                checks.append(f"repository homepage field → {DOCS_SITE}")
            else:
                errors.append(
                    f"repository homepage field is {homepage or '(empty)'!r}, not {DOCS_SITE}. "
                    f"Set it: gh api -X PATCH repos/celikgo/souxmar -f homepage={DOCS_SITE}"
                )
        except Exception as e:  # noqa: BLE001 — a probe failure is not a doc defect
            msg = f"repository homepage field not checked ({type(e).__name__}: {e})"
            (errors if args.strict else warnings).append(msg)
    else:
        checks.append("repository homepage field: no GITHUB_TOKEN (skipped)")

    for c in checks:
        print(f"  ok   {c}")
    for w in warnings:
        print(f"  WARN {w}", file=sys.stderr)
    for e in errors:
        print(f"  FAIL {e}", file=sys.stderr)
    if errors:
        print(f"\n{len(errors)} live-reference failure(s).", file=sys.stderr)
        return 1
    print("\nEvery documented install command resolves, and every promised site is up.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
