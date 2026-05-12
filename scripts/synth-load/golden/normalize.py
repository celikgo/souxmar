#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# scripts/synth-load/golden/normalize.py — output normaliser.
#
# Sprint 13 push 1 (ADR-0017). Strips non-deterministic noise from
# souxmar / souxmar-eval output before fingerprinting, so the golden
# corpus tracks behaviour, not wall-clock / addresses / paths.
#
# Reads <file> argv[1] (or stdin if "-"); writes normalised text to
# stdout. Idempotent — running it twice produces the same output.
#
# The normalisations below are *named*. Adding a new pattern is a
# Tier-0 change (golden format is not a stable contract; see ADR-0017
# § "What this ADR does NOT do"). The cost of adding a normalisation
# is regenerating every golden — which the harness's --refresh-golden
# flag automates.

from __future__ import annotations

import re
import sys
import pathlib


# Order matters: longer/specific patterns first to avoid masking.
NORMALISERS: list[tuple[re.Pattern[str], str]] = [
    # ISO-8601 timestamps with optional fractional seconds + Z|±offset.
    (re.compile(r"\b\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})\b"),
     "<TIMESTAMP>"),

    # RFC-2822-ish dates ("Mon, 11 May 2026 12:34:56 GMT").
    (re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun), \d{1,2} (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) \d{4} \d{2}:\d{2}:\d{2} GMT\b"),
     "<RFC2822>"),

    # Hex pointer addresses (0x… of 8+ hex chars).
    (re.compile(r"\b0x[0-9a-fA-F]{8,}\b"), "<PTR>"),

    # Absolute paths under common build/temp roots that vary per run.
    (re.compile(r"/tmp/[A-Za-z0-9._-]+/?"), "<TMP>/"),
    (re.compile(r"/var/folders/[A-Za-z0-9._/+-]+"), "<MACOS_TMP>"),
    (re.compile(r"C:\\\\Users\\\\[^\\\\]+\\\\AppData\\\\Local\\\\Temp\\\\[^ \r\n]+"), "<WIN_TMP>"),

    # Repo-relative absolute paths — anything containing /CLionProjects/souxmar/
    # or the GHA runner equivalent.
    (re.compile(r"/[A-Za-z0-9._/-]*/CLionProjects/souxmar/"), "<REPO>/"),
    (re.compile(r"/home/runner/work/souxmar/souxmar/"), "<REPO>/"),

    # Elapsed-time fragments emitted by souxmar-eval / souxmar run.
    (re.compile(r"\(elapsed: \d+(?:\.\d+)? ?ms\)"), "(elapsed: <T>ms)"),
    (re.compile(r"\bin \d+(?:\.\d+)? ?(?:ms|µs|us|s)\b"), "in <T>"),

    # Build-id / git-sha fragments ("a1b2c3d-dirty" 7-12 chars).
    (re.compile(r"\bbuild [0-9a-f]{7,12}(?:-dirty)?\b"), "build <SHA>"),
    (re.compile(r"\bgit-sha: [0-9a-f]{7,40}\b"), "git-sha: <SHA>"),

    # Plugin-discovery order varies by readdir() order. Sort lines
    # within a [plugins-loaded] / [discovered] block; that's done
    # separately below in line-block normalisation.

    # Windows line endings — strip CR.
    (re.compile(r"\r\n"), "\n"),
]


# Line-block normalisers — runs after regex pass. Each entry:
#   (start_marker_regex, end_marker_regex, line_sort: bool)
# Sorts the inner lines lexicographically when line_sort is True.
BLOCK_NORMALISERS: list[tuple[re.Pattern[str], re.Pattern[str], bool]] = [
    (re.compile(r"^\[plugins-loaded\]\s*$"),  re.compile(r"^$"), True),
    (re.compile(r"^\[plugins-discovered\]\s*$"), re.compile(r"^$"), True),
    (re.compile(r"^\[tools-registered\]\s*$"), re.compile(r"^$"), True),
]


def normalise(text: str) -> str:
    for pat, repl in NORMALISERS:
        text = pat.sub(repl, text)

    lines = text.splitlines()
    out: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        handled = False
        for start_re, end_re, do_sort in BLOCK_NORMALISERS:
            if start_re.match(line):
                out.append(line)
                i += 1
                block_start = i
                while i < len(lines) and not end_re.match(lines[i]):
                    i += 1
                block = lines[block_start:i]
                if do_sort:
                    block = sorted(block)
                out.extend(block)
                if i < len(lines):
                    out.append(lines[i])
                    i += 1
                handled = True
                break
        if not handled:
            out.append(line)
            i += 1

    # Trailing newline normalisation: exactly one terminating newline.
    normalised = "\n".join(out).rstrip("\n") + "\n"
    return normalised


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: normalize.py <file|->", file=sys.stderr)
        return 2
    src = argv[1]
    if src == "-":
        text = sys.stdin.read()
    else:
        text = pathlib.Path(src).read_text(encoding="utf-8", errors="replace")
    sys.stdout.write(normalise(text))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
