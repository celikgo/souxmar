#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Sprint 10 push 8 — release orchestrator. Ties the three platforms'
# signing flows together so a single `build-release.sh <version>`
# produces a fully-signed, manifest-attached release.
#
# What this script does:
#   1. Validates the working tree is at a clean tag matching <version>.
#   2. Builds the binary artefacts for the host platform only
#      (cross-OS signing happens on platform-specific CI runners; this
#      script is the per-runner driver).
#   3. Platform-specific signing:
#        macOS  -> notarize-macos.sh against the built .dmg
#        Windows -> sign-windows.ps1 against the .msi
#        Linux  -> dpkg-sig / detached gpg signature (see below)
#   4. Hashes the signed artefact, emits an [[artifact]] entry the
#      release pipeline collates into the channel manifest.
#   5. Optionally signs the manifest itself via sign-manifest.sh
#      (only invoked on the orchestrator runner, not per-platform
#      runner — the release engineer holds the private key).
#
# Expected env vars vary by --target:
#   --target=macos    requires AC_USERNAME / AC_PASSWORD / AC_TEAM_ID
#   --target=windows  requires SOUXMAR_EV_THUMBPRINT
#   --target=linux    requires GPG_KEY_ID
#   --target=manifest requires SOUXMAR_SIGNING_KEY (orchestrator only)
#
# Each platform's CI job runs this with the matching --target. The
# orchestrator final step (publish.yml in release.yml) runs with
# --target=manifest to emit + sign the channel manifest after every
# per-platform job uploads its artefact.

set -euo pipefail

VERSION=""
TARGET=""
ARTIFACT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)  VERSION="$2";  shift 2 ;;
    --target)   TARGET="$2";   shift 2 ;;
    --artifact) ARTIFACT="$2"; shift 2 ;;
    *) echo "unknown flag: $1" >&2; exit 64 ;;
  esac
done

if [[ -z "${VERSION}" || -z "${TARGET}" ]]; then
  cat >&2 <<USAGE
usage: $0 --version <X.Y.Z> --target <macos|windows|linux|manifest>
                            [--artifact <path>]

The --artifact flag is required for --target macos/windows/linux and
names the binary to sign. For --target manifest, it names the
manifest TOML file to sign (default: artefacts/manifest.toml).
USAGE
  exit 64
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${TARGET}" in
  macos)
    [[ -n "${ARTIFACT}" ]] || { echo "--artifact required for macos" >&2; exit 64; }
    "${SCRIPT_DIR}/notarize-macos.sh" "${ARTIFACT}"
    ;;
  windows)
    [[ -n "${ARTIFACT}" ]] || { echo "--artifact required for windows" >&2; exit 64; }
    pwsh "${SCRIPT_DIR}/sign-windows.ps1" "${ARTIFACT}"
    ;;
  linux)
    [[ -n "${ARTIFACT}" ]] || { echo "--artifact required for linux" >&2; exit 64; }
    : "${GPG_KEY_ID:?GPG_KEY_ID must be set for linux signing}"
    # Detached gpg signature alongside the .tar.gz / .deb / .rpm.
    # The package managers verify against the gpg key embedded in
    # the souxmar release keyring (shipped in docs/releases/
    # *.asc files — published once per key, never rotated mid-cycle).
    gpg --batch --yes \
        --default-key "${GPG_KEY_ID}" \
        --detach-sign --armor \
        --output "${ARTIFACT}.asc" \
        "${ARTIFACT}"
    echo "[build-release] linux: ${ARTIFACT}.asc"
    ;;
  manifest)
    MAN="${ARTIFACT:-artefacts/manifest.toml}"
    [[ -f "${MAN}" ]] || { echo "manifest file not found: ${MAN}" >&2; exit 64; }
    "${SCRIPT_DIR}/sign-manifest.sh" "${MAN}"
    ;;
  *)
    echo "unknown --target '${TARGET}'" >&2
    exit 64
    ;;
esac

echo "[build-release] ${TARGET} signing complete (version ${VERSION})"
