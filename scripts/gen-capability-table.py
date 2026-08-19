#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate docs/CAPABILITIES.md from the repository itself.

The point of this script is that the capability table cannot drift away from
what the code actually ships. Three of the four columns are read straight out
of the tree and are not editable by hand:

  * the capability id set comes from every examples/plugins/*/souxmar-plugin.toml
  * whether a capability is built by default comes from examples/CMakeLists.txt
  * the covering test comes from grepping tests/integration/ for the id

The fourth column -- what *kind* of model sits behind the capability -- is the
one thing no static read can determine, so it is declared in
docs/capability-states.toml and this script refuses to run when that file and
the code disagree:

  * a capability with no declared state is an error
  * a declared state for a capability that no longer exists is an error
  * declaring "implemented and tested" with no covering integration test is an
    error, and so is declaring "implemented, untested" when one exists

Run with --check in CI to fail the build when the committed table is stale.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

try:                       # Python 3.11+
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib  # type: ignore

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLUGINS = ROOT / "examples" / "plugins"
STATES = ROOT / "docs" / "capability-states.toml"
OUT = ROOT / "docs" / "CAPABILITIES.md"
INTEGRATION = ROOT / "tests" / "integration"
TOOLS_DIR = ROOT / "src" / "ai" / "tools"
EVALS = ROOT / "evals" / "v1"

VALID_STATES = {
    "implemented and tested",
    "implemented, untested",
    "stub",
    "planned",
}


def die(msg: str) -> None:
    print(f"gen-capability-table: error: {msg}", file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------------------
# Read the tree
# --------------------------------------------------------------------------
def read_build_guards() -> dict[str, str | None]:
    """plugin dir -> None when built unconditionally, else the guarding option."""
    text = (ROOT / "examples" / "CMakeLists.txt").read_text()
    guards: dict[str, str | None] = {}
    guard: str | None = None
    for line in text.splitlines():
        s = line.strip()
        if m := re.match(r"if\(([A-Z0-9_]+)\)", s):
            guard = m.group(1)
        elif s.startswith("endif("):
            guard = None
        elif m := re.match(r"add_subdirectory\(plugins/([\w-]+)\)", s):
            guards[m.group(1)] = guard
    return guards


def read_plugins() -> list[dict]:
    plugins = []
    for manifest in sorted(PLUGINS.glob("*/souxmar-plugin.toml")):
        data = tomllib.loads(manifest.read_text())
        plugins.append(
            {
                "dir": manifest.parent.name,
                "name": data["plugin"]["name"],
                "capabilities": list(data["plugin"]["capabilities"]["provides"]),
            }
        )
    return plugins


def integration_tests_for(cap: str) -> list[str]:
    """Tests that actually exercise the capability.

    Only tests/integration/ counts. tests/unit/test_manifest.cpp and
    tests/unit/test_plugin_index.cpp mention capability ids as parser input --
    naming a string is not running the code behind it.
    """
    hits = []
    for f in sorted(INTEGRATION.glob("test_*.cpp")):
        if f'"{cap}"' in f.read_text():
            hits.append(f"tests/integration/{f.name}")
    return hits


def read_agent_tools() -> list[str]:
    names = []
    for f in sorted(TOOLS_DIR.glob("*.cpp")):
        names += re.findall(r't\.name\s*=\s*"([a-z_]+)"', f.read_text())
    registered = len(
        re.findall(r"r\.add\(", (TOOLS_DIR / "default_registry.cpp").read_text())
    )
    if registered != len(names):
        die(
            f"{registered} tools registered in default_registry.cpp but "
            f"{len(names)} tool names defined in src/ai/tools/*.cpp"
        )
    return sorted(names)


def tool_coverage(tool: str) -> tuple[bool, list[str]]:
    unit = f'"{tool}"' in (ROOT / "tests" / "unit" / "test_ai_tools.cpp").read_text()
    evals = [
        f"evals/v1/{f.name}"
        for f in sorted(EVALS.glob("*.yaml"))
        if re.search(rf"\b{re.escape(tool)}\b", f.read_text())
    ]
    return unit, evals


# --------------------------------------------------------------------------
# Render
# --------------------------------------------------------------------------
def render() -> str:
    if not STATES.exists():
        die(f"{STATES.relative_to(ROOT)} is missing")
    declared = tomllib.loads(STATES.read_text())
    cap_states = declared.get("capability", {})

    guards = read_build_guards()
    plugins = read_plugins()

    found = {c for p in plugins for c in p["capabilities"]}
    missing = sorted(found - set(cap_states))
    if missing:
        die(
            "these capabilities ship but have no entry in "
            f"docs/capability-states.toml: {', '.join(missing)}"
        )
    orphan = sorted(set(cap_states) - found)
    if orphan:
        die(
            "docs/capability-states.toml declares capabilities that no plugin "
            f"provides: {', '.join(orphan)}"
        )

    rows, errors = [], []
    for p in sorted(plugins, key=lambda p: p["dir"]):
        guard = guards.get(p["dir"], "NOT BUILT")
        for cap in sorted(p["capabilities"]):
            entry = cap_states[cap]
            state = entry["state"]
            if state not in VALID_STATES:
                die(f"{cap}: unknown state {state!r}")
            tests = integration_tests_for(cap)
            if state == "implemented and tested" and not tests:
                errors.append(
                    f"{cap} is declared 'implemented and tested' but no test in "
                    f"tests/integration/ names it"
                )
            if state == "implemented, untested" and tests:
                errors.append(
                    f"{cap} is declared 'implemented, untested' but "
                    f"{tests[0]} exercises it"
                )
            rows.append(
                {
                    "cap": cap,
                    "plugin": p["dir"],
                    "state": state,
                    "guard": guard,
                    "tests": tests,
                    "note": entry.get("note", ""),
                }
            )
    if errors:
        die("declared state contradicts the tree:\n  - " + "\n  - ".join(errors))

    tools = read_agent_tools()

    out = []
    w = out.append
    w("<!-- GENERATED FILE — DO NOT EDIT.")
    w("     Regenerate with: python3 scripts/gen-capability-table.py")
    w("     CI runs the same script with --check and fails when this is stale. -->")
    w("")
    w("# Capabilities")
    w("")
    w("Everything souxmar can actually do, read out of the tree rather than")
    w("written down: the ids come from the plugin manifests, the default-build")
    w("column from `examples/CMakeLists.txt`, and the test column from grepping")
    w("`tests/integration/` for each id.")
    w("")
    w("**How to read the state column.**")
    w("")
    w("| State | Means |")
    w("| --- | --- |")
    w("| `implemented and tested` | Does what its id says, and a test in `tests/integration/` runs it. |")
    w("| `implemented, untested` | Does what its id says; nothing in `tests/integration/` covers it. |")
    w("| `stub` | Registers the id and returns a plausible field so the pipeline runs end to end. Not the computation the id implies. |")
    w("| `planned` | Id is reserved; no implementation. |")
    w("")
    w("A test naming a capability id in `tests/unit/test_manifest.cpp` or")
    w("`tests/unit/test_plugin_index.cpp` does **not** count as coverage here —")
    w("those tests feed ids to a parser, they do not run the code behind them.")
    w("")
    w("The `Model` column is the honest one: most of these are closed-form or")
    w("heuristic engineering models with a cited source, not discretised solvers.")
    w("See [`PHYSICS.md`](PHYSICS.md) for the equations and validity envelopes.")
    w("")
    w("## Plugin capabilities")
    w("")
    w("| Capability | Plugin | State | Model | Default build | Covering test |")
    w("| --- | --- | --- | --- | --- | --- |")
    for r in rows:
        tests = "<br>".join(f"`{t}`" for t in r["tests"]) or "— none —"
        build = "always-on" if r["guard"] is None else f"opt-in (`{r['guard']}`)"
        w(
            f"| `{r['cap']}` | `{r['plugin']}` | {r['state']} | {r['note']} "
            f"| {build} | {tests} |"
        )
    w("")
    w(f"{len(rows)} capabilities across {len(plugins)} in-tree plugins.")
    w("")
    w("## Agent tools")
    w("")
    w("The agent tool contract is v1 FINAL; the catalogue below is assembled by")
    w("`default_v1_tools()` in `src/ai/tools/default_registry.cpp` and read here")
    w("from the `t.name` assignments in `src/ai/tools/*.cpp`.")
    w("")
    w("| Tool | Unit test | Eval |")
    w("| --- | --- | --- |")
    for t in tools:
        unit, evals = tool_coverage(t)
        if not evals:
            ev = "— none —"
        elif len(evals) <= 3:
            ev = "<br>".join(f"`{e}`" for e in evals)
        else:
            shown = "<br>".join(f"`{e}`" for e in evals[:3])
            ev = f"{shown}<br>+{len(evals) - 3} more"
        w(f"| `{t}` | {'`tests/unit/test_ai_tools.cpp`' if unit else '— none —'} | {ev} |")
    w("")
    w(f"{len(tools)} tools.")
    w("")
    w("## What is deliberately not here")
    w("")
    for line in declared.get("absent", {}).get("notes", []):
        w(f"- {line}")
    w("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if the committed table is stale")
    args = ap.parse_args()

    text = render()
    if args.check:
        current = OUT.read_text() if OUT.exists() else ""
        if current != text:
            print(
                "docs/CAPABILITIES.md is out of date.\n"
                "Regenerate it with: python3 scripts/gen-capability-table.py",
                file=sys.stderr,
            )
            return 1
        print("docs/CAPABILITIES.md is up to date.")
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
