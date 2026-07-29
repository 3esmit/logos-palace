#!/usr/bin/env bash

set -euo pipefail

artifacts_dir="$1"
if [ -z "${PALACE_MVP_PROCESS_SCOPE_UNIT:-}" ] \
  || [ -z "${PALACE_MVP_PROCESS_SCOPE_SLICE:-}" ] \
  || [ -z "${PALACE_MVP_PROCESS_SCOPE_PREFIX:-}" ] \
  || [ "${PALACE_MVP_LOCK_FD+x}" = "x" ]; then
  printf 'Standalone probe lacks exact scope environment\n' >&2
  exit 1
fi
printf 'started\n' >"${artifacts_dir}/standalone-probe-started"
sleep "${PALACE_STANDALONE_PROBE_DELAY:-0}"
printf 'passed\n' >"${artifacts_dir}/standalone-probe-passed"
