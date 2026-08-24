#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check-frozen-headers.sh — block PRs touching the frozen v1 ABI surface
# unless the commit messages carry a recognised ratchet token.
#
# Sprint 7 push 1 (ADR-0008) declared every header under
# include/souxmar-c/ frozen for the entire 1.x release series. This
# script is the CI gate that enforces it.
#
# Pass conditions:
#   - No changes touch any frozen header. (Allowed: changes anywhere else.)
#   - All changes are documentation-only (no .h modifications).
#   - At least one commit message in the PR range carries one of the
#     ratchet markers:
#       * "Ratchet: additive minor surface (ADR-0008)"
#       * "Ratchet: bug-fix (ADR-0008) — <reason>"
#     The additive marker requires the change to be strictly additive
#     (new declarations / new headers / new SOUXMAR_X_* macros under a
#     fresh prefix). The bug-fix marker carries a justification.
#
# Fail conditions:
#   - Any frozen header changed without one of the ratchet markers.
#   - The marker is present but the change is non-additive AND not
#     declared as bug-fix.
#
# Usage:
#   scripts/check-frozen-headers.sh [<base-ref>] [<head-ref>]
#
# Defaults to comparing HEAD against the remote's default branch,
# resolved from origin/HEAD (falling back to origin/master). CI
# overrides both refs from the workflow.

set -euo pipefail

default_base_ref() {
  git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null \
    || echo "origin/master"
}

BASE_REF="${1:-$(default_base_ref)}"
HEAD_REF="${2:-HEAD}"

# Fail closed. An unresolvable base ref used to make the `git diff`
# below print nothing, which read as "no v1 ABI surface touched" and
# exited 0 — a gate that cannot find its baseline must not pass.
for ref in "$BASE_REF" "$HEAD_REF"; do
  if ! git rev-parse --verify --quiet "${ref}^{commit}" >/dev/null; then
    echo "frozen-headers: ref '${ref}' does not resolve in this repository." >&2
    echo "  Pass an explicit base: scripts/check-frozen-headers.sh <base-ref> [head-ref]" >&2
    exit 1
  fi
done

# The inventory under freeze, derived from the tree rather than restated
# here. The hand-maintained list this replaces said "Keep in sync with
# ADR-0008's table" and listed 15 headers while include/souxmar-c/ had
# grown to 20: brep.h, field_stream.h, sketch.h, surface_stream.h and
# timeseries.h were all editable with no ratchet marker. A gate that
# prints "no v1 ABI surface touched" while a quarter of that surface is
# outside its own list is worse than no gate, because it positively
# asserts the safety it is not checking. ADR-0008 freezes the directory,
# so the directory is the source of truth.
#
# Both refs are consulted, not just the worktree: a PR whose only change
# is `git rm include/souxmar-c/sketch.h` leaves `git ls-files` nothing to
# find, and removing a frozen header is the most breaking change the ABI
# admits. `ls-tree` takes no `glob` pathspec magic, so the base side is
# scoped to the directory and filtered to `.h` here.
#
# Every pathspec carries `:(top)`, which anchors it to the repository
# root. Without it the plain relative paths matched nothing whenever the
# script ran from anywhere but the checkout root — from scripts/, the
# diff came back empty and the gate exited 0 on a PR that rewrote abi.h.
FROZEN_HEADERS=()
while IFS= read -r header; do
  [ -n "$header" ] && FROZEN_HEADERS+=(":(top)$header")
done < <(
  {
    git ls-files --full-name -- ':(top,glob)include/souxmar-c/*.h'
    git ls-tree -r --full-name --name-only "$BASE_REF" \
      -- ':(top)include/souxmar-c/' | grep -E '\.h$'
  } | LC_ALL=C sort -u
)

# Fail closed for the same reason the ref check above does: an empty
# inventory would make the diff below match nothing and report "no v1 ABI
# surface touched", which is the exact false green this gate exists to
# prevent.
if [ "${#FROZEN_HEADERS[@]}" -eq 0 ]; then
  echo "frozen-headers: found no headers under include/souxmar-c/." >&2
  echo "  Either the checkout is not a souxmar tree or the ABI surface was deleted wholesale." >&2
  exit 1
fi
echo "frozen-headers: ${#FROZEN_HEADERS[@]} headers under ADR-0008 freeze."

ADDITIVE_MARKER="Ratchet: additive minor surface (ADR-0008)"
BUGFIX_MARKER_PREFIX="Ratchet: bug-fix (ADR-0008)"

# Build the diff path list. `mapfile` would be more idiomatic but
# requires bash 4+, and macOS still ships 3.2 in /bin/bash; the
# while-read pattern below works on every bash + dash + zsh the CI
# matrix uses.
CHANGED=""
while IFS= read -r line; do
  [ -n "$line" ] && CHANGED="$CHANGED $line"
done < <(git diff --name-only "$BASE_REF" "$HEAD_REF" -- "${FROZEN_HEADERS[@]}")

if [ -z "$CHANGED" ]; then
  echo "frozen-headers: no v1 ABI surface touched. OK."
  exit 0
fi

echo "frozen-headers: PR touches the v1 ABI surface:"
for h in $CHANGED; do echo "  - $h"; done

# Inspect commit messages on the range for the ratchet token.
COMMIT_MSGS=$(git log --format=%B "$BASE_REF..$HEAD_REF")
HAS_ADDITIVE=false
HAS_BUGFIX=false
if grep -qF "$ADDITIVE_MARKER" <<<"$COMMIT_MSGS"; then
  HAS_ADDITIVE=true
fi
if grep -qF "$BUGFIX_MARKER_PREFIX" <<<"$COMMIT_MSGS"; then
  HAS_BUGFIX=true
fi

if [ "$HAS_ADDITIVE" = true ] || [ "$HAS_BUGFIX" = true ]; then
  echo "frozen-headers: ratchet marker present — allowed."
  if [ "$HAS_ADDITIVE" = true ]; then
    echo "  marker: $ADDITIVE_MARKER"
  fi
  if [ "$HAS_BUGFIX" = true ]; then
    bugfix_line=$(grep -F "$BUGFIX_MARKER_PREFIX" <<<"$COMMIT_MSGS" | head -n1)
    echo "  marker: $bugfix_line"
  fi
  exit 0
fi

cat <<EOF >&2
frozen-headers: ABI v1 lockdown gate REJECTED this PR.

The headers above are pinned for the entire 1.x release series. To
land an additive minor surface, add this exact marker to the relevant
commit message:

    Ratchet: additive minor surface (ADR-0008)

For a non-load-bearing fix (comment, doc, declaration restoration),
use:

    Ratchet: bug-fix (ADR-0008) — <reason>

Anything else — renaming a function, appending a struct field, changing
a SOUXMAR_X_* numeric value — requires a Tier-3 ADR per
docs/GOVERNANCE.md and is in practice a v2 ABI conversation.

See docs/adr/0008-abi-v1-final-freeze.md for the full rules.
EOF
exit 1
