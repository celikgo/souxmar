#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Sprint 10 push 8 — sign an update manifest with the release ed25519
# private key.
#
# Reads the manifest bytes from <manifest-path>, produces a detached
# signature alongside (<manifest-path>.sig, 128 hex chars +
# trailing \n). Uses libsodium via Python's PyNaCl — the same
# verification curve the client uses, so there's no algorithm drift
# between sign and verify.
#
# The private key material comes from $SOUXMAR_SIGNING_KEY (raw 64
# bytes hex-encoded). In production this is read from the offline
# HSM during a release session; for dev / CI smoke tests, a
# fixture key is used. See docs/SECURITY.md § "Release signing
# key rotation".
#
# Usage:
#   scripts/release/sign-manifest.sh <manifest.toml>

set -euo pipefail

MANIFEST="${1:-}"
if [[ -z "${MANIFEST}" || ! -f "${MANIFEST}" ]]; then
  echo "usage: $0 <manifest.toml>" >&2
  exit 64
fi
if [[ -z "${SOUXMAR_SIGNING_KEY:-}" ]]; then
  echo "error: SOUXMAR_SIGNING_KEY must be set (128 hex chars of ed25519 secret key)" >&2
  exit 64
fi

SIG_OUT="${MANIFEST}.sig"

python3 - "${MANIFEST}" "${SIG_OUT}" <<'PY'
import os, sys
try:
    import nacl.signing
    import nacl.encoding
except ImportError:
    sys.stderr.write("error: install PyNaCl (`pip install pynacl`)\n")
    sys.exit(70)

manifest_path, sig_path = sys.argv[1], sys.argv[2]
key_hex = os.environ["SOUXMAR_SIGNING_KEY"].strip().lower()
if len(key_hex) != 128:
    sys.stderr.write(f"error: SOUXMAR_SIGNING_KEY must be 128 hex chars, got {len(key_hex)}\n")
    sys.exit(64)

key_bytes = bytes.fromhex(key_hex)
# PyNaCl's SigningKey takes the 32-byte seed; libsodium's
# crypto_sign_keypair produces a 64-byte secret key whose first
# 32 bytes are the seed. Accept both shapes for forward-compat
# with future scripts.
seed = key_bytes[:32]
sk = nacl.signing.SigningKey(seed)

with open(manifest_path, "rb") as f:
    msg = f.read()
signed = sk.sign(msg)
sig = signed.signature  # 64 raw bytes
hex_sig = sig.hex()

with open(sig_path, "w", encoding="ascii") as f:
    f.write(hex_sig + "\n")

# Also print the matching public key so the release engineer can
# cross-check it against the embedded trust store.
pk_hex = sk.verify_key.encode(encoder=nacl.encoding.HexEncoder).decode("ascii")
sys.stderr.write(f"signed {manifest_path}\n")
sys.stderr.write(f"  signature: {sig_path}\n")
sys.stderr.write(f"  pubkey:    {pk_hex}\n")
PY

echo "[sign-manifest] wrote ${SIG_OUT}"
