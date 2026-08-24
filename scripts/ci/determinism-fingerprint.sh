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
# A pipeline that exits non-zero is recorded as `<target>  EXIT-<rc>`
# rather than aborting the run, on the grounds that failing identically
# everywhere is still deterministic. That holds only while most of the
# corpus is actually hashing, and nothing bounded the EXIT-* share: on
# 2026-08-20 (run 32355873858) five of the nine examples were EXIT-70 on
# all three platforms and the gate still announced "3 platforms agree on
# every pipeline" — 56% of the corpus contributing zero coverage while
# reading as green. So both counts are always reported, and --min-hashed
# turns the floor into a committed number CI enforces.
#
# Usage:
#   scripts/ci/determinism-fingerprint.sh --engine <path> [--out <file>]
#                                         [--plugin-path <dir>]...
#                                         [--min-hashed <n>] [--require-all]
#
# Exit status: 0 on success, 2 on a usage error, 1 when fewer than
# --min-hashed pipelines produced a hash or --require-all is set and any
# pipeline did not.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NORMALISE="$REPO_ROOT/scripts/synth-load/golden/normalize.py"

ENGINE=""
OUT="-"
PLUGIN_ARGS=""
MIN_HASHED=""
REQUIRE_ALL=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --engine)      ENGINE="$2"; shift 2 ;;
    --out)         OUT="$2"; shift 2 ;;
    --plugin-path) PLUGIN_ARGS="$PLUGIN_ARGS --plugin-path $2"; shift 2 ;;
    --require-all)
      # Stronger than --min-hashed and self-maintaining: every pipeline in
      # the corpus must hash. A floor cannot notice an *eleventh* example
      # landing broken — HASHED stays at the floor and the gate passes — which
      # is the same blind spot that let five broken examples sit in the corpus
      # for months. Once coverage is total, assert totality and there is no
      # number to keep updating.
      REQUIRE_ALL=1; shift ;;
    --min-hashed)
      MIN_HASHED="$2"; shift 2
      # Rejected here rather than at the comparison, where a typo'd
      # `--min-hashed four` would make `[ "$HASHED" -lt four ]` an error
      # the shell reports and then carries on past, leaving the floor
      # silently unenforced.
      case "$MIN_HASHED" in
        ''|*[!0-9]*)
          echo "determinism: --min-hashed wants a non-negative integer (got '$MIN_HASHED')" >&2
          exit 2 ;;
      esac
      ;;
    -h|--help)
      sed -n '2,36p' "$0"; exit 0 ;;
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

# Every diagnostic goes to stderr, including the "wrote N fingerprints"
# line that used to print on stdout. With `--out -` stdout *is* the
# manifest, and the `determinism` job in ci.yml diffs those manifests
# byte for byte — a summary line inside one would read as a platform
# difference and fail the gate for the gate's own output.
log() { echo "$@" >&2; }

MANIFEST="$WORK/manifest.txt"

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
} | LC_ALL=C sort > "$MANIFEST"

# Counted off the finished manifest rather than tallied inside the loop,
# so the numbers describe exactly the bytes the `determinism` job will
# diff. grep exits 1 when it matches nothing, which is a legitimate count
# of zero here, so the status is discarded.
HASHED="$(grep -cE '  [0-9a-f]{64}$' "$MANIFEST")" || true
FAILED="$(grep -cE '  EXIT-[0-9]+$' "$MANIFEST")" || true
TOTAL="$(wc -l < "$MANIFEST" | tr -d ' ')"

emit < "$MANIFEST"

log "determinism: $HASHED of $TOTAL pipelines hashed, $FAILED reported EXIT-*."
if [ "$OUT" != "-" ]; then
  log "determinism: wrote $TOTAL fingerprints to $OUT"
fi

if [ "$REQUIRE_ALL" -eq 1 ] && [ "$FAILED" -ne 0 ]; then
  log "determinism: $FAILED of $TOTAL pipelines did not produce a hash, and"
  log "--require-all says every one of them must."
  log ""
  log "An EXIT-* line compares equal across platforms for free, so it"
  log "contributes nothing to the cross-platform check — a pipeline that fails"
  log "identically everywhere is invisible to this gate unless it is counted."
  grep -E '  EXIT-[0-9]+$' "$MANIFEST" | sed 's/^/  /' >&2
  log ""
  log "Either an example regressed, or a newly added example has never run."
  log "Run it by hand from an empty directory — that is how the last five"
  log "broke: they resolved their input paths against the working directory."
  exit 1
fi

if [ -n "$MIN_HASHED" ] && [ "$HASHED" -lt "$MIN_HASHED" ]; then
  log "determinism: only $HASHED pipelines produced a hash; the floor is $MIN_HASHED."
  log ""
  log "This is a coverage failure, not a determinism failure. An EXIT-* line"
  log "compares equal across platforms for free, so it contributes nothing to"
  log "the cross-platform check. These pipelines produced no hash:"
  grep -E '  EXIT-[0-9]+$' "$MANIFEST" | sed 's/^/  /' >&2
  log ""
  log "Either an example that used to run has regressed on this platform, or"
  log "--min-hashed in .github/workflows/ci.yml was raised past what the"
  log "corpus can currently meet."
  exit 1
fi
