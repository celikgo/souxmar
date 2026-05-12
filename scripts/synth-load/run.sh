#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# scripts/synth-load/run.sh — public-alpha regression harness.
#
# Implements ADR-0017's "synthetic-load-augmented external" arm.
# Runs every example pipeline + every eval task, diffs against the
# committed golden corpus in golden/corpus.toml.
#
# Sprint 13 push 1 ships the driver + a starter corpus
# (cantilever-beam + 3 smoke evals). The corpus grows over time;
# every Sprint 13+ bug report whose root cause is engine-side
# becomes a synthetic-load golden, ratcheting the regression net.

set -u  # strict on undef vars; we handle errors explicitly

# ---------------------------------------------------------------------
# Layout assumptions
# ---------------------------------------------------------------------
#
#   $REPO_ROOT/scripts/synth-load/run.sh         (this script)
#   $REPO_ROOT/scripts/synth-load/golden/        (corpus)
#   $REPO_ROOT/scripts/synth-load/golden/normalize.py
#   $REPO_ROOT/build/dev/tools/souxmar/souxmar   (engine binary)
#   $REPO_ROOT/build/dev/tools/eval/souxmar-eval (eval binary)
#
# Resolve via the script's own location so it works from anywhere.
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"
GOLDEN_DIR="$SCRIPT_DIR/golden"
NORMALIZE="$GOLDEN_DIR/normalize.py"
CORPUS_FILE="$GOLDEN_DIR/corpus.toml"

# Aligned with tools/eval/main.cpp:
EXIT_OK=0
EXIT_EXAMPLES_DIVERGED=1
EXIT_EVALS_DIVERGED=2
EXIT_BOTH_DIVERGED=3
EXIT_MISCONFIG=4
EXIT_REFRESH_DIRTY=5

# ---------------------------------------------------------------------
# Arg parsing
# ---------------------------------------------------------------------

SKIP_EXAMPLES=0
SKIP_EVALS=0
REFRESH_GOLDEN=0
JSON_OUT=""
ENGINE_BIN=""
EVAL_BIN=""
VERBOSE=0

usage() {
  cat <<'EOF'
Usage: scripts/synth-load/run.sh [options]

Options:
  --skip-examples         Don't run example pipelines.
  --skip-evals            Don't run eval tasks.
  --refresh-golden        Recompute and overwrite golden corpus.
                          Refuses if corpus is dirty in git.
  --json-out <path>       Write a structured JSON report.
  --engine <path>         Override path to the souxmar CLI binary.
  --eval <path>           Override path to the souxmar-eval binary.
  --verbose               Print per-target stdout/stderr on diff.
  -h | --help             This message.

Exit codes:
  0  all targets matched golden
  1  example divergence
  2  eval divergence
  3  both diverged
  4  harness misconfigured
  5  --refresh-golden requested but corpus is dirty in git

See ADR-0017 and scripts/synth-load/README.md for the rationale.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --skip-examples)  SKIP_EXAMPLES=1; shift ;;
    --skip-evals)     SKIP_EVALS=1; shift ;;
    --refresh-golden) REFRESH_GOLDEN=1; shift ;;
    --json-out)       JSON_OUT="$2"; shift 2 ;;
    --engine)         ENGINE_BIN="$2"; shift 2 ;;
    --eval)           EVAL_BIN="$2"; shift 2 ;;
    --verbose)        VERBOSE=1; shift ;;
    -h|--help)        usage; exit 0 ;;
    *)
      echo "synth-load: unknown arg: $1" >&2
      usage >&2
      exit "$EXIT_MISCONFIG"
      ;;
  esac
done

# ---------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------

# Find engine + eval binaries if not overridden.
if [ -z "$ENGINE_BIN" ]; then
  ENGINE_BIN="$REPO_ROOT/build/dev/tools/souxmar/souxmar"
fi
if [ -z "$EVAL_BIN" ]; then
  EVAL_BIN="$REPO_ROOT/build/dev/tools/eval/souxmar-eval"
fi

# Sanity-check.
need_exit_misconfig=0
if [ "$SKIP_EXAMPLES" -eq 0 ] && [ ! -x "$ENGINE_BIN" ]; then
  echo "synth-load: engine binary not found or not executable: $ENGINE_BIN" >&2
  echo "synth-load:   build first with: cmake --build build/dev --target souxmar" >&2
  need_exit_misconfig=1
fi
if [ "$SKIP_EVALS" -eq 0 ] && [ ! -x "$EVAL_BIN" ]; then
  echo "synth-load: eval binary not found or not executable: $EVAL_BIN" >&2
  echo "synth-load:   build first with: cmake --build build/dev --target souxmar-eval" >&2
  need_exit_misconfig=1
fi
if [ ! -x "$NORMALIZE" ]; then
  echo "synth-load: normalizer not found or not executable: $NORMALIZE" >&2
  need_exit_misconfig=1
fi
if [ ! -f "$CORPUS_FILE" ]; then
  echo "synth-load: corpus file missing: $CORPUS_FILE" >&2
  need_exit_misconfig=1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "synth-load: python3 not on PATH (needed by normalizer)" >&2
  need_exit_misconfig=1
fi
if [ "$need_exit_misconfig" -eq 1 ]; then
  exit "$EXIT_MISCONFIG"
fi

# Refresh-golden gate: never overwrite a dirty corpus.
if [ "$REFRESH_GOLDEN" -eq 1 ]; then
  if git -C "$REPO_ROOT" diff --quiet -- "$CORPUS_FILE" 2>/dev/null \
     && git -C "$REPO_ROOT" diff --cached --quiet -- "$CORPUS_FILE" 2>/dev/null; then
    : # clean
  else
    echo "synth-load: --refresh-golden refuses because $CORPUS_FILE is dirty" >&2
    echo "synth-load:   commit or stash the corpus changes first" >&2
    exit "$EXIT_REFRESH_DIRTY"
  fi
fi

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

# fingerprint <text-file> -> stdout sha256 hex of normalised text.
fingerprint() {
  python3 "$NORMALIZE" "$1" | shasum -a 256 | awk '{print $1}'
}

# corpus_lookup <kind> <id> -> stdout the golden fingerprint, or empty.
corpus_lookup() {
  local kind="$1"
  local id="$2"
  # corpus.toml has stanzas like:
  #   [[golden]]
  #   kind = "example"
  #   id   = "cantilever-beam"
  #   sha256 = "..."
  awk -v kind="$kind" -v id="$id" '
    BEGIN { in_g = 0; cur_kind=""; cur_id=""; cur_sha=""; }
    /^\[\[golden\]\]/ { in_g=1; cur_kind=""; cur_id=""; cur_sha=""; next }
    in_g && /^kind/    { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_kind=a[2] }
    in_g && /^id/      { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_id=a[2] }
    in_g && /^sha256/  { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_sha=a[2] }
    in_g && /^$/ {
      if (cur_kind == kind && cur_id == id) { print cur_sha; exit 0 }
      in_g=0
    }
    END {
      if (in_g && cur_kind == kind && cur_id == id) print cur_sha
    }
  ' "$CORPUS_FILE"
}

# ---------------------------------------------------------------------
# Run examples
# ---------------------------------------------------------------------

declare -a EXAMPLE_RESULTS=()
examples_diverged=0
examples_ran=0
examples_matched=0

if [ "$SKIP_EXAMPLES" -eq 0 ]; then
  # Sprint 13 starter set: the cantilever beam (the canonical demo).
  # Sprint 14+ extends to mesh-comparison + thermal-fin once each
  # gets a stable golden; pipe-bend + swap-mesher are blocked on
  # CFD-stub determinism work.
  starter_examples=( "cantilever-beam" )

  workdir="$(mktemp -d -t souxmar-synth-load.XXXXXX)"
  trap 'rm -rf "$workdir"' EXIT

  for ex in "${starter_examples[@]}"; do
    examples_ran=$((examples_ran + 1))
    pipeline="$REPO_ROOT/examples/$ex/pipeline.yaml"
    if [ ! -f "$pipeline" ]; then
      echo "synth-load: example pipeline missing: $pipeline" >&2
      EXAMPLE_RESULTS+=( "$ex|missing-pipeline||" )
      examples_diverged=$((examples_diverged + 1))
      continue
    fi

    out="$workdir/$ex.out"
    err="$workdir/$ex.err"
    pushd "$workdir" >/dev/null
    "$ENGINE_BIN" run "$pipeline" \
      --plugin-path "$REPO_ROOT/build/dev/examples/plugins/hello-mesher" \
      --plugin-path "$REPO_ROOT/build/dev/examples/plugins/vtu-writer" \
      >"$out" 2>"$err"
    rc=$?
    popd >/dev/null

    if [ $rc -ne 0 ]; then
      EXAMPLE_RESULTS+=( "$ex|engine-exit-$rc||" )
      examples_diverged=$((examples_diverged + 1))
      if [ "$VERBOSE" -eq 1 ]; then
        echo "--- example $ex: engine rc=$rc ---" >&2
        cat "$err" >&2
      fi
      continue
    fi

    fp="$(fingerprint "$out")"
    golden="$(corpus_lookup example "$ex")"

    if [ "$REFRESH_GOLDEN" -eq 1 ]; then
      EXAMPLE_RESULTS+=( "$ex|refresh|$fp|$golden" )
      examples_matched=$((examples_matched + 1))
      continue
    fi

    if [ -z "$golden" ]; then
      EXAMPLE_RESULTS+=( "$ex|no-golden|$fp|" )
      examples_diverged=$((examples_diverged + 1))
    elif [ "$fp" = "$golden" ]; then
      EXAMPLE_RESULTS+=( "$ex|match|$fp|$golden" )
      examples_matched=$((examples_matched + 1))
    else
      EXAMPLE_RESULTS+=( "$ex|diverged|$fp|$golden" )
      examples_diverged=$((examples_diverged + 1))
      if [ "$VERBOSE" -eq 1 ]; then
        echo "--- example $ex: fingerprint mismatch ---" >&2
        echo "  expected: $golden" >&2
        echo "  got:      $fp" >&2
      fi
    fi
  done
fi

# ---------------------------------------------------------------------
# Run evals
# ---------------------------------------------------------------------

declare -a EVAL_RESULTS=()
evals_diverged=0
evals_ran=0
evals_matched=0

if [ "$SKIP_EVALS" -eq 0 ]; then
  # Sprint 13 starter set: three smoke evals. The full evals/v1/
  # set runs nightly via the eval-nightly workflow with its own
  # pass-rate gate (--min-pass-rate 0.90); the synth-load harness
  # exists for the *deterministic-output* subset.
  starter_evals=(
    "listing-01-list-plugins"
    "mesh-01-hello-mesher"
    "export-01-vtu"
  )

  for ev in "${starter_evals[@]}"; do
    evals_ran=$((evals_ran + 1))
    task_file="$REPO_ROOT/evals/v1/$ev.yaml"
    if [ ! -f "$task_file" ]; then
      echo "synth-load: eval task missing: $task_file" >&2
      EVAL_RESULTS+=( "$ev|missing-task||" )
      evals_diverged=$((evals_diverged + 1))
      continue
    fi

    out="$(mktemp -t souxmar-synth-eval.XXXXXX)"
    "$EVAL_BIN" "$task_file" >"$out" 2>&1
    rc=$?

    if [ $rc -ne 0 ]; then
      EVAL_RESULTS+=( "$ev|eval-exit-$rc||" )
      evals_diverged=$((evals_diverged + 1))
      [ "$VERBOSE" -eq 1 ] && cat "$out" >&2
      rm -f "$out"
      continue
    fi

    fp="$(fingerprint "$out")"
    rm -f "$out"
    golden="$(corpus_lookup eval "$ev")"

    if [ "$REFRESH_GOLDEN" -eq 1 ]; then
      EVAL_RESULTS+=( "$ev|refresh|$fp|$golden" )
      evals_matched=$((evals_matched + 1))
      continue
    fi

    if [ -z "$golden" ]; then
      EVAL_RESULTS+=( "$ev|no-golden|$fp|" )
      evals_diverged=$((evals_diverged + 1))
    elif [ "$fp" = "$golden" ]; then
      EVAL_RESULTS+=( "$ev|match|$fp|$golden" )
      evals_matched=$((evals_matched + 1))
    else
      EVAL_RESULTS+=( "$ev|diverged|$fp|$golden" )
      evals_diverged=$((evals_diverged + 1))
      if [ "$VERBOSE" -eq 1 ]; then
        echo "--- eval $ev: fingerprint mismatch ---" >&2
        echo "  expected: $golden" >&2
        echo "  got:      $fp" >&2
      fi
    fi
  done
fi

# ---------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------

emit_json_report() {
  local out_path="$1"
  {
    printf '{\n'
    printf '  "schema": 1,\n'
    printf '  "harness": "scripts/synth-load/run.sh",\n'
    printf '  "started_at": "%s",\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '  "refresh_golden": %s,\n' "$([ "$REFRESH_GOLDEN" -eq 1 ] && echo true || echo false)"
    printf '  "examples": {\n'
    printf '    "ran": %d,\n' "$examples_ran"
    printf '    "matched": %d,\n' "$examples_matched"
    printf '    "diverged": %d,\n' "$examples_diverged"
    printf '    "results": [\n'
    local n="${#EXAMPLE_RESULTS[@]}"
    local i=0
    for line in "${EXAMPLE_RESULTS[@]}"; do
      IFS='|' read -r id status fp golden <<< "$line"
      i=$((i + 1))
      local comma=","
      [ "$i" -eq "$n" ] && comma=""
      printf '      { "id": "%s", "status": "%s", "fingerprint": "%s", "golden": "%s" }%s\n' \
        "$id" "$status" "$fp" "$golden" "$comma"
    done
    printf '    ]\n'
    printf '  },\n'
    printf '  "evals": {\n'
    printf '    "ran": %d,\n' "$evals_ran"
    printf '    "matched": %d,\n' "$evals_matched"
    printf '    "diverged": %d,\n' "$evals_diverged"
    printf '    "results": [\n'
    local m="${#EVAL_RESULTS[@]}"
    local j=0
    for line in "${EVAL_RESULTS[@]}"; do
      IFS='|' read -r id status fp golden <<< "$line"
      j=$((j + 1))
      local comma=","
      [ "$j" -eq "$m" ] && comma=""
      printf '      { "id": "%s", "status": "%s", "fingerprint": "%s", "golden": "%s" }%s\n' \
        "$id" "$status" "$fp" "$golden" "$comma"
    done
    printf '    ]\n'
    printf '  }\n'
    printf '}\n'
  } > "$out_path"
}

if [ -n "$JSON_OUT" ]; then
  json_dir="$(dirname "$JSON_OUT")"
  [ -d "$json_dir" ] || mkdir -p "$json_dir"
  emit_json_report "$JSON_OUT"
fi

# Human-readable summary.
echo "synth-load: examples ran=$examples_ran matched=$examples_matched diverged=$examples_diverged"
echo "synth-load: evals    ran=$evals_ran    matched=$evals_matched    diverged=$evals_diverged"
for line in "${EXAMPLE_RESULTS[@]}"; do
  IFS='|' read -r id status fp golden <<< "$line"
  printf '  example  %-30s %s\n' "$id" "$status"
done
for line in "${EVAL_RESULTS[@]}"; do
  IFS='|' read -r id status fp golden <<< "$line"
  printf '  eval     %-30s %s\n' "$id" "$status"
done

# Refresh-golden writes back to corpus.toml and exits OK regardless
# (the diff *is* the intended outcome). The user reviews the
# corpus.toml diff and decides whether to commit.
if [ "$REFRESH_GOLDEN" -eq 1 ]; then
  refresh_corpus() {
    local tmp="${CORPUS_FILE}.tmp.$$"
    {
      printf '# scripts/synth-load/golden/corpus.toml\n'
      printf '#\n'
      printf '# Golden fingerprints for the synthetic-load harness (ADR-0017).\n'
      printf '# Each [[golden]] stanza is one target; sha256 is the\n'
      printf '# normaliser-output hash. Regenerated by run.sh --refresh-golden.\n'
      printf '#\n'
      printf '# Last regenerated: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      printf '\n'
      for line in "${EXAMPLE_RESULTS[@]}"; do
        IFS='|' read -r id status fp golden <<< "$line"
        [ -z "$fp" ] && continue
        printf '[[golden]]\n'
        printf 'kind   = "example"\n'
        printf 'id     = "%s"\n' "$id"
        printf 'sha256 = "%s"\n' "$fp"
        printf '\n'
      done
      for line in "${EVAL_RESULTS[@]}"; do
        IFS='|' read -r id status fp golden <<< "$line"
        [ -z "$fp" ] && continue
        printf '[[golden]]\n'
        printf 'kind   = "eval"\n'
        printf 'id     = "%s"\n' "$id"
        printf 'sha256 = "%s"\n' "$fp"
        printf '\n'
      done
    } > "$tmp"
    mv "$tmp" "$CORPUS_FILE"
  }
  refresh_corpus
  echo "synth-load: refreshed $CORPUS_FILE — review the diff before committing."
  exit "$EXIT_OK"
fi

# Exit code per the matrix in README.md.
if [ "$examples_diverged" -gt 0 ] && [ "$evals_diverged" -gt 0 ]; then
  exit "$EXIT_BOTH_DIVERGED"
elif [ "$examples_diverged" -gt 0 ]; then
  exit "$EXIT_EXAMPLES_DIVERGED"
elif [ "$evals_diverged" -gt 0 ]; then
  exit "$EXIT_EVALS_DIVERGED"
fi
exit "$EXIT_OK"
