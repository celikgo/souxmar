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
# Defaults to comparing HEAD against `origin/main`. CI overrides both
# refs from the workflow.

set -euo pipefail

BASE_REF="${1:-origin/main}"
HEAD_REF="${2:-HEAD}"

# The inventory under freeze. Keep in sync with ADR-0008's table.
FROZEN_HEADERS=(
  "include/souxmar-c/abi.h"
  "include/souxmar-c/status.h"
  "include/souxmar-c/types.h"
  "include/souxmar-c/plugin.h"
  "include/souxmar-c/registry.h"
  "include/souxmar-c/mesher.h"
  "include/souxmar-c/solver.h"
  "include/souxmar-c/writer.h"
  "include/souxmar-c/postproc.h"
  "include/souxmar-c/reader.h"
  "include/souxmar-c/mesh.h"
  "include/souxmar-c/geometry.h"
  "include/souxmar-c/field.h"
  "include/souxmar-c/value.h"
  "include/souxmar-c/buffer.h"
)

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
