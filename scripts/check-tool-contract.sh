#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check-tool-contract.sh — block PRs touching the frozen v1 agent tool
# contract surface unless the commit messages carry a recognised ratchet
# token.
#
# Sprint 9 push 1 (ADR-0011) declared the agent tool contract frozen
# final at v1 — superseding the freeze-candidate ADR-0010. This script
# is the CI gate that enforces it.
#
# Pass conditions:
#   - No changes touch any contract-surface file.
#   - All changes are documentation / test / CMake adjustments (the
#     framework header itself is untouched).
#   - At least one commit message in the PR range carries one of the
#     ratchet markers:
#       * "Ratchet: additive tool (ADR-0010)"
#       * "Ratchet: additive context field (ADR-0010)"
#     (The ADR-0010 spelling is preserved across the freeze-final
#      transition so reviewer muscle memory carries through; ADR-0011
#      inherits the ratchet vocabulary.)
#
# Fail conditions:
#   - The framework header (include/souxmar/ai/tool.h) changes without
#     the additive-context-field marker.
#   - default_v1_tools() loses a tool or reorders the catalogue in a way
#     the registry-count assertion in test_ai_tools.cpp cannot reconcile.
#   - Any other contract-surface edit without a ratchet marker.
#
# Usage:
#   scripts/check-tool-contract.sh [<base-ref>] [<head-ref>]
#
# Defaults to comparing HEAD against `origin/main`. Blocking-by-default;
# set SOUXMAR_TOOL_CONTRACT_BLOCKING=0 only for local dry-run inspection
# of an in-progress ratchet PR (the CI job never sets it).

set -euo pipefail

BASE_REF="${1:-origin/main}"
HEAD_REF="${2:-HEAD}"
BLOCKING="${SOUXMAR_TOOL_CONTRACT_BLOCKING:-1}"

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
tool-contract: tool-contract v1 lockdown gate REJECTED this PR.

The agent tool contract is frozen final at v1 (ADR-0011, superseding
ADR-0010). To land an additive new tool, add this exact marker to the
relevant commit message:

    Ratchet: additive tool (ADR-0010)

To append a new optional field to ToolContext (zero-init, no behaviour
change for existing tools), use:

    Ratchet: additive context field (ADR-0010)

(Ratchet markers preserve the ADR-0010 spelling on purpose — the
vocabulary carries through the freeze-final transition unchanged.)

Anything else — renaming a tool, changing its category / confirmation
tier / schema doc semantics, reordering ToolContext fields — requires a
Tier-3 ADR per docs/GOVERNANCE.md and is in practice a v2 tool-catalogue
conversation.

See docs/adr/0011-tool-contract-v1-final-freeze.md for the full rules.
EOF

if [ "$BLOCKING" = "1" ]; then
  exit 1
fi
echo "(SOUXMAR_TOOL_CONTRACT_BLOCKING=0 set; warning only — CI runs blocking)"
exit 0
