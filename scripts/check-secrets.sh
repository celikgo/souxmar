#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check-secrets.sh — reject secret-shaped strings in tracked source.
#
# ENGINEERING_PRACTICES.md § Security promises "a pre-commit hook + CI
# scan rejects anything matching `sk-`, `xoxb-`, AKIA[0-9A-Z]{16}, etc."
# This is that scan. It is deliberately pattern-based and local: it
# catches the accident (a key pasted into a test fixture) rather than a
# determined leak, and it runs in under a second so it can sit in a
# pre-commit hook.
#
# Usage:
#   scripts/check-secrets.sh [<path>...]        # default: tracked files
#
# Exit 0 when clean, 1 when a candidate is found.
#
# Suppressing a false positive: put the literal `pragma: allowlist-secret`
# on the same line. Suppressions are visible in review, which is the
# point — an invisible allowlist file is how these scans rot.

set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

# name|regex. Anchored where the vendor's format allows it, so a prose
# mention of "sk-" in documentation does not trip the scan.
PATTERNS=(
  "OpenAI-style key|sk-[A-Za-z0-9]{20,}"
  "Anthropic key|sk-ant-[A-Za-z0-9_-]{20,}"
  "xAI key|xai-[A-Za-z0-9]{20,}"
  "Slack token|xox[baprs]-[A-Za-z0-9-]{10,}"
  "AWS access key id|AKIA[0-9A-Z]{16}"
  "Google API key|AIza[0-9A-Za-z_-]{35}"
  "GitHub token|gh[pousr]_[A-Za-z0-9]{36,}"
  "Stripe secret key|sk_live_[0-9a-zA-Z]{24,}"
  "Private key block|-----BEGIN [A-Z ]*PRIVATE KEY-----"
  "Generic assignment|(api_?key|secret|passwd|password|token)[\"' ]*[:=][\"' ]*[A-Za-z0-9/+_-]{24,}"
)

# Build the filtered file list. `mapfile` would be tidier but needs
# bash 4+, and macOS still ships 3.2 — the same constraint
# check-frozen-headers.sh documents.
TMP_LIST="$(mktemp)"
trap 'rm -f "$TMP_LIST"' EXIT

list_candidates() {
  if [ "$#" -gt 0 ]; then
    printf '%s\n' "$@"
  else
    git ls-files
  fi
}

file_count=0
list_candidates "$@" | while IFS= read -r f; do
  [ -f "$f" ] || continue
  case "$f" in
    */.git/*|.git/*|build/*|*/build/*|node_modules/*|*/node_modules/*) continue ;;
    target/*|*/target/*|__pycache__/*|*/__pycache__/*) continue ;;
    *.lock|*.png|*.jpg|*.jpeg|*.gif|*.ico|*.pdf|*.woff|*.woff2) continue ;;
    package-lock.json|*/package-lock.json) continue ;;
    THIRD_PARTY_LICENSES.md) continue ;;
  esac
  printf '%s\n' "$f"
done > "$TMP_LIST"
file_count="$(wc -l < "$TMP_LIST" | tr -d ' ')"

# One grep pass per pattern over the whole list, rather than a grep per
# (pattern, file) pair — the latter is thousands of processes and turns
# a sub-second gate into a minute.
findings=0
for entry in "${PATTERNS[@]}"; do
  name="${entry%%|*}"
  regex="${entry#*|}"
  hits="$(tr '\n' '\0' < "$TMP_LIST" \
          | xargs -0 grep -IHnE "$regex" -- 2>/dev/null \
          | grep -v 'pragma: allowlist-secret' || true)"
  [ -n "$hits" ] || continue
  printf '%s\n' "$hits" | while IFS= read -r line; do
    echo "secret-scan: $name in $line" | cut -c1-200
  done
  findings=$((findings + $(printf '%s\n' "$hits" | wc -l | tr -d ' ')))
done

if [ "$findings" -gt 0 ]; then
  cat >&2 <<'MSG'

secret-scan: REJECTED — credential-shaped strings found above.

If one is a real credential:
  1. Rotate it now. Assume it is public the moment it is pushed.
  2. Remove it from the working tree, and from history if already pushed.
  3. Put the value in an environment variable instead. souxmar never
     reads a key out of a config file — see docs/AI_INTEGRATION.md.

If it is a false positive, append this to the line:
  pragma: allowlist-secret
MSG
  exit 1
fi

echo "secret-scan: $file_count files scanned, no credential-shaped strings. OK."
