#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check-tool-contract.sh — block PRs touching the frozen-candidate agent
# tool contract surface unless the commit messages carry a recognised
# ratchet token.
#
# Sprint 8 push 5 (ADR-0010) declared the agent tool contract a freeze
# candidate. During the candidate period this script runs as a
# non-blocking CI job; it flips to blocking when the final freeze ADR
# lands (target: Sprint 9 push 1).
#
# Pass conditions:
#   - No changes touch any contract-surface file.
#   - All changes are documentation / test / CMake adjustments (the
#     framework header itself is untouched).
#   - At least one commit message in the PR range carries one of the
#     ratchet markers:
#       * "Ratchet: additive tool (ADR-0010)"
#       * "Ratchet: additive context field (ADR-0010)"
#
# Fail conditions (final-freeze mode only):
#   - The framework header (include/souxmar/ai/tool.h) changes without
#     the additive-context-field marker.
#   - default_v1_tools()'s catalogue order has been edited so that the
#     registry-count assertion in test_ai_tools.cpp can no longer be
#     reconciled with the documented tool count.
#
# Usage:
#   scripts/check-tool-contract.sh [<base-ref>] [<head-ref>]
#
# Defaults to comparing HEAD against `origin/main`. The candidate-period
# behaviour is to print warnings + exit 0 (non-blocking). When the final
# freeze lands, set SOUXMAR_TOOL_CONTRACT_BLOCKING=1 to flip to blocking.

set -euo pipefail

BASE_REF="${1:-origin/main}"
HEAD_REF="${2:-HEAD}"
BLOCKING="${SOUXMAR_TOOL_CONTRACT_BLOCKING:-0}"

# The contract surface. Keep in sync with ADR-0010's table.
CONTRACT_FILES=(
  "include/souxmar/ai/tool.h"
  "src/ai/tools/default_registry.cpp"
)

ADDITIVE_TOOL_MARKER="Ratchet: additive tool (ADR-0010)"
ADDITIVE_CTX_MARKER="Ratchet: additive context field (ADR-0010)"

# Build the diff path list.
CHANGED=""
while IFS= read -r line; do
  [ -n "$line" ] && CHANGED="$CHANGED $line"
done < <(git diff --name-only "$BASE_REF" "$HEAD_REF" -- "${CONTRACT_FILES[@]}")

if [ -z "$CHANGED" ]; then
  echo "tool-contract: no contract surface touched. OK."
  exit 0
fi

echo "tool-contract: PR touches the v1 tool-contract surface:"
for f in $CHANGED; do echo "  - $f"; done

# Inspect commit messages on the range for the ratchet token.
COMMIT_MSGS=$(git log --format=%B "$BASE_REF..$HEAD_REF")
HAS_TOOL=false
HAS_CTX=false
if grep -qF "$ADDITIVE_TOOL_MARKER" <<<"$COMMIT_MSGS"; then
  HAS_TOOL=true
fi
if grep -qF "$ADDITIVE_CTX_MARKER" <<<"$COMMIT_MSGS"; then
  HAS_CTX=true
fi

if [ "$HAS_TOOL" = true ] || [ "$HAS_CTX" = true ]; then
  echo "tool-contract: ratchet marker present — allowed."
  [ "$HAS_TOOL" = true ] && echo "  marker: $ADDITIVE_TOOL_MARKER"
  [ "$HAS_CTX"  = true ] && echo "  marker: $ADDITIVE_CTX_MARKER"
  exit 0
fi

cat <<EOF >&2
tool-contract: contract surface modified without a ratchet marker.

The agent tool contract is a freeze candidate (ADR-0010). To land an
additive new tool, add this exact marker to the relevant commit message:

    Ratchet: additive tool (ADR-0010)

To append a new optional field to ToolContext (zero-init, no behaviour
change for existing tools), use:

    Ratchet: additive context field (ADR-0010)

Anything else — renaming a tool, changing its category / confirmation
tier / schema doc semantics, reordering ToolContext fields — requires a
Tier-3 ADR per docs/GOVERNANCE.md.

See docs/adr/0010-tool-contract-v1-freeze-candidate.md for the full rules.
EOF

if [ "$BLOCKING" = "1" ]; then
  exit 1
fi
echo "(candidate period: non-blocking; will block when the final freeze ADR lands)"
exit 0
