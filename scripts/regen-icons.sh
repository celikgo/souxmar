#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# regen-icons.sh — regenerate the full desktop-app icon bundle from
# src/desktop/src-tauri/icons/icon.svg.
#
# Outputs, all under src/desktop/src-tauri/icons/:
#   - icon.png                    1024x1024 master
#   - 32x32.png, 128x128.png,     Linux + dev path
#     128x128@2x.png
#   - icon.icns                   macOS native; 10-image iconset
#                                 (16/32/64/128/256/512/1024 @1x+@2x)
#   - icon.ico                    Windows multi-res (16/24/32/48/64/
#                                 128/256) built via Pillow
#   - Square{30,44,71,89,107,                Windows MSI / Microsoft
#     142,150,284,310}Logo.png +              Store assets
#     StoreLogo.png
#
# Re-runnable: existing files are overwritten in place.
#
# Tool prerequisites:
#   - rsvg-convert  (brew install librsvg)
#   - iconutil      (macOS, built-in)
#   - python3 + Pillow  (pip3 install pillow)
#
# macOS-only because iconutil is the only sanctioned way to build a
# valid .icns. On Linux you could swap in png2icns from libicns;
# we have not exercised that path so the script bails early.
#
# Usage:
#   scripts/regen-icons.sh
#
# Exits non-zero on any tool-missing, render, or pack failure.

set -euo pipefail

# Resolve repo root via the script's own location so the script runs
# correctly from any cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ICONS_DIR="${REPO_ROOT}/src/desktop/src-tauri/icons"
SVG="${ICONS_DIR}/icon.svg"

# ---- Preflight ---------------------------------------------------------

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "regen-icons.sh: requires macOS (uses iconutil for .icns). On" >&2
  echo "non-mac hosts, generate the PNGs with rsvg-convert and the" >&2
  echo ".icns separately with png2icns from libicns." >&2
  exit 1
fi

if [[ ! -f "${SVG}" ]]; then
  echo "regen-icons.sh: source not found: ${SVG}" >&2
  exit 1
fi

for tool in rsvg-convert iconutil python3; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "regen-icons.sh: required tool not on PATH: ${tool}" >&2
    exit 1
  fi
done

if ! python3 -c "import PIL" >/dev/null 2>&1; then
  echo "regen-icons.sh: Pillow not installed for python3 (pip3 install pillow)" >&2
  exit 1
fi

# ---- Work in a temp dir; copy final artefacts back when done -----------

WORK="$(mktemp -d -t souxmar-icons.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT

cd "${WORK}"

# Render every size we need exactly once; downstream copy/rename steps
# pick from these intermediates.
for size in 16 30 32 44 48 50 64 71 89 107 128 142 150 256 284 310 512 1024; do
  rsvg-convert -w "${size}" -h "${size}" "${SVG}" -o "tmp_${size}.png" \
    || { echo "rsvg-convert failed at ${size}px"; exit 1; }
done

# ---- Canonical Tauri set -----------------------------------------------

cp tmp_1024.png "${ICONS_DIR}/icon.png"
cp tmp_32.png   "${ICONS_DIR}/32x32.png"
cp tmp_128.png  "${ICONS_DIR}/128x128.png"
cp tmp_256.png  "${ICONS_DIR}/128x128@2x.png"

# ---- Windows MSI / Microsoft Store ------------------------------------

cp tmp_30.png  "${ICONS_DIR}/Square30x30Logo.png"
cp tmp_44.png  "${ICONS_DIR}/Square44x44Logo.png"
cp tmp_71.png  "${ICONS_DIR}/Square71x71Logo.png"
cp tmp_89.png  "${ICONS_DIR}/Square89x89Logo.png"
cp tmp_107.png "${ICONS_DIR}/Square107x107Logo.png"
cp tmp_142.png "${ICONS_DIR}/Square142x142Logo.png"
cp tmp_150.png "${ICONS_DIR}/Square150x150Logo.png"
cp tmp_284.png "${ICONS_DIR}/Square284x284Logo.png"
cp tmp_310.png "${ICONS_DIR}/Square310x310Logo.png"
cp tmp_50.png  "${ICONS_DIR}/StoreLogo.png"

# ---- macOS .icns via iconutil ------------------------------------------
#
# iconutil expects an icon.iconset/ directory containing exactly the
# ten well-known filenames below. Source pixel sizes are determined by
# the trailing @1x / @2x suffix.

mkdir -p icon.iconset
cp tmp_16.png   icon.iconset/icon_16x16.png
cp tmp_32.png   icon.iconset/icon_16x16@2x.png
cp tmp_32.png   icon.iconset/icon_32x32.png
cp tmp_64.png   icon.iconset/icon_32x32@2x.png
cp tmp_128.png  icon.iconset/icon_128x128.png
cp tmp_256.png  icon.iconset/icon_128x128@2x.png
cp tmp_256.png  icon.iconset/icon_256x256.png
cp tmp_512.png  icon.iconset/icon_256x256@2x.png
cp tmp_512.png  icon.iconset/icon_512x512.png
cp tmp_1024.png icon.iconset/icon_512x512@2x.png

iconutil -c icns icon.iconset -o "${ICONS_DIR}/icon.icns"

# ---- Windows .ico via Pillow -------------------------------------------
#
# Pillow embeds every requested size inside a single .ico. Source is
# the 256px render — Pillow downsamples for smaller embedded entries.

python3 - "${ICONS_DIR}/icon.ico" <<'PYEOF'
import sys
from PIL import Image
out_path = sys.argv[1]
img = Image.open("tmp_256.png")
img.save(out_path, format="ICO",
         sizes=[(16, 16), (24, 24), (32, 32),
                (48, 48), (64, 64),
                (128, 128), (256, 256)])
PYEOF

echo "regen-icons.sh: wrote 17 files to ${ICONS_DIR}"
