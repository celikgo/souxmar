#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# apt-install.sh — install Debian packages, with a bounded retry.
#
# GitHub's Ubuntu runners talk to Azure package mirrors that intermittently
# stall rather than fail: apt-get sits with the connection open and never
# returns. A job with no timeout then burns its full six-hour default, and a
# job with one is cancelled mid-step with nothing useful in the log — which
# is how four Linux legs of this pipeline once spent seventy minutes each
# doing nothing.
#
# So each attempt is given a hard deadline and retried. A mirror that is
# genuinely down still fails the job, but it fails in minutes with a clear
# message rather than silently occupying a runner.
#
# Usage:
#   scripts/ci/apt-install.sh <package>...

set -uo pipefail

if [ "$#" -eq 0 ]; then
  echo "apt-install: no packages given" >&2
  exit 2
fi

ATTEMPTS="${APT_INSTALL_ATTEMPTS:-3}"
TIMEOUT_SECS="${APT_INSTALL_TIMEOUT:-300}"

# `timeout` is coreutils; present on every GitHub Ubuntu image.
run_with_deadline() {
  timeout --signal=KILL "$TIMEOUT_SECS" "$@"
}

for attempt in $(seq 1 "$ATTEMPTS"); do
  echo "apt-install: attempt $attempt/$ATTEMPTS — $*"

  if ! run_with_deadline sudo apt-get update -qq; then
    echo "apt-install: 'apt-get update' failed or timed out" >&2
  fi

  if run_with_deadline sudo apt-get install -y --no-install-recommends "$@"; then
    echo "apt-install: installed $*"
    exit 0
  fi

  echo "apt-install: attempt $attempt failed" >&2
  if [ "$attempt" -lt "$ATTEMPTS" ]; then
    sleep $((attempt * 15))
  fi
done

echo "::error::apt-install: could not install after $ATTEMPTS attempts: $*" >&2
echo "Each attempt was capped at ${TIMEOUT_SECS}s. A stall here is usually the" >&2
echo "runner's package mirror rather than anything in this repository." >&2
exit 1
