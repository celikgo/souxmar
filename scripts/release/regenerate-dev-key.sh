#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Sprint 10 push 8 — regenerate the in-tree dev signing keypair.
#
# The dev key is the placeholder pubkey embedded by default in the
# `souxmar` CLI's trust store (see src/updater/CMakeLists.txt's
# SOUXMAR_RELEASE_PUBKEY_HEX cache var). Release builds *always*
# override this — the release CI workflow refuses to publish if the
# embedded key id is still "souxmar-dev-key". This script exists
# only for local developers who want to produce a real signed
# manifest against the in-tree default trust store (typically for
# end-to-end smoke testing without spinning up a per-test keypair).
#
# Output:
#   * scripts/release/dev-signing-key.local.txt  — 128 hex chars of
#     the ed25519 SECRET KEY (the full 64-byte form). Gitignored.
#   * stdout — the matching public key hex that can be pasted into
#     CMakeCache as -DSOUXMAR_RELEASE_PUBKEY_HEX=<this>.
#
# The committed placeholder hex in CMakeLists.txt is not a real
# ed25519 public key (it is arbitrary bytes) — the integration tests
# generate their own keypairs at test time, so this placeholder is
# only ever exercised by anyone running the CLI by hand.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRIVATE_OUT="${SCRIPT_DIR}/dev-signing-key.local.txt"

python3 - "${PRIVATE_OUT}" <<'PY'
import sys
try:
    import nacl.signing
    import nacl.encoding
except ImportError:
    sys.stderr.write("error: install PyNaCl (`pip install pynacl`)\n")
    sys.exit(70)

sk = nacl.signing.SigningKey.generate()
sk_hex = sk.encode(encoder=nacl.encoding.HexEncoder).decode("ascii")
pk_hex = sk.verify_key.encode(encoder=nacl.encoding.HexEncoder).decode("ascii")

with open(sys.argv[1], "w", encoding="ascii") as f:
    f.write(sk_hex + "\n")

print(pk_hex)
PY

PUBHEX="$(tail -n 1 <<<"$(python3 - "${PRIVATE_OUT}" <<'PY'
import sys
try:
    import nacl.signing, nacl.encoding
except ImportError:
    sys.stderr.write("error: install PyNaCl (`pip install pynacl`)\n")
    sys.exit(70)
with open(sys.argv[1], "r", encoding="ascii") as f:
    sk_hex = f.read().strip()
sk = nacl.signing.SigningKey(bytes.fromhex(sk_hex)[:32])
print(sk.verify_key.encode(encoder=nacl.encoding.HexEncoder).decode("ascii"))
PY
)" || true)"

cat <<USAGE
[regenerate-dev-key] wrote private key to ${PRIVATE_OUT}
[regenerate-dev-key] matching public key:
                     ${PUBHEX}

Reconfigure the build with:
  cmake -B build -S . \
    -DSOUXMAR_RELEASE_PUBKEY_ID=souxmar-dev-key \
    -DSOUXMAR_RELEASE_PUBKEY_HEX=${PUBHEX}

Then sign manifests with:
  SOUXMAR_SIGNING_KEY=\$(cat ${PRIVATE_OUT}) \\
    scripts/release/sign-manifest.sh <manifest.toml>
USAGE
