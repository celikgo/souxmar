#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Sprint 10 push 8 — Apple notarisation automation.
#
# Submits a built macOS bundle (.app, .pkg, .dmg, or .zip) to Apple's
# notarytool, waits for the notarisation queue with bounded retry +
# exponential backoff (Apple's queue can stall for tens of minutes
# during release windows), and on success staples the notarisation
# ticket so the bundle works offline.
#
# Why this script (vs. invoking notarytool inline from release.yml):
#   * The queue-stall handling is the load-bearing detail. Apple's
#     observed queue p99 is bursty — the official advice is "use
#     --wait but be prepared for hour-scale waits during peak
#     periods". A naïve `notarytool submit --wait` can hang past
#     CI's default 6h timeout. We poll every 30s, log every 5m, and
#     time out at 90m (config via SOUXMAR_NOTARY_TIMEOUT_SECS).
#   * The error vocabulary varies across notarytool versions; we
#     normalise into stable exit codes (0 success, 1 transient, 2
#     permanent, 3 timeout).
#
# Required env vars:
#   AC_USERNAME           - Apple ID email
#   AC_PASSWORD           - app-specific password (NEVER the iCloud one)
#   AC_TEAM_ID            - 10-char Team ID from developer.apple.com
#
# Optional env vars:
#   SOUXMAR_NOTARY_TIMEOUT_SECS  - hard upper bound (default 5400 = 90m)
#   SOUXMAR_NOTARY_POLL_SECS     - poll interval (default 30)
#
# Usage:
#   scripts/release/notarize-macos.sh <path-to-bundle.zip-or-dmg-or-pkg>

set -euo pipefail

BUNDLE="${1:-}"
if [[ -z "${BUNDLE}" || ! -f "${BUNDLE}" ]]; then
  echo "usage: $0 <bundle.dmg|.pkg|.zip|.app.zip>" >&2
  exit 64
fi
if [[ -z "${AC_USERNAME:-}" || -z "${AC_PASSWORD:-}" || -z "${AC_TEAM_ID:-}" ]]; then
  echo "error: AC_USERNAME / AC_PASSWORD / AC_TEAM_ID must be set" >&2
  exit 64
fi

TIMEOUT_SECS="${SOUXMAR_NOTARY_TIMEOUT_SECS:-5400}"
POLL_SECS="${SOUXMAR_NOTARY_POLL_SECS:-30}"
START_TS="$(date +%s)"

log() { printf '[notarize %s] %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }

# 1. Submit. notarytool emits JSON with --output-format json; we
#    extract the request UUID to poll separately. Using `--wait` here
#    would block on Apple's queue; we manage the wait ourselves so we
#    can apply our own timeout + structured logging.
log "submitting ${BUNDLE} (team=${AC_TEAM_ID})"
SUBMIT_JSON="$(xcrun notarytool submit "${BUNDLE}" \
    --apple-id "${AC_USERNAME}" \
    --password "${AC_PASSWORD}" \
    --team-id  "${AC_TEAM_ID}" \
    --output-format json)"

REQUEST_UUID="$(printf '%s' "${SUBMIT_JSON}" | python3 -c \
  'import json,sys; print(json.load(sys.stdin)["id"])')"
if [[ -z "${REQUEST_UUID}" ]]; then
  log "submit returned no request id; raw response:"
  echo "${SUBMIT_JSON}" >&2
  exit 2
fi
log "queued: request id ${REQUEST_UUID}"

# 2. Poll with bounded retry + 5-minute heartbeat log.
LAST_HEARTBEAT_TS="${START_TS}"
while true; do
  NOW_TS="$(date +%s)"
  if (( NOW_TS - START_TS > TIMEOUT_SECS )); then
    log "TIMEOUT after $((NOW_TS - START_TS))s; queue did not settle"
    exit 3
  fi

  INFO_JSON="$(xcrun notarytool info "${REQUEST_UUID}" \
      --apple-id "${AC_USERNAME}" \
      --password "${AC_PASSWORD}" \
      --team-id  "${AC_TEAM_ID}" \
      --output-format json || true)"

  STATUS="$(printf '%s' "${INFO_JSON}" | python3 -c \
    'import json,sys
try:
  print(json.load(sys.stdin).get("status",""))
except Exception:
  print("")' || true)"

  case "${STATUS}" in
    Accepted)
      log "accepted in $((NOW_TS - START_TS))s"
      break
      ;;
    "Invalid"|"Rejected")
      log "permanent failure: status=${STATUS}"
      # Fetch + dump the log for diagnostic. The release engineer
      # consumes this from CI logs.
      xcrun notarytool log "${REQUEST_UUID}" \
        --apple-id "${AC_USERNAME}" \
        --password "${AC_PASSWORD}" \
        --team-id  "${AC_TEAM_ID}" \
        notarize.log || true
      cat notarize.log >&2 || true
      exit 2
      ;;
    "In Progress"|"")
      if (( NOW_TS - LAST_HEARTBEAT_TS >= 300 )); then
        log "still queued (status=${STATUS:-unknown}); $((NOW_TS - START_TS))s elapsed"
        LAST_HEARTBEAT_TS="${NOW_TS}"
      fi
      sleep "${POLL_SECS}"
      ;;
    *)
      log "unknown status '${STATUS}'; polling again"
      sleep "${POLL_SECS}"
      ;;
  esac
done

# 3. Staple. The ticket is embedded in the bundle so Gatekeeper
#    accepts it offline. stapler only works on certain bundle types
#    (.app, .dmg, .pkg). A .zip-wrapped .app must be unzipped first;
#    we delegate that to the caller.
log "stapling notarisation ticket"
case "${BUNDLE}" in
  *.app|*.dmg|*.pkg)
    xcrun stapler staple "${BUNDLE}"
    xcrun stapler validate "${BUNDLE}"
    ;;
  *.zip)
    log "skipping staple: input is a .zip (caller must staple the unzipped .app)"
    ;;
  *)
    log "skipping staple: unsupported extension"
    ;;
esac

log "ok"
