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

# SIGTERM first, SIGKILL only if it will not go. A killed apt-get leaves
# /var/lib/apt/lists/lock held, so the next attempt dies instantly with
# "Could not get lock" — the retry then manufactures the failure it exists
# to survive. TERM lets apt release the lock on its way out.
run_with_deadline() {
  timeout --signal=TERM --kill-after=30 "$TIMEOUT_SECS" "$@"
}

# And in case something else on the image is mid-apt: let apt wait for the
# lock rather than erroring on it. This is apt's own supported knob and is
# more reliable than sleeping and hoping.
APT_OPTS=(-o "DPkg::Lock::Timeout=${APT_LOCK_TIMEOUT:-180}")

for attempt in $(seq 1 "$ATTEMPTS"); do
  echo "apt-install: attempt $attempt/$ATTEMPTS — $*"

  if ! run_with_deadline sudo apt-get "${APT_OPTS[@]}" update -qq; then
    # Not fatal on its own: the package lists on the image are usually
    # recent enough to install from. It only matters when the package is
    # genuinely absent, which the install below reports properly.
    echo "apt-install: 'apt-get update' failed or timed out; continuing" >&2
  fi

  if run_with_deadline sudo apt-get "${APT_OPTS[@]}" install -y \
       --no-install-recommends "$@"; then
    echo "apt-install: installed $*"
    exit 0
  fi

  echo "apt-install: attempt $attempt failed" >&2
  if [ "$attempt" -lt "$ATTEMPTS" ]; then
    sleep $((attempt * 15))
  fi
done

# Plain message, not ::error::. Callers legitimately recover from this —
# setup-cpp tries gcc-13 from the archive and falls back to the toolchain
# PPA when it is absent — and a red annotation for a handled failure sends
# people looking at the wrong step.
echo "apt-install: could not install after $ATTEMPTS attempts: $*" >&2
echo "Each attempt was capped at ${TIMEOUT_SECS}s. A stall here is usually the" >&2
echo "runner's package mirror rather than anything in this repository." >&2
exit 1
