#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# determinism-fingerprint.sh — emit a per-platform fingerprint manifest.
#
# ENGINEERING_PRACTICES.md requires that the same pipeline produce
# byte-identical results on Linux, macOS and Windows. Nothing enforced
# it. This script is one half of that gate: each platform in the CI
# matrix runs it and uploads the manifest; a downstream job diffs the
# manifests against each other. A difference *is* the failure — there is
# no golden file to drift, because the platforms are each other's
# reference.
#
# Output is one `<target>  <sha256>` line per pipeline, sorted, on
# stdout. Engine output is passed through the synth-load normaliser
# first, so wall-clock, pointers and absolute paths do not read as
# non-determinism. Anything the normaliser does not strip is either a
# real behavioural difference or a normaliser gap — both worth failing.
#
# Usage:
#   scripts/ci/determinism-fingerprint.sh --engine <path> [--out <file>]
#                                         [--plugin-path <dir>]...

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NORMALISE="$REPO_ROOT/scripts/synth-load/golden/normalize.py"

ENGINE=""
OUT="-"
PLUGIN_ARGS=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --engine)      ENGINE="$2"; shift 2 ;;
    --out)         OUT="$2"; shift 2 ;;
    --plugin-path) PLUGIN_ARGS="$PLUGIN_ARGS --plugin-path $2"; shift 2 ;;
    -h|--help)
      sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "determinism: unknown flag '$1'" >&2; exit 2 ;;
  esac
done

if [ -z "$ENGINE" ] || [ ! -x "$ENGINE" ]; then
  echo "determinism: --engine must point at the souxmar binary (got '$ENGINE')" >&2
  exit 2
fi
# Absolute, because each pipeline runs from a scratch cwd — a relative
# path resolves to nothing there and every target reports EXIT-127.
ENGINE="$(cd "$(dirname "$ENGINE")" && pwd)/$(basename "$ENGINE")"
if [ ! -f "$NORMALISE" ]; then
  echo "determinism: normaliser missing at $NORMALISE" >&2
  exit 2
fi

# Windows runners ship git-bash, where the interpreter is `python`.
PYTHON="python3"
command -v python3 >/dev/null 2>&1 || PYTHON="python"

sha256_of() {
  # shasum is on macOS and Linux runners; sha256sum is Linux-only.
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum | cut -d' ' -f1
  else
    shasum -a 256 | cut -d' ' -f1
  fi
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

emit() { if [ "$OUT" = "-" ]; then cat; else cat > "$OUT"; fi; }

{
  # Deterministic iteration order: the shell's glob is locale-sensitive,
  # so sort explicitly rather than trusting it to match across platforms.
  for pipeline in $(find "$REPO_ROOT/examples" -name pipeline.yaml | LC_ALL=C sort); do
    name="$(basename "$(dirname "$pipeline")")"
    out="$WORK/$name.out"

    # Run from a scratch cwd so relative output paths cannot collide,
    # and with a fixed TZ + locale: both leak into formatted output and
    # would otherwise show up as a platform difference that is really a
    # runner-configuration difference.
    (
      cd "$WORK" || exit 1
      TZ=UTC LC_ALL=C "$ENGINE" run "$pipeline" $PLUGIN_ARGS
    ) >"$out" 2>"$WORK/$name.err"
    rc=$?

    if [ $rc -ne 0 ]; then
      # A pipeline that fails everywhere for the same reason is still
      # deterministic, so the exit code is part of the fingerprint
      # rather than an abort. A pipeline that fails on one platform only
      # is exactly what this gate exists to catch.
      echo "$name  EXIT-$rc"
      continue
    fi

    fp="$("$PYTHON" "$NORMALISE" "$out" | sha256_of)"
    echo "$name  $fp"
  done
} | LC_ALL=C sort | emit

if [ "$OUT" != "-" ]; then
  echo "determinism: wrote $(wc -l < "$OUT" | tr -d ' ') fingerprints to $OUT"
fi
