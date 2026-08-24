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
#   $REPO_ROOT/build/dev/src/cli/souxmar         (engine binary)
#   $REPO_ROOT/build/dev/tools/eval/souxmar-eval (eval binary)
#   $REPO_ROOT/build/dev/examples/plugins/       (plugin search root)
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
BOOTSTRAP=0
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
  --bootstrap             Record fingerprints only for corpus
                          entries currently marked empty. Treats
                          non-empty entries as gates. Sprint 14
                          push 1 initialisation flow — runs once
                          on the first green CI after v0.9.0 to
                          seed the corpus, then the flag is not
                          used again until a new target is added.
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
    --bootstrap)      BOOTSTRAP=1; shift ;;
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

# Find engine + eval binaries if not overridden. The CLI target lives under
# src/cli/, not tools/souxmar/ — the latter path has never existed in any
# preset, so an unqualified run of this harness always died at the sanity
# check below with "engine binary not found".
if [ -z "$ENGINE_BIN" ]; then
  ENGINE_BIN="$REPO_ROOT/build/dev/src/cli/souxmar"
fi
if [ -z "$EVAL_BIN" ]; then
  EVAL_BIN="$REPO_ROOT/build/dev/tools/eval/souxmar-eval"
fi

# The example loop pushd's into a mktemp workdir before invoking the engine,
# so a relative --engine/--eval (which is what nightly.yml passes) stops
# resolving the moment we change directory and the run dies with rc=127.
# Canonicalise once, here, while $PWD is still the caller's directory.
case "$ENGINE_BIN" in /*) ;; *) ENGINE_BIN="$PWD/$ENGINE_BIN" ;; esac
case "$EVAL_BIN"   in /*) ;; *) EVAL_BIN="$PWD/$EVAL_BIN"     ;; esac

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

# A --plugin-path entry is a search *root*: discover_plugins() scans its
# immediate subdirectories for souxmar-plugin.toml
# (include/souxmar/plugin/discovery.h:83-85). Handing it the leaf
# .../examples/plugins/hello-mesher therefore discovers nothing — it looks
# one level too deep — which is why every example run reported "no plugins
# found". Pass the parent, and derive it from $ENGINE_BIN so the harness
# follows whichever preset built the binary instead of pinning build/dev.
PLUGIN_DIR="$( cd "$( dirname "$ENGINE_BIN" )/../.." && pwd )/examples/plugins"

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
    BEGIN { in_g = 0; found = 0; cur_kind=""; cur_id=""; cur_sha=""; }
    /^\[\[golden\]\]/ { in_g=1; cur_kind=""; cur_id=""; cur_sha=""; next }
    in_g && /^kind/    { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_kind=a[2] }
    in_g && /^id/      { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_id=a[2] }
    in_g && /^sha256/  { gsub(/[" =]+/, " ", $0); split($0, a, " "); cur_sha=a[2] }
    in_g && /^$/ {
      # awk runs the END rule even on exit, and in_g is still 1 with the
      # matching cur_* fields loaded, so without this sentinel every hit
      # was printed twice and $golden became "<sha> <sha>". Harmless only
      # while every corpus hash is empty; the moment the corpus is seeded
      # it makes every comparison in the caller fail as "diverged".
      if (cur_kind == kind && cur_id == id) { print cur_sha; found=1; exit 0 }
      in_g=0
    }
    END {
      if (!found && in_g && cur_kind == kind && cur_id == id) print cur_sha
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
    # `|| exit` on both pushd and popd (SC2164): a failed cd here would
    # otherwise run the engine in the previous directory and quietly
    # fingerprint the wrong thing.
    pushd "$workdir" >/dev/null || exit 1
    # --no-cache: with a warm per-user disk cache (~/Library/Caches/souxmar,
    # $XDG_CACHE_HOME/souxmar) `run` prints "[CACHED  ]" where a cold machine
    # prints "[OK      ]" (src/cli/main.cpp:458), so the fingerprint would
    # encode whether this runner had executed the pipeline before rather than
    # what the pipeline does. The harness must measure behaviour, not machine
    # history.
    "$ENGINE_BIN" run "$pipeline" \
      --plugin-path "$PLUGIN_DIR" \
      --no-cache \
      >"$out" 2>"$err"
    rc=$?
    popd >/dev/null || exit 1

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
      # Sprint 14 push 1 — in --bootstrap mode, record the
      # fingerprint instead of treating absence as divergence.
      if [ "$BOOTSTRAP" -eq 1 ]; then
        EXAMPLE_RESULTS+=( "$ex|bootstrap|$fp|" )
        examples_matched=$((examples_matched + 1))
      else
        EXAMPLE_RESULTS+=( "$ex|no-golden|$fp|" )
        examples_diverged=$((examples_diverged + 1))
      fi
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
    # souxmar-eval takes the evals *directory* as its positional and rejects
    # a single file outright with kExitUsage=2 (tools/eval/main.cpp:563); the
    # one-task selector is --only, matched against the task YAML's `id:`
    # field (tools/eval/main.cpp:620). Passing "$task_file" here is what made
    # all three evals report eval-exit-2 every night.
    "$EVAL_BIN" "$REPO_ROOT/evals/v1" \
      --only "$ev" \
      --plugin-path "$PLUGIN_DIR" \
      >"$out" 2>&1
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
      if [ "$BOOTSTRAP" -eq 1 ]; then
        EVAL_RESULTS+=( "$ev|bootstrap|$fp|" )
        evals_matched=$((evals_matched + 1))
      else
        EVAL_RESULTS+=( "$ev|no-golden|$fp|" )
        evals_diverged=$((evals_diverged + 1))
      fi
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

# Bootstrap mode: populate empty-hash entries, preserve existing
# entries unchanged. Writes corpus.toml + exits OK so the
# maintainer can review the diff before committing. Unlike
# --refresh-golden, this does NOT overwrite non-empty entries —
# the bootstrap is one-shot per-target.
if [ "$BOOTSTRAP" -eq 1 ] && [ "$REFRESH_GOLDEN" -eq 0 ]; then
  bootstrap_corpus() {
    local tmp="${CORPUS_FILE}.tmp.$$"
    python3 - "$CORPUS_FILE" <<PYEOF > "$tmp"
import sys, re
src = open(sys.argv[1]).read()
def lookup(kind, ident):
PYEOF
    # The above python is intentionally truncated — we'll do the
    # corpus rewrite in pure bash for portability + auditability.
    # See the awk-based rewrite below.
    rm -f "$tmp"

    # Build a map of (kind, id) -> new_fp from results.
    declare -A NEW_HASHES
    for line in "${EXAMPLE_RESULTS[@]}"; do
      IFS='|' read -r id status fp golden <<< "$line"
      [ "$status" = "bootstrap" ] && NEW_HASHES["example|$id"]="$fp"
    done
    for line in "${EVAL_RESULTS[@]}"; do
      IFS='|' read -r id status fp golden <<< "$line"
      [ "$status" = "bootstrap" ] && NEW_HASHES["eval|$id"]="$fp"
    done

    # Walk corpus.toml stanza-by-stanza and overwrite only the
    # sha256 lines whose stanza's (kind, id) is in NEW_HASHES.
    {
      local stanza_kind="" stanza_id=""
      local in_stanza=0
      while IFS='' read -r line || [ -n "$line" ]; do
        case "$line" in
          '[[golden]]')
            in_stanza=1; stanza_kind=""; stanza_id=""
            printf '%s\n' "$line"
            continue
            ;;
        esac
        if [ "$in_stanza" -eq 1 ]; then
          case "$line" in
            'kind'*)
              stanza_kind=$(echo "$line" | sed -E 's/.*"([^"]*)".*/\1/')
              ;;
            'id'*)
              stanza_id=$(echo "$line" | sed -E 's/.*"([^"]*)".*/\1/')
              ;;
            'sha256'*)
              # If we have a new hash for this stanza AND the
              # existing line is empty (""), write the new one;
              # otherwise leave intact.
              local key="${stanza_kind}|${stanza_id}"
              local existing
              existing=$(echo "$line" | sed -E 's/.*"([^"]*)".*/\1/')
              if [ -z "$existing" ] && [ -n "${NEW_HASHES[$key]:-}" ]; then
                printf 'sha256 = "%s"\n' "${NEW_HASHES[$key]}"
                continue
              fi
              ;;
            '')
              # stanza terminator
              in_stanza=0
              stanza_kind=""; stanza_id=""
              ;;
          esac
        fi
        printf '%s\n' "$line"
      done < "$CORPUS_FILE"
    } > "${CORPUS_FILE}.tmp.$$"
    mv "${CORPUS_FILE}.tmp.$$" "$CORPUS_FILE"
  }
  bootstrap_corpus
  bootstrap_count=0
  for line in "${EXAMPLE_RESULTS[@]}" "${EVAL_RESULTS[@]}"; do
    IFS='|' read -r id status fp golden <<< "$line"
    [ "$status" = "bootstrap" ] && bootstrap_count=$((bootstrap_count + 1))
  done
  echo "synth-load: bootstrap wrote ${bootstrap_count} new golden hash(es) to $CORPUS_FILE"
  echo "synth-load: review the diff (git diff $CORPUS_FILE) before committing."
  exit "$EXIT_OK"
fi

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
