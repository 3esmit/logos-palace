#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 5 ]; then
  printf '%s\n' \
    'usage: run-basecamp-standalone-scoped.sh <gate1|gate2> <snapshot> <tools> <artifacts> <runner>' \
    >&2
  exit 2
fi

gate="$1"
product_snapshot="$2"
acceptance_tools="$3"
artifacts_dir="$4"
gate_runner="$5"
if [[ ! "${gate}" =~ ^gate[12]$ ]] \
  || [[ ! "${product_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
  || [[ ! "${acceptance_tools}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
  || [ "$(realpath -e -- "${product_snapshot}")" != "${product_snapshot}" ] \
  || [ "$(realpath -e -- "${acceptance_tools}")" != "${acceptance_tools}" ] \
  || [ "$(realpath -e -- "${gate_runner}")" != "${gate_runner}" ] \
  || [ "$(realpath -e -- "${artifacts_dir}")" != "${artifacts_dir}" ]; then
  printf 'Standalone process scope inputs are invalid\n' >&2
  exit 1
fi

scope_control="${product_snapshot}/tests/basecamp_scope_control.mjs"
scope_guardian="${product_snapshot}/tests/basecamp_scope_guardian.mjs"
lock_attestor="${product_snapshot}/tests/basecamp_release_lock.mjs"
standalone_script="$(realpath -e -- "${BASH_SOURCE[0]}")"
systemd_run="${acceptance_tools}/bin/systemd-run"
systemctl_bin="${acceptance_tools}/bin/systemctl"
flock_bin="${acceptance_tools}/bin/flock"
setpriv_bin="${acceptance_tools}/bin/setpriv"
bash_bin="${acceptance_tools}/bin/bash"
if [ ! -f "${scope_control}" ] \
  || [ -L "${scope_control}" ] \
  || [ ! -f "${scope_guardian}" ] \
  || [ -L "${scope_guardian}" ] \
  || [ ! -f "${lock_attestor}" ] \
  || [ -L "${lock_attestor}" ] \
  || [ ! -x "${systemd_run}" ] \
  || [ ! -x "${systemctl_bin}" ] \
  || [ ! -x "${flock_bin}" ] \
  || [ ! -x "${setpriv_bin}" ] \
  || [ ! -x "${bash_bin}" ] \
  || [ ! -x "${acceptance_tools}/bin/node" ] \
  || [ ! -x "${acceptance_tools}/bin/setsid" ]; then
  printf 'Standalone process scope tools are incomplete\n' >&2
  exit 1
fi
"${acceptance_tools}/bin/chmod" 700 -- "${artifacts_dir}"
if [ "$("${acceptance_tools}/bin/stat" -c '%u:%a' -- \
    "${artifacts_dir}")" \
    != "$("${acceptance_tools}/bin/id" -u):700" ]; then
  printf 'Standalone artifacts directory is not owner-only state\n' >&2
  exit 1
fi

standalone_lock="${artifacts_dir}/standalone-scope.lock"
if [ -L "${standalone_lock}" ] \
  || { [ -e "${standalone_lock}" ] && [ ! -f "${standalone_lock}" ]; }; then
  printf 'Standalone scope lock path is unsafe\n' >&2
  exit 1
fi
(
  umask 077
  : >>"${standalone_lock}"
)
"${acceptance_tools}/bin/chmod" 600 -- "${standalone_lock}"
if [ "$("${acceptance_tools}/bin/realpath" -e -- "${standalone_lock}")" \
    != "${standalone_lock}" ] \
  || [ "$("${acceptance_tools}/bin/stat" -c '%u:%a' -- \
    "${standalone_lock}")" \
    != "$("${acceptance_tools}/bin/id" -u):600" ]; then
  printf 'Standalone scope lock is not owner-only state\n' >&2
  exit 1
fi
if [ "${PALACE_STANDALONE_SCOPE_LOCKED+x}" != "x" ]; then
  if [ "${PALACE_STANDALONE_SCOPE_LOCK_PATH+x}" = "x" ]; then
    printf 'Standalone scope lock path is reserved for handoff\n' >&2
    exit 1
  fi
  export PALACE_STANDALONE_SCOPE_LOCKED=1
  export PALACE_STANDALONE_SCOPE_LOCK_PATH="${standalone_lock}"
  exec "${flock_bin}" \
    --exclusive \
    --nonblock \
    --conflict-exit-code 75 \
    --close \
    -- \
    "${standalone_lock}" \
    "${setpriv_bin}" --pdeathsig KILL \
    "${bash_bin}" -p \
    "${standalone_script}" \
    "${gate}" \
    "${product_snapshot}" \
    "${acceptance_tools}" \
    "${artifacts_dir}" \
    "${gate_runner}"
fi
if [ "${PALACE_STANDALONE_SCOPE_LOCKED:-}" != "1" ] \
  || [ "${PALACE_STANDALONE_SCOPE_LOCK_PATH:-}" != "${standalone_lock}" ] \
  || [ "${PALACE_MVP_LOCK_FD+x}" = "x" ]; then
  printf 'Standalone scope lacks exact lock supervision\n' >&2
  exit 1
fi
lock_attestation="$(
  "${acceptance_tools}/bin/node" \
    "${lock_attestor}" \
    attest-standalone \
    "${standalone_lock}" \
    "${flock_bin}" \
    "${standalone_script}" \
    "${gate}" \
    "${product_snapshot}" \
    "${acceptance_tools}" \
    "${artifacts_dir}" \
    "${gate_runner}" \
    "$$"
)"
if ! "${acceptance_tools}/bin/jq" -e \
    --arg lock "${standalone_lock}" \
    'select(
       .schema == "logos.palace.standalone-lock-attestation"
       and .version == 1
       and .lockPath == $lock
       and (.supervisorPid | type == "number" and . > 1 and floor == .)
       and (
         .supervisorStartTimeTicks
         | type == "number" and . > 0 and floor == .
       )
     )' <<<"${lock_attestation}" >/dev/null; then
  printf 'Standalone scope lock attestation is invalid\n' >&2
  exit 1
fi

death_coupled_node=(
  "${setpriv_bin}"
  --pdeathsig
  KILL
  "${bash_bin}"
  -p
  -c
  'expected_parent="$1"
   shift
   if [ "$PPID" != "$expected_parent" ]; then
     exit 125
   fi
   exec "$@"'
  palace-death-coupled-node
  "$$"
  "${acceptance_tools}/bin/node"
)
if ! "${death_coupled_node[@]}" "${scope_control}" preflight \
    >/dev/null \
  || [ "$("${systemctl_bin}" --user is-system-running 2>/dev/null)" \
    != "running" ]; then
  printf 'Standalone gate requires a running user manager and cgroup v2\n' >&2
  exit 1
fi

scope_evidence="${artifacts_dir}/process-scope.json"
scope_history="${artifacts_dir}/process-scope-history"
scope_launch="${artifacts_dir}/process-scope-launch.json"
if [ -L "${scope_evidence}" ] \
  || { [ -e "${scope_evidence}" ] && [ ! -f "${scope_evidence}" ]; } \
  || [ -L "${scope_history}" ] \
  || { [ -e "${scope_history}" ] && [ ! -d "${scope_history}" ]; } \
  || [ -L "${scope_launch}" ] \
  || { [ -e "${scope_launch}" ] && [ ! -f "${scope_launch}" ]; }; then
  printf 'Standalone process scope evidence path is unsafe\n' >&2
  exit 1
fi
"${acceptance_tools}/bin/mkdir" -p -- "${scope_history}"
"${acceptance_tools}/bin/chmod" 700 -- "${scope_history}"
if [ "$("${acceptance_tools}/bin/realpath" -e -- "${scope_history}")" \
    != "${scope_history}" ] \
  || [ "$("${acceptance_tools}/bin/stat" -c '%u:%a' -- \
    "${scope_history}")" \
    != "$("${acceptance_tools}/bin/id" -u):700" ]; then
  printf 'Standalone process scope history is not owner-only state\n' >&2
  exit 1
fi

if [ -f "${scope_launch}" ]; then
  recovered="$(
    "${death_coupled_node[@]}" "${scope_control}" recover \
      "${scope_launch}" "${systemctl_bin}"
  )"
  if [ "${recovered}" != "clean" ] \
    && [ "${recovered}" != "residue-killed" ]; then
    printf 'Standalone prior process scope recovery result is invalid\n' >&2
    exit 1
  fi
  if ! "${death_coupled_node[@]}" "${scope_control}" archive \
      "${scope_launch}" "${scope_history}" >/dev/null; then
    printf 'Standalone recovered launch could not be archived\n' >&2
    exit 1
  fi
fi
if [ -f "${scope_evidence}" ]; then
  recovered="$(
    "${death_coupled_node[@]}" "${scope_control}" cleanup \
      "${scope_evidence}"
  )"
  if [ "${recovered}" != "clean" ] \
    && [ "${recovered}" != "residue-killed" ]; then
    printf 'Standalone prior scope recovery result is invalid\n' >&2
    exit 1
  fi
  if ! "${death_coupled_node[@]}" "${scope_control}" archive \
      "${scope_evidence}" "${scope_history}" >/dev/null; then
    printf 'Standalone recovered scope could not be archived\n' >&2
    exit 1
  fi
fi

attempt_file="$(
  "${acceptance_tools}/bin/mktemp" \
    "${artifacts_dir}/.process-scope-attempt.XXXXXXXX"
)"
attempt_id="${attempt_file##*.}"
"${acceptance_tools}/bin/rm" -f -- "${attempt_file}"
if [[ ! "${attempt_id}" =~ ^[A-Za-z0-9]{8}$ ]]; then
  printf 'Standalone process scope attempt identity is invalid\n' >&2
  exit 1
fi
scope_prefix="logos-palace-run-${attempt_id}"
scope_slice="${scope_prefix}.slice"
scope_unit="${scope_prefix}-${gate}-${attempt_id}.scope"
"${death_coupled_node[@]}" "${scope_control}" plan \
  "${scope_unit}" "${scope_slice}" "${scope_launch}" >/dev/null

active_pid=""
scope_attested=0

handle_signal() {
  local signal_name="$1"
  local cleanup_result
  local cleanup_status
  trap - HUP INT TERM
  if [ "${scope_attested}" -eq 1 ]; then
    set +e
    cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" cleanup \
        "${scope_evidence}" 255
    )"
    cleanup_status=$?
    set -e
  else
    set +e
    cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" recover \
        "${scope_launch}" "${systemctl_bin}"
    )"
    cleanup_status=$?
    set -e
  fi
  if [ "${cleanup_status}" -ne 0 ] \
    || { [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; }; then
    printf \
      'Standalone gate interrupted by %s; exact scope cleanup failed\n' \
      "${signal_name}" >&2
    exit 1
  fi
  printf 'Standalone gate interrupted by %s\n' "${signal_name}" >&2
  exit 1
}
trap 'handle_signal HUP' HUP
trap 'handle_signal INT' INT
trap 'handle_signal TERM' TERM

snapshot_variable="PALACE_GATE${gate#gate}_PRODUCT_SNAPSHOT"
active_pid=""
scope_attested=0
"${systemd_run}" \
  --user \
  --scope \
  --quiet \
  --collect \
  --expand-environment=no \
  --slice="${scope_slice}" \
  --unit="${scope_unit}" \
  --property=KillMode=control-group \
  "${setpriv_bin}" \
    --pdeathsig TERM \
  "${acceptance_tools}/bin/bash" \
    -p \
    -c 'expected_parent="$1"
        shift
        trap - TERM
        if [ "$PPID" != "$expected_parent" ]; then
          exit 125
        fi
        builtin kill -STOP "$$"
        if [ "$PPID" != "$expected_parent" ]; then
          exit 125
        fi
        builtin exec "$@"' \
    palace-scope-barrier \
    "$$" \
    "${acceptance_tools}/bin/node" \
    "${scope_guardian}" \
    "${scope_unit}" \
    "${scope_slice}" \
    "${acceptance_tools}/bin/setsid" --wait \
    "${acceptance_tools}/bin/env" \
      "${snapshot_variable}=${product_snapshot}" \
      "PALACE_MVP_PROCESS_SCOPE_PREFIX=${scope_prefix}" \
      "PALACE_MVP_PROCESS_SCOPE_SLICE=${scope_slice}" \
      "PALACE_MVP_PROCESS_SCOPE_UNIT=${scope_unit}" \
      "${acceptance_tools}/bin/bash" \
      -p \
      "${gate_runner}" \
      "${artifacts_dir}" &
active_pid=$!

set +e
"${death_coupled_node[@]}" "${scope_control}" attest \
  "${active_pid}" "${scope_unit}" "${scope_slice}" \
  "${scope_evidence}" "${systemctl_bin}" >/dev/null
attestation_status=$?
set -e
if [ "${attestation_status}" -ne 0 ]; then
  set +e
  recovery_result="$(
    "${death_coupled_node[@]}" "${scope_control}" recover \
      "${scope_launch}" "${systemctl_bin}"
  )"
  recovery_status=$?
  set -e
  if [ "${recovery_status}" -ne 0 ] \
    || { [ "${recovery_result}" != "clean" ] \
      && [ "${recovery_result}" != "residue-killed" ]; }; then
    printf \
      'Standalone scope attestation and exact recovery failed\n' >&2
    exit 1
  fi
  printf 'Standalone process scope attestation failed\n' >&2
  exit 1
fi
scope_attested=1
if ! "${death_coupled_node[@]}" "${scope_control}" finish-launch \
    "${scope_launch}" "${scope_evidence}" >/dev/null; then
  set +e
  cleanup_result="$(
    "${death_coupled_node[@]}" "${scope_control}" cleanup \
      "${scope_evidence}" 255
  )"
  cleanup_status=$?
  set -e
  if [ "${cleanup_status}" -ne 0 ] \
    || { [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; }; then
    printf \
      'Standalone scope transition and exact cleanup failed\n' >&2
    exit 1
  fi
  printf 'Standalone process scope launch transition failed\n' >&2
  exit 1
fi
if ! "${death_coupled_node[@]}" "${scope_control}" release \
    "${scope_evidence}" "${systemctl_bin}" >/dev/null; then
  set +e
  cleanup_result="$(
    "${death_coupled_node[@]}" "${scope_control}" cleanup \
      "${scope_evidence}" 255
  )"
  cleanup_status=$?
  set -e
  if [ "${cleanup_status}" -ne 0 ] \
    || { [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; }; then
    printf \
      'Standalone scope release and exact cleanup failed\n' >&2
    exit 1
  fi
  printf 'Standalone process scope barrier release failed\n' >&2
  exit 1
fi

set +e
wait "${active_pid}"
gate_status=$?
cleanup_result="$(
  "${death_coupled_node[@]}" "${scope_control}" cleanup \
    "${scope_evidence}" "${gate_status}"
)"
cleanup_status=$?
set -e
active_pid=""
scope_attested=0
if [ "${cleanup_status}" -ne 0 ] || [ "${cleanup_result}" != "clean" ]; then
  printf 'Standalone process scope cleanup failed\n' >&2
  exit 1
fi
exit "${gate_status}"
