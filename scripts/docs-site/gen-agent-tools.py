#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# scripts/docs-site/gen-agent-tools.py — generate the public tool
# catalogue page on docs.souxmar.dev from the binary's own
# `souxmar agent list --json` output.
#
# Sprint 13 push 2 closes the Sprint 12 retro's "/agents/tools page
# is hand-curated and drifts" gap. The page is now built from the
# same tool registry the engine ships with — drift between the
# binary and the docs is structurally impossible.
#
# Invocation:
#
#   scripts/docs-site/gen-agent-tools.py \
#       --engine build/dev/tools/souxmar/souxmar \
#       --out    docs-site/agents/tools.md
#
# In CI (.github/workflows/docs-site.yml, wired in this push):
# every master-push that touches docs-site/ or scripts/docs-site/
# re-runs this and commits the regenerated tools.md only if it
# actually changed (otherwise the workflow is idempotent).

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import textwrap
from pathlib import Path


HEADER = textwrap.dedent("""\
    <!--
      This file is **generated** by scripts/docs-site/gen-agent-tools.py.
      Do not hand-edit — your changes will be overwritten on the next
      docs-site build. To change a tool's name / description / category,
      change it at the source (include/souxmar/ai/*.h + the matching
      src/ai/tools/*.cpp) and rebuild the docs site.
      Generator schema version: 1.
    -->

    # Tool catalogue

    Every tool the souxmar agent can call is listed here. The list is
    generated directly from `souxmar agent list --json` — what you see
    below is exactly what the engine ships in this revision of the
    binary. The contract is **frozen final at v1**
    ([ADR-0011](https://github.com/souxmar/souxmar/blob/master/docs/adr/0011-tool-contract-v1-final-freeze.md)).

    """)


CONFIRMATION_HELP = {
    "auto":            "no confirmation prompt — runs as soon as the agent calls it",
    "confirm-once":    "first call in a session prompts; subsequent calls auto-proceed",
    "confirm-always":  "every call prompts; never bypassed in a session",
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--engine", required=True, type=Path,
                   help="Path to the souxmar CLI binary.")
    p.add_argument("--out", required=True, type=Path,
                   help="Output markdown path (e.g. docs-site/agents/tools.md).")
    p.add_argument("--check-only", action="store_true",
                   help="Fail (exit 1) if the regenerated content differs "
                        "from the existing file, but do not overwrite. "
                        "Used by CI to detect stale docs.")
    return p.parse_args(argv)


def fetch_tool_catalogue(engine: Path) -> dict:
    if not engine.exists():
        raise SystemExit(f"engine binary not found: {engine}")
    if not engine.is_file() or not engine.stat().st_mode & 0o111:
        raise SystemExit(f"engine binary not executable: {engine}")
    proc = subprocess.run(
        [str(engine), "agent", "list", "--json"],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"`{engine} agent list --json` exited {proc.returncode}:\n"
            f"  stdout: {proc.stdout[:400]!r}\n"
            f"  stderr: {proc.stderr[:400]!r}"
        )
    try:
        catalogue = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise SystemExit(
            f"agent list --json output is not valid JSON: {exc}\n"
            f"  first 400 bytes: {proc.stdout[:400]!r}"
        )
    if catalogue.get("schema") != 1:
        raise SystemExit(
            f"unexpected schema in agent list output: "
            f"{catalogue.get('schema')!r} (expected 1)"
        )
    return catalogue


def group_by_category(tools: list[dict]) -> dict[str, list[dict]]:
    groups: dict[str, list[dict]] = {}
    for t in tools:
        groups.setdefault(t["category"] or "Other", []).append(t)
    # Order categories in a stable, human-curated order. Anything not
    # listed lands at the end alphabetically — additive Tier-0.
    preferred = [
        "Read", "Mesh", "BC", "Material", "Solve", "Field",
        "Pipeline", "Discovery", "Export", "UI",
    ]
    ordered = {c: groups.pop(c) for c in preferred if c in groups}
    for c in sorted(groups):
        ordered[c] = groups[c]
    return ordered


def render(catalogue: dict) -> str:
    tools = catalogue["tools"]
    count = catalogue.get("tool_count", len(tools))
    contract_version = catalogue.get("contract_version", "v1")

    out = [HEADER]
    out.append(f"**Contract version:** `{contract_version}`  ")
    out.append(f"**Tool count in this build:** **{count}**  \n\n")

    groups = group_by_category(tools)

    out.append("## At a glance\n")
    out.append("| Category | Tools |\n")
    out.append("| -------- | ----- |\n")
    for cat, ts in groups.items():
        names = ", ".join(f"`{t['name']}`" for t in ts)
        out.append(f"| {cat} | {names} |\n")
    out.append("\n")

    out.append("## Confirmation policies\n\n")
    for k, v in CONFIRMATION_HELP.items():
        out.append(f"- **`{k}`** — {v}.\n")
    out.append("\n")

    out.append("## All tools\n\n")
    for cat, ts in groups.items():
        out.append(f"### {cat}\n\n")
        for t in ts:
            out.append(f"#### `{t['name']}`\n\n")
            out.append(f"- **Category:** `{t['category']}`\n")
            out.append(f"- **Confirmation:** `{t['confirmation']}`\n\n")
            if t["description"]:
                out.append(t["description"].strip() + "\n\n")
            else:
                out.append("_No description recorded._\n\n")

    out.append(textwrap.dedent("""\

        ## How this page is generated

        ```sh
        scripts/docs-site/gen-agent-tools.py \\
            --engine build/dev/tools/souxmar/souxmar \\
            --out    docs-site/agents/tools.md
        ```

        Re-runs in CI on every master-push that touches `docs-site/` or
        `scripts/docs-site/`. To verify it's in sync locally:

        ```sh
        scripts/docs-site/gen-agent-tools.py \\
            --engine build/dev/tools/souxmar/souxmar \\
            --out    docs-site/agents/tools.md \\
            --check-only
        ```
    """))

    return "".join(out)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    catalogue = fetch_tool_catalogue(args.engine)
    rendered = render(catalogue)

    if args.check_only:
        existing = args.out.read_text(encoding="utf-8") if args.out.exists() else ""
        if existing != rendered:
            print(
                f"docs out of date: {args.out} differs from the generator output",
                file=sys.stderr,
            )
            print("re-run the generator without --check-only to update.",
                  file=sys.stderr)
            return 1
        print(f"{args.out} is in sync with the engine binary.")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered, encoding="utf-8")
    print(f"wrote {args.out} ({len(rendered)} bytes, "
          f"{catalogue['tool_count']} tools)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
