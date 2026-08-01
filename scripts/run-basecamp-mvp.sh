#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runs_root="${1:-${repo_root}/.artifacts/basecamp-mvp}"
resume_argument="${2:-${PALACE_MVP_RESUME_DIR:-}}"
immutable_runner="${PALACE_MVP_IMMUTABLE_RUNNER:-0}"

if [ "${immutable_runner}" != "0" ] && [ "${immutable_runner}" != "1" ]; then
  printf 'PALACE_MVP_IMMUTABLE_RUNNER must be absent or exactly 1\n' >&2
  exit 1
fi
if [ "${immutable_runner}" = "0" ] \
  && [ "${PALACE_MVP_RUNNER_SHA256+x}" = "x" ]; then
  printf 'PALACE_MVP_RUNNER_SHA256 is reserved for immutable handoff\n' >&2
  exit 1
fi

for forbidden_override in \
  PALACE_GATE1_INSPECTOR_PORT \
  PALACE_GATE2_INSPECTOR_PORTS \
  PALACE_GATE2_DELIVERY_PORTS \
  PALACE_GATE3_STORAGE_CONFIG_BASE \
  PALACE_KEEP_GATE1_WORK \
  PALACE_KEEP_GATE2_WORK \
  PALACE_KEEP_GATE3_WORK \
  PALACE_KEEP_GATE4_WORK \
  PALACE_MVP_CLAIM_PATH \
  PALACE_MVP_PROCESS_CGROUP \
  PALACE_MVP_PROCESS_SCOPE_PREFIX \
  PALACE_MVP_PROCESS_SCOPE_SLICE \
  PALACE_MVP_PROCESS_SCOPE_UNIT; do
  if [ "${!forbidden_override+x}" = "x" ]; then
    printf 'Official MVP runner forbids ambient override %s\n' \
      "${forbidden_override}" >&2
    exit 1
  fi
done

asset_input_root="${PALACE_E2E_ASSET_INPUT_ROOT:-}"
asset_manifest="${PALACE_E2E_ASSET_MANIFEST:-}"
canonical_asset_root="$(
  realpath -e -- "${asset_input_root}" 2>/dev/null || true
)"
canonical_asset_manifest="$(
  realpath -e -- "${asset_manifest}" 2>/dev/null || true
)"
if [ -z "${asset_input_root}" ] \
  || [ "${asset_input_root}" != "${canonical_asset_root}" ] \
  || [ -L "${asset_input_root}" ] \
  || [ ! -d "${canonical_asset_root}" ]; then
  printf 'E2E asset input root must be a canonical directory\n' >&2
  exit 1
fi
case "${canonical_asset_manifest}" in
  "${canonical_asset_root}"/*)
    ;;
  *)
    printf 'E2E asset manifest must be an input-root descendant\n' >&2
    exit 1
    ;;
esac
if [ "${asset_manifest}" != "${canonical_asset_manifest}" ] \
  || [ -L "${asset_manifest}" ] \
  || [ ! -f "${canonical_asset_manifest}" ] \
  || [ "$(stat -c '%s' -- "${canonical_asset_manifest}")" -le 0 ] \
  || [ "$(stat -c '%s' -- "${canonical_asset_manifest}")" \
    -gt 1048576 ]; then
  printf 'E2E asset manifest must be a bounded regular file\n' >&2
  exit 1
fi

if [ -L "${runs_root}" ]; then
  printf 'MVP artifacts root must not be a symlink\n' >&2
  exit 1
fi
mkdir -p "${runs_root}"
runs_root="$(realpath -e -- "${runs_root}")"
chmod 700 "${runs_root}"

product_snapshot=""
jq_bin=""
resuming=0

valid_store_path() {
  local candidate="$1"
  local canonical
  canonical="$(realpath -e -- "${candidate}" 2>/dev/null || true)"
  [ "${candidate}" = "${canonical}" ] \
    && [[ "${canonical}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    && nix path-info "${canonical}" >/dev/null 2>&1
}

valid_product_snapshot() {
  local candidate="$1"
  valid_store_path "${candidate}" && [ -d "${candidate}" ]
}

if [ -n "${resume_argument}" ]; then
  canonical_resume="$(
    realpath -e -- "${resume_argument}" 2>/dev/null || true
  )"
  if [ "${resume_argument}" != "${canonical_resume}" ] \
    || [ -L "${resume_argument}" ] \
    || [ ! -d "${canonical_resume}" ] \
    || [ "$(dirname "${canonical_resume}")" != "${runs_root}" ] \
    || [[ ! "$(basename "${canonical_resume}")" =~ ^run\.[A-Za-z0-9]{8}$ ]] \
    || [ "$(stat -c '%u' "${canonical_resume}")" != "$(id -u)" ] \
    || [ "$(stat -c '%a' "${canonical_resume}")" != "700" ]; then
    printf 'MVP resume directory is not a secure canonical run child\n' >&2
    exit 1
  fi
  run_dir="${canonical_resume}"
  resuming=1
else
  run_dir="$(mktemp -d "${runs_root}/run.XXXXXXXX")"
  chmod 700 "${run_dir}"
fi
run_basename="$(basename "${run_dir}")"
run_scope_id="${run_basename#run.}"
if [[ ! "${run_scope_id}" =~ ^[A-Za-z0-9]{8}$ ]]; then
  printf 'MVP run directory does not yield a safe scope identity\n' >&2
  exit 1
fi
process_scope_prefix="logos-palace-run-${run_scope_id}"
process_scope_slice="${process_scope_prefix}.slice"

shared_state="${run_dir}/shared-state"
compiled_report="${run_dir}/compiled-mvp-report.json"
public_evidence="${run_dir}/public-evidence.json"
claim_completion="${run_dir}/active-claim-completion.json"
snapshot_marker="${run_dir}/product-snapshot"
source_identity="${run_dir}/source-identity.json"
tracked_paths="${run_dir}/source-tree-paths.nul"
snapshot_evidence="${run_dir}/product-snapshot-evidence.json"
runtime_manifest="${run_dir}/runtime-output-manifest.json"
claim_completed=0

if [ "${resuming}" -eq 0 ]; then
  if [ -n "$(git -C "${repo_root}" status --porcelain=v1 \
    --untracked-files=all)" ]; then
    printf 'MVP source must be a clean Git HEAD including untracked files\n' >&2
    exit 1
  fi
  source_commit="$(git -C "${repo_root}" rev-parse --verify HEAD)"
  if [[ ! "${source_commit}" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'MVP source HEAD is not one exact Git commit\n' >&2
    exit 1
  fi
  bootstrap_product_ref="git+file://${repo_root}?rev=${source_commit}"
else
  if [ -L "${snapshot_marker}" ] \
    || [ ! -f "${snapshot_marker}" ] \
    || [ "$(stat -c '%a' "${snapshot_marker}")" != "600" ]; then
    printf 'MVP resume snapshot marker is missing or insecure\n' >&2
    exit 1
  fi
  mapfile -t marked_snapshots <"${snapshot_marker}"
  if [ "${#marked_snapshots[@]}" -ne 1 ] \
    || ! valid_product_snapshot "${marked_snapshots[0]}"; then
    printf 'MVP resume snapshot marker is invalid\n' >&2
    exit 1
  fi
  product_snapshot="${marked_snapshots[0]}"
  bootstrap_product_ref="path:${product_snapshot}"
fi

bootstrap_tools="$(
  nix build --no-link --print-out-paths \
    "${bootstrap_product_ref}#acceptance-tools"
)"
bootstrap_jq="${bootstrap_tools}/bin/jq"
bootstrap_flock="${bootstrap_tools}/bin/flock"
bootstrap_setpriv="${bootstrap_tools}/bin/setpriv"
bootstrap_bash="${bootstrap_tools}/bin/bash"
bootstrap_sha256="${bootstrap_tools}/bin/sha256sum"
bootstrap_systemctl="${bootstrap_tools}/bin/systemctl"
if [ ! -x "${bootstrap_jq}" ] \
  || [ ! -x "${bootstrap_flock}" ] \
  || [ ! -x "${bootstrap_setpriv}" ] \
  || [ ! -x "${bootstrap_bash}" ] \
  || [ ! -x "${bootstrap_sha256}" ] \
  || [ ! -x "${bootstrap_systemctl}" ]; then
  printf 'Could not build acceptance tools for MVP locking/archive\n' >&2
  exit 1
fi

current_uid="$("${bootstrap_tools}/bin/id" -u)"

ensure_owner_directory() {
  local directory="$1"
  local canonical
  if [ ! -e "${directory}" ]; then
    "${bootstrap_tools}/bin/mkdir" -m 700 -- "${directory}"
  fi
  canonical="$(
    "${bootstrap_tools}/bin/realpath" -e -- "${directory}" \
      2>/dev/null || true
  )"
  if [ "${directory}" != "${canonical}" ] \
    || [ -L "${directory}" ] \
    || [ ! -d "${directory}" ] \
    || [ "$("${bootstrap_tools}/bin/stat" -c '%u' "${directory}")" \
      != "${current_uid}" ] \
    || [ "$("${bootstrap_tools}/bin/stat" -c '%a' "${directory}")" \
      != "700" ]; then
    printf 'MVP durable state directory is not owner-only state\n' >&2
    exit 1
  fi
}

if [ "$("${bootstrap_tools}/bin/realpath" -e -- /var/tmp)" != "/var/tmp" ] \
  || [ ! -d /var/tmp ]; then
  printf 'MVP durable state parent is not canonical\n' >&2
  exit 1
fi
claim_directory="/var/tmp/logos-palace-${current_uid}"
durable_roots_parent="${claim_directory}/gc-roots"
durable_roots_dir="${durable_roots_parent}/$(basename "${run_dir}")"
ensure_owner_directory "${claim_directory}"
ensure_owner_directory "${durable_roots_parent}"
ensure_owner_directory "${durable_roots_dir}"
snapshot_gc_root="${durable_roots_dir}/product-snapshot"
runtime_gc_roots="${durable_roots_dir}/runtime"
ensure_owner_directory "${runtime_gc_roots}"
sandbox_test_gc_root="${run_dir}/gate0/sandbox-test-gc-root"
expected_sandbox_test_output=""

release_program_id="e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61"
release_root_id="12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a"
mvp_lock_path="${claim_directory}/release-${release_program_id}-${release_root_id}.lock"
if [ -L "${mvp_lock_path}" ] \
  || { [ -e "${mvp_lock_path}" ] && [ ! -f "${mvp_lock_path}" ]; }; then
  printf 'MVP global lock path is not a secure regular file\n' >&2
  exit 1
fi
if [ "${PALACE_MVP_LOCK_FD+x}" = "x" ]; then
  printf 'MVP runner and gates must not inherit the release lock FD\n' >&2
  exit 1
fi
if [ "${immutable_runner}" = "0" ]; then
  if [ "${PALACE_MVP_LOCK_SUPERVISED+x}" = "x" ] \
    || [ "${PALACE_MVP_LOCK_PATH+x}" = "x" ] \
    || [ "${PALACE_MVP_LOCK_SUPERVISOR_PID+x}" = "x" ] \
    || [ "${PALACE_MVP_LOCK_SUPERVISOR_START_TIME_TICKS+x}" = "x" ]; then
    printf 'Release-lock supervisor variables are reserved for handoff\n' >&2
    exit 1
  fi
  (
    umask 077
    : >>"${mvp_lock_path}"
  )
  chmod 600 "${mvp_lock_path}"
elif [ "${PALACE_MVP_LOCK_SUPERVISED:-}" != "1" ] \
  || [ "${PALACE_MVP_LOCK_PATH:-}" != "${mvp_lock_path}" ]; then
  printf 'Immutable MVP runner lacks exact release-lock supervision\n' >&2
  exit 1
fi
if [ "$(stat -c '%u' "${mvp_lock_path}")" != "${current_uid}" ] \
  || [ "$(stat -c '%a' "${mvp_lock_path}")" != "600" ] \
  || [ "$(realpath -e -- "${mvp_lock_path}")" != "${mvp_lock_path}" ]; then
  printf 'MVP global lock file is not owner-only state\n' >&2
  exit 1
fi

if [ "${resuming}" -eq 1 ]; then
  if [ -L "${source_identity}" ] \
    || [ ! -f "${source_identity}" ] \
    || [ "$(stat -c '%a' "${source_identity}")" != "600" ]; then
    printf 'MVP resume source identity is missing or insecure\n' >&2
    exit 1
  fi
  source_commit="$(
    "${bootstrap_jq}" -er \
      'select(
         .schema == "logos.palace.basecamp-source-identity"
         and .version == 1
       )
       | .gitCommit
        | select(type == "string")
        | select(test("^[0-9a-f]{40}$"))' \
      "${source_identity}"
  )"
  snapshot_nar_hash="$(
    "${bootstrap_jq}" -er \
      --arg snapshot "${product_snapshot}" \
      'select(
         .schema == "logos.palace.basecamp-source-identity"
         and .version == 1
         and .productSnapshot == $snapshot
       )
       | .snapshotNarHash
       | select(type == "string")
       | select(test("^sha256-[A-Za-z0-9+/]{43}=$"))' \
      "${source_identity}"
  )"
  snapshot_nar_size="$(
    "${bootstrap_jq}" -er \
      '.snapshotNarSize
        | select(type == "number")
        | select(. > 0 and . <= 67108864 and floor == .)' \
      "${source_identity}"
  )"
  snapshot_runner_sha256="$(
    "${bootstrap_jq}" -er \
      '.snapshotRunnerSha256
        | select(type == "string")
        | select(test("^[0-9a-f]{64}$"))' \
      "${source_identity}"
  )"
  tracked_paths_sha256="$(
    "${bootstrap_jq}" -er \
      '.trackedPathsSha256
        | select(type == "string")
        | select(test("^[0-9a-f]{64}$"))' \
      "${source_identity}"
  )"
  snapshot_evidence_sha256="$(
    "${bootstrap_jq}" -er \
      '.snapshotEvidenceSha256
        | select(type == "string")
        | select(test("^[0-9a-f]{64}$"))' \
      "${source_identity}"
  )"
  if [ -L "${shared_state}" ] \
    || [ ! -d "${shared_state}" ] \
    || [ "$(stat -c '%a' "${shared_state}")" != "700" ]; then
    printf 'MVP resume shared state is missing or insecure\n' >&2
    exit 1
  fi
else
  mkdir "${shared_state}"
  chmod 700 "${shared_state}"
fi

write_failed_report() {
  local phase="$1"
  local message="$2"
  local temporary
  temporary="$(mktemp "${run_dir}/.compiled-mvp-report.XXXXXXXX")"
  "${jq_bin}" -n \
    --arg phase "${phase}" \
    --arg message "${message}" \
    --arg snapshot "${product_snapshot}" \
    '{
      schema: "logos.palace.basecamp-mvp-compiled-report",
      version: 1,
      status: "failed",
      fullMvp: "not-evaluated",
      productSnapshot:
        (if $snapshot == "" then null else $snapshot end),
      scope: {
        implementedGates:
          ["gate0", "gate1", "gate2", "gate3", "gate4", "gate5", "gate6"],
        pendingGates: []
      },
      failure: {
        phase: $phase,
        message: $message
      },
      gates: {
        gate0: {status: "unknown", report: null},
        gate1: {status: "unknown", report: "gate1/gate1-report.json"},
        gate2: {status: "unknown", report: "gate2/gate2-report.json"},
        gate3: {status: "unknown", report: "gate3/gate3-report.json"},
        gate4: {status: "unknown", report: "gate4/gate4-report.json"},
        gate5: {status: "unknown", report: "gate4/gate4-report.json"},
        gate6: {status: "unknown", report: "gate4/gate4-report.json"}
      }
    }' >"${temporary}"
  chmod 600 "${temporary}"
  mv -- "${temporary}" "${compiled_report}"
  printf 'Compiled MVP report: %s\n' "${compiled_report}" >&2
}

invalidate_public_evidence() {
  if [ -L "${public_evidence}" ] \
    || { [ -e "${public_evidence}" ] && [ ! -d "${public_evidence}" ]; }; then
    "${acceptance_tools}/bin/rm" -f -- "${public_evidence}"
    "${acceptance_tools}/bin/sync" -f "${run_dir}"
  fi
}

fail_run() {
  local phase="$1"
  local message="$2"
  local resume_target="${3:-${run_dir}}"
  invalidate_public_evidence
  if [ "${claim_completed}" -eq 0 ]; then
    write_failed_report "${phase}" "${message}"
  else
    printf 'Completed-claim report preserved: %s\n' \
      "${compiled_report}" >&2
  fi
  printf 'MVP run failed during %s: %s\n' "${phase}" "${message}" >&2
  printf 'Resume with: %q %q %q\n' \
    "${repo_root}/scripts/run-basecamp-mvp.sh" \
    "${runs_root}" \
    "${resume_target}" >&2
  exit 1
}

run_gate() {
  local gate="$1"
  shift
  local cleanup_result
  local cleanup_status
  local launch_cleanup_result
  local scope_attempt_file
  local scope_attempt_id
  local scope_attestation_status
  local scope_evidence="${run_dir}/${gate}/process-scope.json"
  local scope_history="${run_dir}/${gate}/process-scope-history"
  local scope_launch="${run_dir}/${gate}/process-scope-launch.json"
  local scope_transition_status
  local scope_unit
  local gate_status
  local recovery_result
  local recovery_status

  if [ -L "${scope_history}" ] \
    || { [ -e "${scope_history}" ] && [ ! -d "${scope_history}" ]; }; then
    fail_run "${gate}" \
      "process scope history path is not a secure directory"
  fi
  "${acceptance_tools}/bin/mkdir" -p -- "${scope_history}"
  "${acceptance_tools}/bin/chmod" 700 -- "${scope_history}"
  if [ "$("${acceptance_tools}/bin/realpath" -e -- "${scope_history}")" \
      != "${scope_history}" ] \
    || [ "$("${acceptance_tools}/bin/stat" -c '%u:%a' -- \
      "${scope_history}")" \
      != "$("${acceptance_tools}/bin/id" -u):700" ]; then
    fail_run "${gate}" \
      "process scope history is not an owner-only canonical directory"
  fi

  if [ -L "${scope_launch}" ] \
    || { [ -e "${scope_launch}" ] && [ ! -f "${scope_launch}" ]; }; then
    fail_run "${gate}" \
      "process scope launch evidence path is not a regular file"
  fi
  if [ -f "${scope_launch}" ]; then
    set +e
    launch_cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" recover \
        "${scope_launch}" "${systemctl_bin}"
    )"
    cleanup_status=$?
    set -e
    if [ "${cleanup_status}" -ne 0 ]; then
      fail_run "${gate}" \
        "prior planned process scope could not be recovered"
    fi
    if [ "${launch_cleanup_result}" != "clean" ] \
      && [ "${launch_cleanup_result}" != "residue-killed" ]; then
      fail_run "${gate}" \
        "prior planned process scope recovery result is invalid"
    fi
    if ! "${death_coupled_node[@]}" "${scope_control}" archive \
      "${scope_launch}" "${scope_history}" >/dev/null; then
      fail_run "${gate}" \
        "recovered process scope launch could not be archived"
    fi
  fi
  if [ -L "${scope_evidence}" ] \
    || { [ -e "${scope_evidence}" ] \
      && [ ! -f "${scope_evidence}" ]; }; then
    fail_run "${gate}" \
      "process scope evidence path is not a secure regular file"
  fi
  if [ -f "${scope_evidence}" ]; then
    set +e
    cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" cleanup \
        "${scope_evidence}"
    )"
    cleanup_status=$?
    set -e
    if [ "${cleanup_status}" -ne 0 ]; then
      fail_run "${gate}" \
        "prior attested process scope could not be cleaned"
    fi
    if [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; then
      fail_run "${gate}" \
        "prior attested process scope recovery result is invalid"
    fi
    if ! "${death_coupled_node[@]}" "${scope_control}" archive \
      "${scope_evidence}" "${scope_history}" >/dev/null; then
      fail_run "${gate}" \
        "recovered process scope evidence could not be archived"
    fi
  fi

  scope_attempt_file="$(
    "${acceptance_tools}/bin/mktemp" \
      "${run_dir}/${gate}/.process-scope-attempt.XXXXXXXX"
  )"
  scope_attempt_id="${scope_attempt_file##*.}"
  "${acceptance_tools}/bin/rm" -f -- "${scope_attempt_file}"
  if [[ ! "${scope_attempt_id}" =~ ^[A-Za-z0-9]{8}$ ]]; then
    fail_run "${gate}" "process scope attempt identity is invalid"
  fi
  scope_unit="${process_scope_prefix}-${gate}-${scope_attempt_id}.scope"
  "${death_coupled_node[@]}" "${scope_control}" plan \
    "${scope_unit}" "${process_scope_slice}" "${scope_launch}" \
    >/dev/null

  printf 'Running compiled Basecamp %s\n' "${gate}"
  active_gate_pid=""
  active_scope_evidence="${scope_evidence}"
  active_scope_launch="${scope_launch}"
  active_scope_attested=0
  "${systemd_run}" \
    --user \
    --scope \
    --quiet \
    --collect \
    --expand-environment=no \
    --slice="${process_scope_slice}" \
    --unit="${scope_unit}" \
    --property=KillMode=control-group \
    "${acceptance_tools}/bin/setpriv" \
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
      "${process_scope_slice}" \
      "${acceptance_tools}/bin/setsid" --wait \
      "${acceptance_tools}/bin/env" \
        "PALACE_MVP_PROCESS_SCOPE_PREFIX=${process_scope_prefix}" \
        "PALACE_MVP_PROCESS_SCOPE_SLICE=${process_scope_slice}" \
        "PALACE_MVP_PROCESS_SCOPE_UNIT=${scope_unit}" \
      "$@" &
  active_gate_pid=$!
  set +e
  "${death_coupled_node[@]}" "${scope_control}" attest \
    "${active_gate_pid}" \
    "${scope_unit}" \
    "${process_scope_slice}" \
    "${scope_evidence}" \
    "${systemctl_bin}" >/dev/null
  scope_attestation_status=$?
  set -e
  if [ "${scope_attestation_status}" -ne 0 ]; then
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
        'Gate %s scope attestation and exact recovery failed\n' \
        "${gate}" >&2
      exit 1
    fi
    fail_run "${gate}" "process scope attestation failed"
  fi
  active_scope_attested=1
  set +e
  "${death_coupled_node[@]}" "${scope_control}" finish-launch \
    "${scope_launch}" "${scope_evidence}" >/dev/null
  scope_transition_status=$?
  set -e
  if [ "${scope_transition_status}" -ne 0 ]; then
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
        'Gate %s scope transition and exact cleanup failed\n' \
        "${gate}" >&2
      exit 1
    fi
    fail_run "${gate}" "process scope launch transition failed"
  fi
  active_scope_launch=""
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
        'Gate %s scope release and exact cleanup failed\n' \
        "${gate}" >&2
      exit 1
    fi
    fail_run "${gate}" "process scope barrier release failed"
  fi
  set +e
  wait "${active_gate_pid}"
  gate_status=$?
  cleanup_result="$(
    "${death_coupled_node[@]}" "${scope_control}" cleanup \
      "${scope_evidence}" "${gate_status}"
  )"
  cleanup_status=$?
  set -e
  if [ "${cleanup_status}" -ne 0 ] \
    || { [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; }; then
    printf \
      'Gate %s post-command exact scope cleanup failed\n' \
      "${gate}" >&2
    exit 1
  fi
  active_gate_pid=""
  active_scope_evidence=""
  active_scope_launch=""
  active_scope_attested=0
  if [ "${gate_status}" -ne 0 ]; then
    fail_run "${gate}" "gate command exited with status ${gate_status}"
  fi
  if [ "${cleanup_result}" != "clean" ]; then
    fail_run "${gate}" \
      "gate retained processes that required cgroup.kill"
  fi
}

active_gate_pid=""
active_scope_attested=0
active_scope_evidence=""
active_scope_launch=""

handle_runner_signal() {
  local signal_name="$1"
  local cleanup_result
  local cleanup_status
  trap - HUP INT TERM
  if [ "${active_scope_attested}" -eq 1 ] \
    && [ -n "${active_scope_evidence}" ]; then
    set +e
    cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" cleanup \
        "${active_scope_evidence}" 255
    )"
    cleanup_status=$?
    set -e
  elif [ -n "${active_scope_launch}" ]; then
    set +e
    cleanup_result="$(
      "${death_coupled_node[@]}" "${scope_control}" recover \
        "${active_scope_launch}" "${systemctl_bin}"
    )"
    cleanup_status=$?
    set -e
  else
    cleanup_result="clean"
    cleanup_status=0
  fi
  if [ "${cleanup_status}" -ne 0 ] \
    || { [ "${cleanup_result}" != "clean" ] \
      && [ "${cleanup_result}" != "residue-killed" ]; }; then
    printf \
      'Runner interrupted by %s; exact scope cleanup failed\n' \
      "${signal_name}" >&2
    exit 1
  fi
  active_gate_pid=""
  active_scope_attested=0
  active_scope_evidence=""
  active_scope_launch=""
  fail_run "signal" "runner interrupted by ${signal_name}"
}

printf 'MVP run directory: %s\n' "${run_dir}"

if [ "${resuming}" -eq 0 ]; then
  product_snapshot="$(
    nix flake archive --json "${bootstrap_product_ref}" |
      "${bootstrap_jq}" -er '.path | select(type == "string")'
  )"
  if ! valid_product_snapshot "${product_snapshot}"; then
    printf 'Could not archive one canonical immutable product source\n' >&2
    exit 1
  fi
  temporary_marker="$(
    mktemp "${run_dir}/.product-snapshot.XXXXXXXX"
  )"
  printf '%s\n' "${product_snapshot}" >"${temporary_marker}"
  chmod 600 "${temporary_marker}"
  "${bootstrap_tools}/bin/sync" -f "${temporary_marker}"
  mv -- "${temporary_marker}" "${snapshot_marker}"
  "${bootstrap_tools}/bin/sync" -f "${run_dir}"
  if [ -e "${snapshot_gc_root}" ] || [ -L "${snapshot_gc_root}" ]; then
    printf 'MVP snapshot GC root path already exists\n' >&2
    exit 1
  fi
  nix-store --add-root "${snapshot_gc_root}" --indirect \
    -r "${product_snapshot}" >/dev/null
  if [ ! -L "${snapshot_gc_root}" ] \
    || [ "$(realpath -e -- "${snapshot_gc_root}" 2>/dev/null || true)" \
      != "${product_snapshot}" ]; then
    printf 'Could not create exact per-run snapshot GC root\n' >&2
    exit 1
  fi
  "${bootstrap_tools}/bin/sync" -f "${durable_roots_dir}"
  snapshot_info="$(
    nix path-info --json --json-format 1 "${product_snapshot}"
  )"
  snapshot_nar_hash="$(
    "${bootstrap_jq}" -er \
      --arg snapshot "${product_snapshot}" \
      '.[$snapshot].narHash
        | select(type == "string")
        | select(test("^sha256-[A-Za-z0-9+/]{43}=$"))' \
      <<<"${snapshot_info}"
  )"
  snapshot_nar_size="$(
    "${bootstrap_jq}" -er \
      --arg snapshot "${product_snapshot}" \
      '.[$snapshot].narSize
        | select(type == "number")
        | select(. > 0 and . <= 67108864 and floor == .)' \
      <<<"${snapshot_info}"
  )"

  temporary_tracked_paths="$(
    mktemp "${run_dir}/.source-tree-paths.XXXXXXXX"
  )"
  git -C "${repo_root}" ls-tree -r -z --name-only \
    "${source_commit}" >"${temporary_tracked_paths}"
  chmod 600 "${temporary_tracked_paths}"
  "${bootstrap_tools}/bin/sync" -f "${temporary_tracked_paths}"
  mv -- "${temporary_tracked_paths}" "${tracked_paths}"

  temporary_snapshot_evidence="$(
    mktemp "${run_dir}/.product-snapshot-evidence.XXXXXXXX"
  )"
  "${bootstrap_tools}/bin/node" \
    "${product_snapshot}/tests/validate_product_snapshot.mjs" \
    "${product_snapshot}" "${tracked_paths}" \
    >"${temporary_snapshot_evidence}"
  chmod 600 "${temporary_snapshot_evidence}"
  "${bootstrap_jq}" -e \
    --arg snapshot "${product_snapshot}" \
    'select(
       .schema == "logos.palace.product-snapshot-evidence"
       and .version == 1
       and .productSnapshot == $snapshot
       and (.fileCount | type) == "number"
       and .fileCount > 0
       and .fileCount <= 4096
       and (.totalBytes | type) == "number"
       and .totalBytes > 0
       and .totalBytes <= 67108864
       and (.treeSha256 | type) == "string"
       and (.treeSha256 | test("^[0-9a-f]{64}$"))
       and .exactTrackedFileSet == true
       and .forbiddenEntriesAbsent == true
       and .symlinksAbsent == true
     )' "${temporary_snapshot_evidence}" >/dev/null
  "${bootstrap_tools}/bin/sync" -f "${temporary_snapshot_evidence}"
  mv -- "${temporary_snapshot_evidence}" "${snapshot_evidence}"
  "${bootstrap_tools}/bin/sync" -f "${run_dir}"

  tracked_paths_output="$("${bootstrap_sha256}" -- "${tracked_paths}")"
  tracked_paths_sha256="${tracked_paths_output%% *}"
  snapshot_evidence_output="$(
    "${bootstrap_sha256}" -- "${snapshot_evidence}"
  )"
  snapshot_evidence_sha256="${snapshot_evidence_output%% *}"
  snapshot_runner="${product_snapshot}/scripts/run-basecamp-mvp.sh"
  snapshot_runner_output="$("${bootstrap_sha256}" -- "${snapshot_runner}")"
  snapshot_runner_sha256="${snapshot_runner_output%% *}"
  if [[ ! "${tracked_paths_sha256}" =~ ^[0-9a-f]{64}$ ]] \
    || [[ ! "${snapshot_evidence_sha256}" =~ ^[0-9a-f]{64}$ ]] \
    || [[ ! "${snapshot_runner_sha256}" =~ ^[0-9a-f]{64}$ ]]; then
    printf 'MVP snapshot evidence digest is invalid\n' >&2
    exit 1
  fi

  temporary_identity="$(
    mktemp "${run_dir}/.source-identity.XXXXXXXX"
  )"
  "${bootstrap_jq}" -n \
    --arg commit "${source_commit}" \
    --arg snapshot "${product_snapshot}" \
    --arg nar_hash "${snapshot_nar_hash}" \
    --argjson nar_size "${snapshot_nar_size}" \
    --arg runner_sha256 "${snapshot_runner_sha256}" \
    --arg tracked_paths_sha256 "${tracked_paths_sha256}" \
    --arg evidence_sha256 "${snapshot_evidence_sha256}" \
    --arg gc_root "${snapshot_gc_root}" \
    '{
      schema: "logos.palace.basecamp-source-identity",
      version: 1,
      gitCommit: $commit,
      productSnapshot: $snapshot,
      snapshotNarHash: $nar_hash,
      snapshotNarSize: $nar_size,
      snapshotRunnerSha256: $runner_sha256,
      trackedPathsSha256: $tracked_paths_sha256,
      snapshotEvidenceSha256: $evidence_sha256,
      snapshotGcRoot: $gc_root
    }' >"${temporary_identity}"
  chmod 600 "${temporary_identity}"
  "${bootstrap_tools}/bin/sync" -f "${temporary_identity}"
  mv -- "${temporary_identity}" "${source_identity}"
  "${bootstrap_tools}/bin/sync" -f "${run_dir}"
fi
actual_snapshot_info="$(
  nix path-info --json --json-format 1 "${product_snapshot}"
)"
actual_snapshot_nar_hash="$(
  "${bootstrap_jq}" -er --arg snapshot "${product_snapshot}" \
    '.[$snapshot].narHash' <<<"${actual_snapshot_info}"
)"
actual_snapshot_nar_size="$(
  "${bootstrap_jq}" -er --arg snapshot "${product_snapshot}" \
    '.[$snapshot].narSize' <<<"${actual_snapshot_info}"
)"
snapshot_runner="${product_snapshot}/scripts/run-basecamp-mvp.sh"
actual_snapshot_runner_output="$(
  "${bootstrap_sha256}" -- "${snapshot_runner}"
)"
actual_snapshot_runner_sha256="${actual_snapshot_runner_output%% *}"
if [ "${actual_snapshot_nar_hash}" != "${snapshot_nar_hash}" ] \
  || [ "${actual_snapshot_nar_size}" != "${snapshot_nar_size}" ] \
  || [ "${actual_snapshot_runner_sha256}" \
    != "${snapshot_runner_sha256}" ]; then
  printf 'MVP snapshot identity differs from durable source identity\n' >&2
  exit 1
fi

for evidence_file in "${tracked_paths}" "${snapshot_evidence}"; do
  if [ -L "${evidence_file}" ] \
    || [ ! -f "${evidence_file}" ] \
    || [ "$(dirname "${evidence_file}")" != "${run_dir}" ] \
    || [ "$(stat -c '%a' "${evidence_file}")" != "600" ]; then
    printf 'MVP source evidence file is missing or insecure\n' >&2
    exit 1
  fi
done
actual_tracked_paths_output="$("${bootstrap_sha256}" -- "${tracked_paths}")"
actual_snapshot_evidence_output="$(
  "${bootstrap_sha256}" -- "${snapshot_evidence}"
)"
if [ "${actual_tracked_paths_output%% *}" != "${tracked_paths_sha256}" ] \
  || [ "${actual_snapshot_evidence_output%% *}" \
    != "${snapshot_evidence_sha256}" ]; then
  printf 'MVP source evidence digest differs from durable identity\n' >&2
  exit 1
fi
regenerated_snapshot_evidence="$(
  "${bootstrap_tools}/bin/node" \
    "${product_snapshot}/tests/validate_product_snapshot.mjs" \
    "${product_snapshot}" "${tracked_paths}" \
    | "${bootstrap_jq}" -cS .
)"
stored_snapshot_evidence="$(
  "${bootstrap_jq}" -cS . "${snapshot_evidence}"
)"
if [ "${regenerated_snapshot_evidence}" != "${stored_snapshot_evidence}" ]; then
  printf 'MVP snapshot no longer matches tracked source evidence\n' >&2
  exit 1
fi
if [ ! -L "${snapshot_gc_root}" ] \
  || [ "$(realpath -e -- "${snapshot_gc_root}" 2>/dev/null || true)" \
    != "${product_snapshot}" ]; then
  printf 'MVP durable snapshot GC root is missing or mismatched\n' >&2
  exit 1
fi
if ! "${bootstrap_jq}" -e \
  --arg commit "${source_commit}" \
  --arg snapshot "${product_snapshot}" \
  --arg nar_hash "${snapshot_nar_hash}" \
  --argjson nar_size "${snapshot_nar_size}" \
  --arg runner_sha256 "${snapshot_runner_sha256}" \
  --arg tracked_sha256 "${tracked_paths_sha256}" \
  --arg evidence_sha256 "${snapshot_evidence_sha256}" \
  --arg gc_root "${snapshot_gc_root}" \
  'select(
     (keys | sort) == ([
       "gitCommit",
       "productSnapshot",
       "schema",
       "snapshotEvidenceSha256",
       "snapshotGcRoot",
       "snapshotNarHash",
       "snapshotNarSize",
       "snapshotRunnerSha256",
       "trackedPathsSha256",
       "version"
     ] | sort)
     and .schema == "logos.palace.basecamp-source-identity"
     and .version == 1
     and .gitCommit == $commit
     and .productSnapshot == $snapshot
     and .snapshotNarHash == $nar_hash
     and .snapshotNarSize == $nar_size
     and .snapshotRunnerSha256 == $runner_sha256
     and .trackedPathsSha256 == $tracked_sha256
     and .snapshotEvidenceSha256 == $evidence_sha256
     and .snapshotGcRoot == $gc_root
   )' "${source_identity}" >/dev/null; then
  printf 'MVP durable source identity failed exact validation\n' >&2
  exit 1
fi

invoked_runner="$(
  "${bootstrap_tools}/bin/realpath" -e -- "${BASH_SOURCE[0]}"
)"
if [ "${immutable_runner}" = "0" ]; then
  export PALACE_MVP_IMMUTABLE_RUNNER=1
  export PALACE_MVP_RUNNER_SHA256="${snapshot_runner_sha256}"
  export PALACE_MVP_LOCK_SUPERVISED=1
  export PALACE_MVP_LOCK_PATH="${mvp_lock_path}"
  exec "${bootstrap_flock}" \
    --exclusive \
    --nonblock \
    --conflict-exit-code 75 \
    --close \
    -- \
    "${mvp_lock_path}" \
    "${bootstrap_setpriv}" --pdeathsig KILL \
    "${bootstrap_bash}" -p \
    "${snapshot_runner}" "${runs_root}" "${run_dir}"
fi
if [ "${invoked_runner}" != "${snapshot_runner}" ] \
  || [ "${PALACE_MVP_RUNNER_SHA256:-}" != "${snapshot_runner_sha256}" ]; then
  printf 'MVP gates must execute from exact immutable snapshot runner\n' >&2
  exit 1
fi
lock_attestation="$(
  "${bootstrap_tools}/bin/node" \
    "${product_snapshot}/tests/basecamp_release_lock.mjs" \
    attest-parent \
    "${mvp_lock_path}" \
    "${bootstrap_flock}" \
    "${snapshot_runner}" \
    "${runs_root}" \
    "${run_dir}" \
    "$$"
)"
lock_supervisor_pid="$(
  "${bootstrap_jq}" -er \
    'select(
       .schema == "logos.palace.release-lock-attestation"
       and .version == 1
     )
     | .supervisorPid
     | select(type == "number" and . > 1 and floor == .)' \
    <<<"${lock_attestation}"
)"
lock_supervisor_start_time="$(
  "${bootstrap_jq}" -er \
    '.supervisorStartTimeTicks
      | select(type == "number" and . > 0 and floor == .)' \
    <<<"${lock_attestation}"
)"
export PALACE_MVP_LOCK_SUPERVISOR_PID="${lock_supervisor_pid}"
export PALACE_MVP_LOCK_SUPERVISOR_START_TIME_TICKS="${lock_supervisor_start_time}"
bootstrap_death_coupled_node=(
  "${bootstrap_setpriv}"
  --pdeathsig
  KILL
  "${bootstrap_bash}"
  -p
  -c
  'expected_parent="$1"
   shift
   if [ "$PPID" != "$expected_parent" ]; then
     exit 125
   fi
   exec "$@"'
  palace-bootstrap-death-coupled-node
  "$$"
  "${bootstrap_tools}/bin/node"
)
active_scope_preflight="${product_snapshot}/tests/basecamp_active_scope_preflight.mjs"
if [ -L "${active_scope_preflight}" ] \
  || [ ! -f "${active_scope_preflight}" ]; then
  printf 'Immutable active-scope preflight is missing\n' >&2
  exit 1
fi
scope_preflight_result="$(
  "${bootstrap_death_coupled_node[@]}" \
    "${active_scope_preflight}" \
    retire-before-release \
    "${snapshot_runner}" \
    "${runs_root}" \
    "${run_dir}" \
    "${bootstrap_systemctl}"
)"
case "${scope_preflight_result}" in
  no-claim|legacy-clean|retired-clean|residue-killed)
    ;;
  *)
    printf 'Active-scope preflight returned an invalid result\n' >&2
    exit 1
    ;;
esac
if [ "${resuming}" -eq 1 ]; then
  if [ -d "${public_evidence}" ] && [ ! -L "${public_evidence}" ]; then
    printf 'MVP public evidence path is an unsafe directory\n' >&2
    exit 1
  fi
  if [ -e "${public_evidence}" ] || [ -L "${public_evidence}" ]; then
    "${bootstrap_tools}/bin/unlink" -- "${public_evidence}"
    "${bootstrap_tools}/bin/sync" -f "${run_dir}"
  fi
fi
printf 'MVP product snapshot: %s\n' "${product_snapshot}"

product_ref="path:${product_snapshot}"
acceptance_tools="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#acceptance-tools"
)"
if ! valid_store_path "${acceptance_tools}"; then
  printf 'Acceptance tools output is not one canonical Nix store path\n' >&2
  exit 1
fi
jq_bin="${acceptance_tools}/bin/jq"
sha256_bin="${acceptance_tools}/bin/sha256sum"
systemd_run="${acceptance_tools}/bin/systemd-run"
systemctl_bin="${acceptance_tools}/bin/systemctl"
scope_control="${product_snapshot}/tests/basecamp_scope_control.mjs"
scope_guardian="${product_snapshot}/tests/basecamp_scope_guardian.mjs"
if [ ! -x "${jq_bin}" ] \
  || [ ! -x "${sha256_bin}" ] \
  || [ ! -x "${systemd_run}" ] \
  || [ ! -x "${systemctl_bin}" ] \
  || [ ! -x "${acceptance_tools}/bin/node" ] \
  || [ ! -x "${acceptance_tools}/bin/setpriv" ] \
  || [ ! -x "${acceptance_tools}/bin/setsid" ] \
  || [ -L "${scope_control}" ] \
  || [ ! -f "${scope_control}" ] \
  || [ -L "${scope_guardian}" ] \
  || [ ! -f "${scope_guardian}" ]; then
  printf 'Pinned acceptance tools are incomplete\n' >&2
  exit 1
fi
death_coupled_node=(
  "${acceptance_tools}/bin/setpriv"
  --pdeathsig
  KILL
  "${acceptance_tools}/bin/bash"
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
  printf 'Compiled MVP requires a running user manager and cgroup v2\n' >&2
  exit 1
fi
release_artifact="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-release-artifact"
)"
if ! valid_store_path "${release_artifact}" \
  || [ ! -x "${release_artifact}/bin/palace-image-id" ] \
  || [ ! -f "${release_artifact}/bin/palace-image-id" ] \
  || [ -L "${release_artifact}/bin/palace-image-id" ] \
  || [ ! -f "${release_artifact}/share/logos-palace/release.json" ] \
  || [ -L "${release_artifact}/share/logos-palace/release.json" ] \
  || [ -e "${release_artifact}/share/logos-palace/palace.bin" ] \
  || [ -L "${release_artifact}/share/logos-palace/palace.bin" ]; then
  printf 'Pinned Palace release artifact is incomplete\n' >&2
  exit 1
fi
export PALACE_RELEASE_ARTIFACT="${release_artifact}"

lock_file="${product_snapshot}/flake.lock"
basecamp_owner="$("${jq_bin}" -er '.nodes.basecamp.locked.owner' "${lock_file}")"
basecamp_repo="$("${jq_bin}" -er '.nodes.basecamp.locked.repo' "${lock_file}")"
basecamp_rev="$("${jq_bin}" -er '.nodes.basecamp.locked.rev' "${lock_file}")"
basecamp_ref="github:${basecamp_owner}/${basecamp_repo}/${basecamp_rev}"
dependency_revisions="$(
  "${jq_bin}" -c \
    '{
      basecamp: {
        revision: .nodes.basecamp.locked.rev,
        narHash: .nodes.basecamp.locked.narHash
      },
      delivery_module: {
        revision: .nodes.delivery_module.locked.rev,
        narHash: .nodes.delivery_module.locked.narHash
      },
      storage_module: {
        revision: .nodes.storage_module.locked.rev,
        narHash: .nodes.storage_module.locked.narHash
      },
      lez_core: {
        revision: .nodes.lez_core.locked.rev,
        narHash: .nodes.lez_core.locked.narHash
      }
    }' "${lock_file}"
)"

build_one_output() {
  local reference="$1"
  local output_text
  local -a outputs
  output_text="$(
    nix build --no-link --print-out-paths "${reference}"
  )"
  mapfile -t outputs <<<"${output_text}"
  if [ "${#outputs[@]}" -ne 1 ] \
    || ! valid_store_path "${outputs[0]}"; then
    printf 'Runtime build did not produce one canonical store path\n' >&2
    return 1
  fi
  printf '%s\n' "${outputs[0]}"
}

palace_vm_lgx="$(
  build_one_output "${product_ref}#palace-vm-lgx-portable"
)"
palace_core_lgx="$(
  build_one_output "${product_ref}#palace-core-lgx-portable"
)"
palace_core_acceptance_lgx="$(
  build_one_output \
    "${product_ref}#palace-core-acceptance-lgx-portable"
)"
palace_ui_lgx="$(
  build_one_output "${product_ref}#logos-palace-ui-lgx-portable"
)"
delivery_lgx="$(
  build_one_output "${product_ref}#delivery-module-lgx-portable"
)"
storage_lgx="$(
  build_one_output "${product_ref}#storage-module-lgx-portable"
)"
lez_lgx="$(
  build_one_output "${product_ref}#lez-core-lgx-portable"
)"
basecamp_bundle="$(
  build_one_output "${basecamp_ref}#bin-bundle-dir-inspector"
)"
qt_mcp="$(
  build_one_output "${basecamp_ref}#logos-qt-mcp"
)"
runtime_system="$(
  nix eval --impure --raw --expr builtins.currentSystem
)"
if [[ ! "${runtime_system}" =~ ^[A-Za-z0-9_+-]+-[A-Za-z0-9_+-]+$ ]]; then
  printf 'Runtime Nix system identity is invalid\n' >&2
  exit 1
fi
palace_core_contracts="$(
  build_one_output \
    "${product_ref}#checks.${runtime_system}.palace-core-contracts"
)"
palace_vm_contracts="$(
  build_one_output \
    "${product_ref}#checks.${runtime_system}.palace-vm-contracts"
)"

runtime_entries="$(
  mktemp "${run_dir}/.runtime-output-entries.XXXXXXXX"
)"
root_runtime_output() {
  local name="$1"
  local target="$2"
  local root="${runtime_gc_roots}/${name}"
  local info
  local nar_hash
  local nar_size
  if [[ ! "${name}" =~ ^[a-z][a-z0-9-]{0,63}$ ]] \
    || ! valid_store_path "${target}"; then
    printf 'Runtime output name or target is invalid\n' >&2
    return 1
  fi
  if [ -e "${root}" ] || [ -L "${root}" ]; then
    if [ ! -L "${root}" ] \
      || [ "$(realpath -e -- "${root}" 2>/dev/null || true)" \
        != "${target}" ]; then
      printf 'Runtime GC root differs from exact output\n' >&2
      return 1
    fi
  else
    nix-store --add-root "${root}" --indirect -r "${target}" >/dev/null
  fi
  if [ ! -L "${root}" ] \
    || [ "$(realpath -e -- "${root}" 2>/dev/null || true)" \
      != "${target}" ]; then
    printf 'Runtime GC root did not retain exact output\n' >&2
    return 1
  fi
  info="$(nix path-info --json --json-format 1 "${target}")"
  nar_hash="$(
    "${jq_bin}" -er --arg target "${target}" \
      '.[$target].narHash
        | select(type == "string")
        | select(test("^sha256-[A-Za-z0-9+/]{43}=$"))' \
      <<<"${info}"
  )"
  nar_size="$(
    "${jq_bin}" -er --arg target "${target}" \
      '.[$target].narSize
        | select(type == "number")
        | select(. > 0 and . <= 9007199254740991 and floor == .)' \
      <<<"${info}"
  )"
  printf '%s\t%s\t%s\t%s\t%s\n' \
    "${name}" "${target}" "${root}" "${nar_hash}" "${nar_size}" \
    >>"${runtime_entries}"
}

root_runtime_output "acceptance-tools" "${acceptance_tools}"
root_runtime_output "release-verifier" "${release_artifact}"
root_runtime_output "palace-core-contracts" "${palace_core_contracts}"
root_runtime_output "palace-vm-contracts" "${palace_vm_contracts}"
root_runtime_output "palace-vm-lgx" "${palace_vm_lgx}"
root_runtime_output \
  "palace-core-acceptance-lgx" \
  "${palace_core_acceptance_lgx}"
root_runtime_output "palace-core-lgx" "${palace_core_lgx}"
root_runtime_output "palace-ui-lgx" "${palace_ui_lgx}"
root_runtime_output "delivery-module-lgx" "${delivery_lgx}"
root_runtime_output "storage-module-lgx" "${storage_lgx}"
root_runtime_output "lez-core-lgx" "${lez_lgx}"
root_runtime_output "basecamp" "${basecamp_bundle}"
root_runtime_output "qt-mcp" "${qt_mcp}"
"${acceptance_tools}/bin/sync" -f "${runtime_gc_roots}"

generated_runtime_manifest="$(
  mktemp "${run_dir}/.runtime-output-manifest.XXXXXXXX"
)"
"${jq_bin}" -Rn \
  '[
     inputs
     | split("\t")
     | select(length == 5)
     | {
         name: .[0],
         narHash: .[3],
         narSize: (.[4] | tonumber)
       }
   ]
   | sort_by(.name)
   | select(length == 13)
   | select((map(.name) | unique | length) == 13)
   | {
       schema: "logos.palace.runtime-output-manifest",
       version: 1,
       outputs: .
     }' <"${runtime_entries}" >"${generated_runtime_manifest}"
chmod 600 "${generated_runtime_manifest}"
if ! "${jq_bin}" -e \
  'select(
     (keys | sort) == (["outputs", "schema", "version"] | sort)
     and .schema == "logos.palace.runtime-output-manifest"
     and .version == 1
     and (.outputs | type) == "array"
     and (.outputs | map(.name)) == [
       "acceptance-tools",
       "basecamp",
       "delivery-module-lgx",
       "lez-core-lgx",
       "palace-core-acceptance-lgx",
       "palace-core-contracts",
       "palace-core-lgx",
       "palace-ui-lgx",
       "palace-vm-contracts",
       "palace-vm-lgx",
       "qt-mcp",
       "release-verifier",
       "storage-module-lgx"
     ]
     and all(
       .outputs[];
       (keys | sort) == (["name", "narHash", "narSize"] | sort)
         and (.name | type) == "string"
         and (.narHash | type) == "string"
         and (.narHash | test("^sha256-[A-Za-z0-9+/]{43}=$"))
         and (.narSize | type) == "number"
         and .narSize > 0
         and .narSize <= 9007199254740991
         and (.narSize | floor) == .narSize
     )
   )' "${generated_runtime_manifest}" >/dev/null; then
  printf 'Runtime output manifest is not exact or path-free\n' >&2
  exit 1
fi
"${acceptance_tools}/bin/sync" -f "${generated_runtime_manifest}"
if [ -e "${runtime_manifest}" ] || [ -L "${runtime_manifest}" ]; then
  if [ -L "${runtime_manifest}" ] \
    || [ ! -f "${runtime_manifest}" ] \
    || [ "$(stat -c '%a' "${runtime_manifest}")" != "600" ] \
    || ! "${acceptance_tools}/bin/cmp" -s \
      "${generated_runtime_manifest}" "${runtime_manifest}"; then
    printf 'Runtime output manifest differs on resume\n' >&2
    exit 1
  fi
  "${acceptance_tools}/bin/rm" -- "${generated_runtime_manifest}"
else
  mv -- "${generated_runtime_manifest}" "${runtime_manifest}"
  "${acceptance_tools}/bin/sync" -f "${run_dir}"
fi
"${acceptance_tools}/bin/rm" -- "${runtime_entries}"
runtime_manifest_output="$("${sha256_bin}" -- "${runtime_manifest}")"
runtime_manifest_sha256="${runtime_manifest_output%% *}"
if [[ ! "${runtime_manifest_sha256}" =~ ^[0-9a-f]{64}$ ]]; then
  printf 'Runtime output manifest digest is invalid\n' >&2
  exit 1
fi
release_verifier_nar_hash="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "release-verifier")
      | .narHash' "${runtime_manifest}"
)"
release_verifier_nar_size="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "release-verifier")
      | .narSize' "${runtime_manifest}"
)"
palace_core_contracts_nar_hash="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "palace-core-contracts")
      | .narHash' "${runtime_manifest}"
)"
palace_core_contracts_nar_size="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "palace-core-contracts")
      | .narSize' "${runtime_manifest}"
)"
palace_vm_contracts_nar_hash="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "palace-vm-contracts")
      | .narHash' "${runtime_manifest}"
)"
palace_vm_contracts_nar_size="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "palace-vm-contracts")
      | .narSize' "${runtime_manifest}"
)"
basecamp_nar_hash="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "basecamp")
      | .narHash' "${runtime_manifest}"
)"
basecamp_nar_size="$(
  "${jq_bin}" -er \
    '.outputs[]
      | select(.name == "basecamp")
      | .narSize' "${runtime_manifest}"
)"
accepted_submission_contract_test_terminal="terminal_recovery_states_require_a_nonfinal_durable_action"
accepted_submission_contract_test_repair="core_lez_repairs_durable_coordinator_after_accept_to_journal_crash"
accepted_submission_contract_source_terminal="${product_snapshot}/packages/palace_core/tests/test_action_journal.cpp"
accepted_submission_contract_source_repair="${product_snapshot}/packages/palace_core/tests/test_lez_coordinator_store.cpp"
if [ -L "${accepted_submission_contract_source_terminal}" ] \
  || [ ! -f "${accepted_submission_contract_source_terminal}" ] \
  || [ -L "${accepted_submission_contract_source_repair}" ] \
  || [ ! -f "${accepted_submission_contract_source_repair}" ] \
  || ! "${death_coupled_node[@]}" -e \
    '
      const { readFileSync } = require("node:fs");
      for (let index = 1; index < process.argv.length; index += 2) {
        const source = readFileSync(process.argv[index], "utf8");
        const test = process.argv[index + 1];
        const pattern = new RegExp(`LOGOS_TEST\\(\\s*${test}\\s*\\)`, "g");
        if ([...source.matchAll(pattern)].length !== 1) process.exit(1);
      }
    ' \
    "${accepted_submission_contract_source_terminal}" \
    "${accepted_submission_contract_test_terminal}" \
    "${accepted_submission_contract_source_repair}" \
    "${accepted_submission_contract_test_repair}"; then
  printf 'Accepted-submit crash recovery tests are not exact in immutable source\n' >&2
  exit 1
fi
cold_replay_contract_test_builds="core_vm_finalized_replay_builds_one_exact_idempotent_plan"
cold_replay_contract_test_fails_closed="core_vm_finalized_replay_fails_closed_without_exact_evidence"
cold_replay_contract_source="${product_snapshot}/packages/palace_core/tests/test_core_vm_recovery.cpp"
if [ -L "${cold_replay_contract_source}" ] \
  || [ ! -f "${cold_replay_contract_source}" ] \
  || ! "${death_coupled_node[@]}" -e \
    '
      const { readFileSync } = require("node:fs");
      const source = readFileSync(process.argv[1], "utf8");
      for (const test of process.argv.slice(2)) {
        const marker = `LOGOS_TEST(${test})`;
        if (source.split(marker).length !== 2) process.exit(1);
      }
    ' \
    "${cold_replay_contract_source}" \
    "${cold_replay_contract_test_builds}" \
    "${cold_replay_contract_test_fails_closed}"; then
  printf 'Cold replay contract tests are not exact in immutable source\n' >&2
  exit 1
fi
palace_vm_contract_test_navigation="shared_door_navigation_emits_only_after_finalized_replay"
palace_vm_contract_test_rejection="tracked_finality_rejects_wrong_action_receipt_script_and_state"
palace_vm_contract_test_idempotence="exact_finality_promotion_is_one_shot_and_restart_safe"
palace_vm_contract_test_compatibility="untracked_finalized_compatibility_api_cannot_navigate"
palace_vm_contract_source="${product_snapshot}/packages/palace_vm/tests/test_vm_contract.cpp"
if [ -L "${palace_vm_contract_source}" ] \
  || [ ! -f "${palace_vm_contract_source}" ] \
  || ! "${death_coupled_node[@]}" -e \
    '
      const { readFileSync } = require("node:fs");
      const source = readFileSync(process.argv[1], "utf8");
      for (const test of process.argv.slice(2)) {
        const marker = `LOGOS_TEST(${test})`;
        if (source.split(marker).length !== 2) process.exit(1);
      }
    ' \
    "${palace_vm_contract_source}" \
    "${palace_vm_contract_test_navigation}" \
    "${palace_vm_contract_test_rejection}" \
    "${palace_vm_contract_test_idempotence}" \
    "${palace_vm_contract_test_compatibility}"; then
  printf 'Palace VM finality contract tests are not exact in immutable source\n' >&2
  exit 1
fi

claim_tool="${product_snapshot}/tests/basecamp_active_run_claim.mjs"
set +e
active_claim_path="$(
  "${death_coupled_node[@]}" "${claim_tool}" \
    acquire-or-roll-forward \
    "${run_dir}" "${product_snapshot}" "${snapshot_gc_root}" \
    "${source_commit}" "${snapshot_nar_hash}" "${snapshot_nar_size}" \
    "${snapshot_runner_sha256}" "${runtime_manifest}" \
    "${runtime_manifest_sha256}" \
    "${process_scope_slice}" "${process_scope_prefix}"
)"
claim_status=$?
set -e
if [ "${claim_status}" -ne 0 ]; then
  fail_run "active-run-claim" \
    "another run owns this release or the durable claim is invalid"
fi
set +e
claim_state="$(
  "${death_coupled_node[@]}" "${claim_tool}" state \
    "${run_dir}" "${product_snapshot}" "${snapshot_gc_root}" \
    "${source_commit}" "${snapshot_nar_hash}" "${snapshot_nar_size}" \
    "${snapshot_runner_sha256}" "${runtime_manifest}" \
    "${runtime_manifest_sha256}" \
    "${process_scope_slice}" "${process_scope_prefix}"
)"
claim_state_status=$?
set -e
if [ "${claim_state_status}" -ne 0 ]; then
  invalidate_public_evidence
  printf 'MVP active-run claim state could not be verified\n' >&2
  exit 1
fi
invalidate_public_evidence
case "${claim_state}" in
  active-pre-gate3|gate3-entered)
    claim_completed=0
    ;;
  completed)
    claim_completed=1
    ;;
  *)
    invalidate_public_evidence
    printf 'MVP active-run claim returned an invalid state\n' >&2
    exit 1
    ;;
esac
export PALACE_MVP_CLAIM_PATH="${active_claim_path}"
export PALACE_SOURCE_COMMIT="${source_commit}"
export PALACE_PRODUCT_SNAPSHOT_NAR_HASH="${snapshot_nar_hash}"
export PALACE_PRODUCT_SNAPSHOT_NAR_SIZE="${snapshot_nar_size}"
export PALACE_MVP_SNAPSHOT_GC_ROOT="${snapshot_gc_root}"
export PALACE_RUNTIME_OUTPUT_MANIFEST="${runtime_manifest}"
export PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256="${runtime_manifest_sha256}"
trap 'handle_runner_signal HUP' HUP
trap 'handle_runner_signal INT' INT
trap 'handle_runner_signal TERM' TERM

report_sha256() {
  local output
  output="$("${sha256_bin}" "$1")"
  printf '%s\n' "${output%% *}"
}

secure_gate_dir() {
  local gate_dir="$1"
  if [ -L "${gate_dir}" ] \
    || { [ -e "${gate_dir}" ] && [ ! -d "${gate_dir}" ]; }; then
    fail_run "run-scope" "gate artifact path is not a secure directory"
  fi
  mkdir -p "${gate_dir}"
  if [ "$(realpath -e -- "${gate_dir}")" != "${gate_dir}" ]; then
    fail_run "run-scope" "gate artifact directory is not canonical"
  fi
  chmod 700 "${gate_dir}"
}

process_scope_evidence_passes() {
  local gate="$1"
  local report="$2"
  local gate_dir
  local evidence
  local launch
  local evidence_run
  local evidence_run_id
  local expected_slice

  if [[ ! "${gate}" =~ ^gate[1-4]$ ]]; then
    return 1
  fi
  gate_dir="$(dirname "${report}")"
  evidence="${gate_dir}/process-scope.json"
  launch="${gate_dir}/process-scope-launch.json"
  evidence_run="$(dirname "${gate_dir}")"
  evidence_run_id="$(basename "${evidence_run}")"
  evidence_run_id="${evidence_run_id#run.}"
  if [[ ! "${evidence_run_id}" =~ ^[A-Za-z0-9]{8}$ ]] \
    || [ -L "${evidence}" ] \
    || [ ! -f "${evidence}" ] \
    || [ -e "${launch}" ] \
    || [ -L "${launch}" ] \
    || [ "$(stat -c '%u:%a' "${evidence}")" \
      != "$(id -u):600" ]; then
    return 1
  fi
  expected_slice="logos-palace-run-${evidence_run_id}.slice"
  "${death_coupled_node[@]}" "${scope_control}" validate-cleaned \
    "${evidence}" "${gate}" "${expected_slice}" >/dev/null 2>&1
}

gate0_report_passes() {
  local report="$1"
  local record
  local output
  local sandbox_nar_hash
  local sandbox_nar_size
  local actual_info
  if [ -L "${report}" ] || [ ! -f "${report}" ]; then
    return 1
  fi
  record="$(
    "${jq_bin}" -er \
      --arg snapshot "${product_snapshot}" \
      --arg source_commit "${source_commit}" \
      --arg snapshot_nar_hash "${snapshot_nar_hash}" \
      --argjson snapshot_nar_size "${snapshot_nar_size}" \
      --arg runner_sha256 "${snapshot_runner_sha256}" \
      --arg runtime_manifest_sha256 "${runtime_manifest_sha256}" \
      --arg basecamp_revision "${basecamp_rev}" \
      --arg basecamp_nar_hash "${basecamp_nar_hash}" \
      --argjson basecamp_nar_size "${basecamp_nar_size}" \
      --arg expected_sandbox_output "${expected_sandbox_test_output}" \
      '
        select(
          .schema == "logos.palace.basecamp-gate0-report"
          and .version == 1
          and .status == "passed"
          and .check == "sandbox-test"
          and .productSnapshot == $snapshot
          and .sourceCommit == $source_commit
          and .productSnapshotNarHash == $snapshot_nar_hash
          and .productSnapshotNarSize == $snapshot_nar_size
          and .snapshotRunnerSha256 == $runner_sha256
          and .runtimeOutputManifestSha256
            == $runtime_manifest_sha256
          and .basecampRevision == $basecamp_revision
          and .basecampRuntimeOutput == "basecamp"
          and .basecampNarHash == $basecamp_nar_hash
          and .basecampNarSize == $basecamp_nar_size
          and (.sandboxTestNarHash | type) == "string"
          and (
            .sandboxTestNarHash
            | test("^sha256-[A-Za-z0-9+/]{43}=$")
          )
          and (.sandboxTestNarSize | type) == "number"
          and .sandboxTestNarSize > 0
          and .sandboxTestNarSize <= 9007199254740991
          and (.sandboxTestNarSize | floor) == .sandboxTestNarSize
          and (.sandboxTestOutput | type) == "string"
          and .sandboxTestOutput == $expected_sandbox_output
        )
        | [
            .sandboxTestOutput,
            .sandboxTestNarHash,
            (.sandboxTestNarSize | tostring)
          ]
        | @tsv
      ' "${report}" 2>/dev/null
  )" || return 1
  IFS=$'\t' read -r \
    output sandbox_nar_hash sandbox_nar_size <<<"${record}"
  if ! valid_store_path "${output}" \
    || [ ! -L "${sandbox_test_gc_root}" ] \
    || [ "$(realpath -e -- "${sandbox_test_gc_root}" 2>/dev/null || true)" \
      != "${output}" ]; then
    return 1
  fi
  actual_info="$(nix path-info --json --json-format 1 "${output}")" \
    || return 1
  "${jq_bin}" -e \
    --arg output "${output}" \
    --arg nar_hash "${sandbox_nar_hash}" \
    --argjson nar_size "${sandbox_nar_size}" \
    '.[$output].narHash == $nar_hash
      and .[$output].narSize == $nar_size' \
    <<<"${actual_info}" >/dev/null 2>&1
}

gate_report_passes() {
  local gate="$1"
  local report="$2"
  local gate3_sha256=""
  local -a reports=("${report}")

  if [ "${gate}" = "gate0" ]; then
    gate0_report_passes "${report}"
    return
  fi
  if [ -L "${report}" ] || [ ! -f "${report}" ]; then
    return 1
  fi
  if [ "${gate}" != "gate1" ]; then
    if [ -L "${gate1_report}" ] || [ ! -f "${gate1_report}" ]; then
      return 1
    fi
    reports+=("${gate1_report}")
  fi
  if [ "${gate}" = "gate4" ]; then
    if [ -L "${gate3_report}" ] || [ ! -f "${gate3_report}" ]; then
      return 1
    fi
    gate3_sha256="$(report_sha256 "${gate3_report}")"
    reports+=("${gate3_report}")
  fi

  if ! "${jq_bin}" -s -e \
    -L "${product_snapshot}/tests" \
    --arg gate "${gate}" \
    --arg snapshot "${product_snapshot}" \
    --arg source_commit "${source_commit}" \
    --arg snapshot_nar_hash "${snapshot_nar_hash}" \
    --argjson snapshot_nar_size "${snapshot_nar_size}" \
    --arg runner_sha256 "${snapshot_runner_sha256}" \
    --arg runtime_manifest_sha256 "${runtime_manifest_sha256}" \
    --arg basecamp_revision "${basecamp_rev}" \
    --arg gate3_sha256 "${gate3_sha256}" \
    --argjson dependencies "${dependency_revisions}" \
    '
      include "basecamp_application_metrics";

      def basecamp:
        if (.basecamp | type) == "object" then
          {
            revision: .basecamp.revision,
            sha256: .basecamp.sha256
          }
        else
          {
            revision: .basecampRevision,
            sha256: .basecampBinarySha256
          }
        end;

      def packages:
        (.productionLgxPackages
          // .lgxPackages
          // .packageHashes
          // [])
        | map({file: .file, sha256: .sha256})
        | sort_by(.file);

      def valid_sha256:
        type == "string" and test("^[0-9a-f]{64}$");

      def valid_packages:
        packages as $packages
        | ($packages | length) == 6
          and ($packages | map(.file) | unique | length) == 6
          and (
            ($packages | map(.file))
            == [
              "logos-delivery_module-module-lib.lgx",
              "logos-lez_core-module-lib.lgx",
              "logos-logos_palace_ui-module.lgx",
              "logos-palace_core-module-lib.lgx",
              "logos-palace_vm-module-lib.lgx",
              "logos-storage_module-module-lib.lgx"
            ]
          )
          and all($packages[]; .sha256 | valid_sha256);

      def exact_object_keys($expected):
        type == "object"
          and ((keys | sort) == ($expected | sort));

      def valid_file_identity:
        exact_object_keys(["device", "inode"])
          and (.device | test("^[0-9a-f]+:[0-9a-f]+$"))
          and (.inode | test("^[1-9][0-9]*$"));

      def valid_executable_mapping($path; $identity):
        exact_object_keys([
          "path",
          "permissions",
          "device",
          "inode"
        ])
          and .path == $path
          and (.permissions | test("^[r-][w-]x[ps]$"))
          and ({device, inode} | valid_file_identity)
          and .device == $identity.device
          and .inode == $identity.inode;

      def valid_nonnegative_integer:
        type == "number"
          and . >= 0
          and . <= 9007199254740991
          and floor == .;

      def valid_program_deployment:
        exact_object_keys([
          "status",
          "explorerOrigin",
          "path",
          "pagesScanned",
          "blocksScanned",
          "blockId",
          "blockHash",
          "transactionHash",
          "byteLength",
          "bytecodeSha256",
          "sha256",
          "risc0ImageIdHex",
          "programIdHex",
          "bedrockStatus"
        ])
          and .status == "passed"
          and .explorerOrigin
            == "https://explorer.testnet.lez.logos.co"
          and .path == "/api/get_blocks3022937127152978530"
          and (.pagesScanned | valid_nonnegative_integer)
          and .pagesScanned > 0
          and (.blocksScanned | valid_nonnegative_integer)
          and .blocksScanned > 0
          and .blockId == 41029
          and .blockHash
            == "0ea1852f8c91d9ba8003844d1c98c68ba1b6ddc08a7c9dbf23dec95eb0460b92"
          and .transactionHash
            == "98711414b02a12a9abfdd17780337f7f32c962df1dea2f50e3deeecba3c7a0b5"
          and .byteLength == 297312
          and .bytecodeSha256
            == "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171"
          and .sha256 == .bytecodeSha256
          and .risc0ImageIdHex
            == "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61"
          and .programIdHex == .risc0ImageIdHex
          and .bedrockStatus == "Finalized";

      def valid_uninitialized_release_root:
        exact_object_keys([
          "status",
          "state",
          "explorerOrigin",
          "path",
          "accountIdBase58",
          "programOwner",
          "balance",
          "nonce",
          "dataBytes",
          "dataSha256",
          "responseSha256"
        ])
          and .status == "passed"
          and .state == "uninitialized"
          and .explorerOrigin
            == "https://explorer.testnet.lez.logos.co"
          and .path == "/api/get_account3022937127152978530"
          and .accountIdBase58
            == "2H9vVPVToHwyHer6e7dAUGA7ToQmi4HSRscjounkkig9"
          and .programOwner == "11111111111111111111111111111111"
          and (.balance | valid_nonnegative_integer)
          and .nonce == 0
          and .dataBytes == 0
          and .dataSha256
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
          and (.responseSha256 | valid_sha256);

      def valid_release_preflight:
        exact_object_keys([
          "schema",
          "version",
          "status",
          "startedAtUnixMs",
          "completedAtUnixMs",
          "release",
          "programDeployment",
          "rootAccountBeforeWrites"
        ])
          and .schema == "logos.palace.release-preflight"
          and .version == 1
          and .status == "passed"
          and (.startedAtUnixMs | valid_nonnegative_integer)
          and .startedAtUnixMs > 0
          and (.completedAtUnixMs | valid_nonnegative_integer)
          and .completedAtUnixMs >= .startedAtUnixMs
          and .release == {
            programIdHex:
              "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61",
            programBytecodeSha256:
              "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171",
            programByteLength: 297312,
            deploymentTransactionHash:
              "98711414b02a12a9abfdd17780337f7f32c962df1dea2f50e3deeecba3c7a0b5",
            rootAccountIdHex:
              "12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a",
            rootAccountIdBase58:
              "2H9vVPVToHwyHer6e7dAUGA7ToQmi4HSRscjounkkig9"
          }
          and (.programDeployment | valid_program_deployment)
          and (
            .rootAccountBeforeWrites
            | valid_uninitialized_release_root
          );

      def valid_release_identity($display):
        exact_object_keys([
          "accountId",
          "display",
          "deliveryKey",
          "keyEpoch",
          "registrationTransaction",
          "existing",
          "receipt"
        ])
          and (.accountId | valid_sha256)
          and .display == $display
          and (.deliveryKey | valid_sha256)
          and .keyEpoch == "1"
          and (.registrationTransaction | valid_sha256)
          and (.existing | type) == "boolean"
          and (.receipt | type) == "string"
          and (.receipt | length) > 0
          and (.receipt | length) <= 65536;

      def valid_private_storage_bootstrap_nodes:
        type == "array"
          and length >= 1
          and length <= 8
          and all(
            .[];
            type == "string"
              and length >= 16
              and length <= 8192
              and (contains("\u0000") | not)
          );

      def valid_private_storage_config:
        fromjson
        | .["log-level"] == "INFO"
          and .["listen-ip"] == "127.0.0.1"
          and .nat == "none"
          and (.["listen-port"] | valid_nonnegative_integer)
          and .["listen-port"] >= 1024
          and .["listen-port"] <= 65535
          and (.["disc-port"] | valid_nonnegative_integer)
          and .["disc-port"] >= 1024
          and .["disc-port"] <= 65535
          and (
            (
              exact_object_keys([
                "disc-port",
                "listen-ip",
                "listen-port",
                "log-level",
                "nat",
                "no-bootstrap-node"
              ])
              and .["no-bootstrap-node"] == true
            )
            or
            (
              exact_object_keys([
                "bootstrap-node",
                "disc-port",
                "listen-ip",
                "listen-port",
                "log-level",
                "nat"
              ])
              and (.["bootstrap-node"]
                | valid_private_storage_bootstrap_nodes)
            )
          );

      def valid_asset_invocation:
        exact_object_keys(["receipt", "elapsedMs"])
          and (.receipt | type) == "string"
          and (.receipt | length) > 0
          and (.receipt | length) <= 65536
          and (.elapsedMs | valid_nonnegative_integer)
          and .elapsedMs <= 180000;

      def valid_asset_target:
        (
          exact_object_keys(["kind", "roomId"])
          and .kind == "room-background"
          and (.roomId == "atrium" or .roomId == "lounge")
        )
        or
        (
          exact_object_keys([
            "kind",
            "propId",
            "anchorX",
            "anchorY",
            "layer"
          ])
          and .kind == "prop-image"
          and (.propId | test("^[a-z][a-z0-9_-]{0,63}$"))
          and (.anchorX | valid_nonnegative_integer)
          and (.anchorY | valid_nonnegative_integer)
          and (
            .layer == "head"
            or .layer == "body"
            or .layer == "hand"
            or .layer == "back"
          )
        );

      def valid_authored_asset:
        . as $asset
        | (
            keys | sort
          ) == (
            ([
              "assetId",
              "label",
              "file",
              "handle",
              "width",
              "height",
              "byteLength",
              "role",
              "chunkBytes",
              "chunkCount",
              "begin",
              "appends",
              "commit",
              "review",
              "publication",
              "cid"
            ] + (
              if has("target")
              then ["target", "assignment"]
              else []
              end
            )) | sort
          )
          and (.assetId | test("^[a-z][a-z0-9_-]{0,63}$"))
          and (.file | test("^[a-z0-9][a-z0-9._-]{0,127}\\.png$"))
          and .label == (.file | if length > 32 then "selected-image.png" else . end)
          and (.handle | valid_sha256)
          and (.width | valid_nonnegative_integer)
          and .width > 0
          and (.height | valid_nonnegative_integer)
          and .height > 0
          and (.byteLength | valid_nonnegative_integer)
          and .byteLength > 0
          and .byteLength <= 10485760
          and (.role == "room-background" or .role == "prop-image")
          and (
            (has("target") | not)
            or (
              (.target | valid_asset_target)
              and .target.kind == .role
            )
          )
          and .chunkBytes == 32768
          and (.chunkCount | valid_nonnegative_integer)
          and .chunkCount > 0
          and (.begin | valid_asset_invocation)
          and (
            .begin.receipt
            | test(
                "^ok;session=[0-9a-f]{32};next=0;maxChunkBytes=32768;maxTotalBytes=10485760$"
              )
          )
          and (.appends | type) == "array"
          and (.appends | length) == .chunkCount
          and (
            [.appends[].byteLength] | add
          ) == .byteLength
          and all(
            range(0; .chunkCount);
            . as $index
            | $asset.appends[$index] as $append
            | (
                $append
                | exact_object_keys([
                    "sequence",
                    "byteLength",
                    "receipt",
                    "elapsedMs"
                  ])
              )
              and $append.sequence == $index
              and ($append.byteLength | valid_nonnegative_integer)
              and $append.byteLength > 0
              and $append.byteLength <= 32768
              and (
                {
                  receipt: $append.receipt,
                  elapsedMs: $append.elapsedMs
                }
                | valid_asset_invocation
              )
              and (
                $append.receipt
                | test(
                    "^ok;session=[0-9a-f]{32};next=[0-9]+;bytes=[0-9]+$"
                  )
              )
          )
          and (.commit | valid_asset_invocation)
          and .commit.receipt
            == "ok;handle=\(.handle);width=\(.width);height=\(.height);bytes=\(.byteLength)"
          and (.review | valid_asset_invocation)
          and .review.receipt
            == "ok;handle=\(.handle);review=approved"
          and (
            .publication
            | exact_object_keys(["dispatched", "completed"])
          )
          and (.publication.dispatched | valid_asset_invocation)
          and .publication.dispatched.receipt == "ok;asset=publishing"
          and (.publication.completed | valid_asset_invocation)
          and .publication.completed.receipt == "published;cid=\(.cid)"
          and (
            .cid
            | test("^(b[a-z2-7]+|z[1-9A-HJ-NP-Za-km-z]+)$")
          )
          and (
            if has("target")
            then (
              (.assignment | valid_asset_invocation)
              and (
                (
                  .target.kind == "room-background"
                  and .assignment.receipt
                    == "ok;room=\(.target.roomId);handle=\(.handle)"
                )
                or
                (
                  .target.kind == "prop-image"
                  and .assignment.receipt
                    == "ok;propId=\(.target.propId);handle=\(.handle);anchorX=\(.target.anchorX);anchorY=\(.target.anchorY);layer=\(.target.layer)"
                )
              )
            )
            else true
            end
          );

      def valid_asset_authoring_evidence:
        . as $report
        | $report.assetAuthoring as $authoring
        | $report.assetAuthoringScreenshot as $screenshot
        | (
            $authoring
            | exact_object_keys([
              "version",
              "phase",
              "inputManifest",
              "selectedAssetCount",
              "propStory",
              "boundary",
              "guardedBeforeApproval",
              "assets",
              "graphBindings",
              "activePropProjection",
              "elapsedMs",
              "catalogCount",
              "assignments"
            ])
          )
          and $authoring.version == 1
          and $authoring.phase == "complete"
          and (
            $authoring.inputManifest
            | exact_object_keys([
                "schema",
                "version",
                "sha256",
                "assetCount"
              ])
          )
          and $authoring.inputManifest.schema
            == "logos.palace.e2e-asset-inputs"
          and $authoring.inputManifest.version == 1
          and ($authoring.inputManifest.sha256 | valid_sha256)
          and ($authoring.inputManifest.assetCount
            | valid_nonnegative_integer)
          and $authoring.inputManifest.assetCount >= 2
          and $authoring.inputManifest.assetCount <= 128
          and $authoring.selectedAssetCount
            == $authoring.inputManifest.assetCount
          and (
            $authoring.propStory == "requested"
            or $authoring.propStory == "not-requested"
          )
          and $authoring.boundary
            == "operator-selected bounded PNG bytes -> verified handle -> approval -> local-byte-verified Storage CID -> manifest assignment"
          and (
            $authoring.guardedBeforeApproval
            | valid_asset_invocation
          )
          and $authoring.guardedBeforeApproval.receipt
            == "rejected=asset-not-approved"
          and ($authoring.elapsedMs | valid_nonnegative_integer)
          and $authoring.elapsedMs <= 1800000
          and ($authoring.catalogCount | valid_nonnegative_integer)
          and $authoring.catalogCount >= $authoring.selectedAssetCount
          and ($authoring.assets | type) == "array"
          and ($authoring.assets | length) == $authoring.selectedAssetCount
          and all($authoring.assets[]; valid_authored_asset)
          and (
            $authoring.assets | map(.assetId) | unique | length
          ) == $authoring.selectedAssetCount
          and (
            $authoring.assets | map(.handle) | unique | length
          ) == $authoring.selectedAssetCount
          and (
            $authoring.assets | map(.cid) | unique | length
          ) == $authoring.selectedAssetCount
          and (
            $authoring.assignments
            | exact_object_keys(["rooms", "prop"])
          )
          and (
            $authoring.assignments.rooms
            | exact_object_keys(["atrium", "lounge"])
          )
          and ($authoring.assignments.rooms.atrium | valid_sha256)
          and ($authoring.assignments.rooms.lounge | valid_sha256)
          and all($authoring.assets[]; has("target"))
          and (
            $authoring.assets
            | map(select(.target.kind? == "room-background"))
            | length
          ) >= 2
          and (
            reduce $authoring.assets[] as $asset (
              {atrium: "", lounge: ""};
              if $asset.target.kind == "room-background"
              then .[$asset.target.roomId] = $asset.handle
              else .
              end
            )
          ) == $authoring.assignments.rooms
          and (
            if $authoring.propStory == "requested"
            then (
              (
                $authoring.assignments.prop
                | exact_object_keys([
                    "propId",
                    "handle",
                    "anchorX",
                    "anchorY",
                    "layer"
                  ])
              )
              and ($authoring.assignments.prop.handle | valid_sha256)
              and (
                $authoring.assets
                | map(select(.target.kind? == "prop-image"))
                | length
              ) == 1
              and (
                $authoring.assets
                | map(select(.target.kind == "prop-image"))
                | .[0] as $prop
                | (
                    {
                      propId: $prop.target.propId,
                      handle: $prop.handle,
                      anchorX: $prop.target.anchorX,
                      anchorY: $prop.target.anchorY,
                      layer: $prop.target.layer
                    } == $authoring.assignments.prop
                    and $authoring.activePropProjection == {
                      version: 1,
                      available: true,
                      propId: $prop.target.propId,
                      handle: $prop.handle,
                      contentSha256: $prop.handle,
                      width: $prop.width,
                      height: $prop.height,
                      anchorX: $prop.target.anchorX,
                      anchorY: $prop.target.anchorY,
                      layer: $prop.target.layer
                    }
                  )
              )
            )
            else (
              $authoring.assignments.prop == null
              and (
                $authoring.assets
                | map(select(.target.kind? == "prop-image"))
                | length
              ) == 0
              and $authoring.activePropProjection == {
                version: 1,
                available: false
              }
            )
            end
          )
          and ($authoring.graphBindings | type) == "array"
          and ($authoring.graphBindings | length)
            == (if $authoring.propStory == "requested" then 3 else 2 end)
          and (
            $authoring.graphBindings
            | map(.objectId) | sort
          ) == (
            [
              "background-atrium",
              "background-lounge"
            ] + (
              if $authoring.propStory == "requested"
              then [
                "prop-\($authoring.assignments.prop.propId)-image"
              ]
              else []
              end
            ) | sort
          )
          and all(
            $authoring.graphBindings[];
            . as $binding
            | ($binding.contentSha256 | valid_sha256)
              and any(
                $authoring.assets[];
                .assetId == $binding.assetId
                  and .handle == $binding.contentSha256
                  and .cid == $binding.cid
              )
              and any(
                $report.publication.objects[];
                .objectId == $binding.objectId
                  and .cid == $binding.cid
                  and .contentSha256 == $binding.contentSha256
              )
          )
          and (
            $screenshot
            | exact_object_keys([
              "file",
              "artifactPath",
              "width",
              "height",
              "byteLength",
              "sha256",
              "stage",
              "state",
              "label",
              "renderEvidence"
            ])
          )
          and $screenshot.file
            == "gate3-admin-assets-published.png"
          and $screenshot.artifactPath == $screenshot.file
          and $screenshot.width == 1600
          and $screenshot.height == 900
          and ($screenshot.byteLength | valid_nonnegative_integer)
          and $screenshot.byteLength >= 24
          and $screenshot.byteLength <= 67108864
          and ($screenshot.sha256 | valid_sha256)
          and $screenshot.stage
            == "gate3-admin-asset-authoring"
          and $screenshot.state
            == "admin-selected-assets-approved-published-assigned"
          and $screenshot.label == "a"
          and (
            $screenshot.renderEvidence
            | exact_object_keys([
              "schema",
              "version",
              "open",
              "cardCount",
              "readyImageCount",
              "publishedCount",
              "atriumAssigned",
              "loungeAssigned",
              "propAssigned",
              "fenceRequest",
              "fenceState",
              "fenceFrame",
              "epoch"
            ])
          )
          and $screenshot.renderEvidence.schema
            == "logos.palace.asset-authoring-render"
          and $screenshot.renderEvidence.version == 1
          and $screenshot.renderEvidence.open == true
          and $screenshot.renderEvidence.cardCount
            >= $authoring.selectedAssetCount
          and $screenshot.renderEvidence.readyImageCount
            == $screenshot.renderEvidence.cardCount
          and $screenshot.renderEvidence.publishedCount
            >= $authoring.selectedAssetCount
          and $screenshot.renderEvidence.atriumAssigned == true
          and $screenshot.renderEvidence.loungeAssigned == true
          and $screenshot.renderEvidence.propAssigned
            == ($authoring.propStory == "requested")
          and (
            $screenshot.renderEvidence.fenceRequest
            | valid_nonnegative_integer
          )
          and $screenshot.renderEvidence.fenceRequest > 0
          and $screenshot.renderEvidence.fenceState == "complete"
          and (
            $screenshot.renderEvidence.fenceFrame
            | valid_nonnegative_integer
          )
          and (
            $screenshot.renderEvidence.epoch
            | valid_nonnegative_integer
          )
          and $screenshot.renderEvidence.epoch
            >= $authoring.selectedAssetCount;

      def valid_gate3_release_evidence:
        . as $report
        | ($report.publication.objects | length) as $object_count
        | (.releasePreflight | valid_release_preflight)
          and valid_asset_authoring_evidence
          and (
            .identities
            | exact_object_keys(["a", "b", "c"])
          )
          and (.identities.a | valid_release_identity("Alice"))
          and (.identities.b | valid_release_identity("Bob"))
          and (.identities.c | valid_release_identity("Carol"))
          and (
            [.identities.a.accountId,
             .identities.b.accountId,
             .identities.c.accountId]
            | unique | length
          ) == 3
          and (
            [.identities.a.deliveryKey,
             .identities.b.deliveryKey,
             .identities.c.deliveryKey]
            | unique | length
          ) == 3
          and (
            [.identities.a.registrationTransaction,
             .identities.b.registrationTransaction,
             .identities.c.registrationTransaction]
            | unique | length
          ) == 3
          and (
            .storageConfigs
            | exact_object_keys(["a", "b", "c"])
          )
          and all(
            [.storageConfigs.a, .storageConfigs.b, .storageConfigs.c][];
            type == "string"
              and length > 0
              and length <= 65536
              and valid_private_storage_config
          )
          and all(
            [.providerBFetch, .coldCFetch][];
            .mode == "network"
              and (.endToEndMs | valid_nonnegative_integer)
              and (.verified | length) == $object_count
          )
          and all(
            [.providerBCachedFetch, .coldCCachedFetch][];
            .mode == "cache"
              and (.endToEndMs | valid_nonnegative_integer)
              and (.verified | length) == $object_count
          );

      def expected_screenshots:
        [
          {
            file: "gate4-a-three-user-atrium-converged.png",
            stage: "gate4-delivery-convergence",
            state: "three-user-atrium-converged",
            label: "a"
          },
          {
            file: "gate4-b-storage-object-degraded.png",
            stage: "gate4-storage-failure",
            state: "missing-storage-object-degraded",
            label: "b"
          },
          {
            file: "gate4-b-atrium-after-moderation.png",
            stage: "gate4-moderation",
            state: "atrium-after-human-moderation",
            label: "b"
          },
          {
            file: "gate5-a-door-preview.png",
            stage: "gate5-preview",
            state: "door-preview-before-finality",
            label: "a"
          },
          {
            file: "gate5-b-door-preview.png",
            stage: "gate5-preview",
            state: "door-preview-before-finality",
            label: "b"
          },
          {
            file: "gate5-b-door-pending.png",
            stage: "gate5-pending",
            state: "door-awaiting-lez-observation",
            label: "b"
          },
          {
            file: "gate5-b-lounge-finalized.png",
            stage: "gate5-finality",
            state: "lounge-after-door-finality",
            label: "b"
          },
          {
            file: "gate6-b-offline-before-reconnect.png",
            stage: "gate6-offline",
            state: "creator-offline-client-before-reconnect",
            label: "b"
          },
          {
            file: "gate6-b-lounge-restarted.png",
            stage: "gate6-restart",
            state: "lounge-after-bob-carol-restart",
            label: "b"
          },
          {
            file: "gate6-c-lounge-restarted.png",
            stage: "gate6-restart",
            state: "lounge-after-bob-carol-restart",
            label: "c"
          }
        ];

      def valid_screenshot_evidence:
        (.screenshots | type) == "array"
          and (.screenshots | length) == 10
          and (
            .screenshots
            | map({file, stage, state, label})
            | sort_by(.file)
          ) == (expected_screenshots | sort_by(.file))
          and all(
            .screenshots[];
            exact_object_keys([
              "file",
              "stage",
              "state",
              "label",
              "artifactPath",
              "width",
              "height",
              "byteLength",
              "sha256"
            ])
              and .artifactPath == .file
              and (.width | valid_nonnegative_integer)
              and .width > 0
              and (.height | valid_nonnegative_integer)
              and .height > 0
              and (.byteLength | valid_nonnegative_integer)
              and .byteLength > 0
              and (.sha256 | valid_sha256)
          );

      def valid_gate4_behaviors:
        . as $gate4
        | ($gate4.catalog.byId | length) as $object_count
        | ($gate4.plan.propStory == "requested") as $prop_requested
        | $gate4.plan.doorActionId as $door_action_id
        | .processModel.runtimeArtifacts as $process_artifacts
        | .processModel.loaderSelection as $loader_selection
        | $process_artifacts.wrapperExecution as $wrapper_execution
        | ($gate4 | packages) as $process_package_hashes
        | .storage.initial.status == "passed"
          and .storage.initial.productionIdentityHolders == true
          and .storage.initial.acceptanceHolderProfile == false
          and all(
            [.storage.initial.catalog.a,
             .storage.initial.catalog.b,
             .storage.initial.catalog.c][];
            .mode == "cache"
              and (.endToEndMs | valid_nonnegative_integer)
              and (.retentionMs | valid_nonnegative_integer)
              and (.objects | length) == $object_count
          )
          and .storage.restart.status == "passed"
          and .storage.restart.retainedDataRoots == true
          and .storage.restart.clients.b.recovered.mode == "network"
          and .storage.restart.clients.b.recovered.nativeSource
            == "network"
          and .storage.restart.clients.b.recovered.nativeAvailable == 0
          and .storage.restart.clients.b.recovered.nativeTotal
            == $object_count
          and (
            .storage.restart.clients.b.recovered.endToEndMs
            | valid_nonnegative_integer
          )
          and (
            .storage.restart.clients.b.recovered.objects | length
          ) == $object_count
          and .storage.restart.clients.c.recovered.mode == "cache"
          and .storage.restart.clients.c.recovered.nativeSource == "cache"
          and .storage.restart.clients.c.recovered.nativeAvailable
            == $object_count
          and .storage.restart.clients.c.recovered.nativeTotal
            == $object_count
          and (
            .storage.restart.clients.c.recovered.endToEndMs
            | valid_nonnegative_integer
          )
          and (
            .storage.restart.clients.c.recovered.objects | length
          ) == $object_count
          and .storage.restart.sourceBinding.sourceLabel == "c"
          and .storage.restart.sourceBinding.sourceAccountId
            == .identities.c.accountId
          and .storage.restart.sourceBinding.exactCatalogChecksum
            == .catalog.checksum
          and .storage.restart.sourceBinding
            .retainedCatalogVerifiedBeforeColdFetch == true
          and .storage.restart.sourceBinding
            .retainedCatalogVerifiedAfterColdFetch == true
          and .storage.restart.sourceBinding.sourceNativeAvailable
            == $object_count
          and .storage.restart.sourceBinding.sourceNativeTotal
            == $object_count
          and .storage.restart.sourceBinding.coldClientNativeAvailable == 0
          and .storage.restart.sourceBinding.coldClientNativeTotal
            == $object_count
          and .storage.restart.sourceBinding.creatorOffline == true
          and .storage.restart.sourceBinding.coldClientDataRootRemoved
            == true
          and .storage.restart.sourceBinding.coldClientStorageNotStarted
            == true
          and .storage.restart.sourceBinding.onlineRetainedHolderLabels
            == ["c"]
          and .delivery.initialLifecycle.status == "passed"
          and .delivery.initialLifecycle.propStory
            == $gate4.plan.propStory
          and .delivery.initialLifecycle.approvedPropVisible
            == $prop_requested
          and (.delivery.initialMesh.elapsedMs
            | valid_nonnegative_integer)
          and .delivery.restartMesh.entryLabel == "b"
          and (.delivery.restartMesh.elapsedMs
            | valid_nonnegative_integer)
          and .delivery.restartBehavior.status == "passed"
          and .delivery.restartBehavior.creatorPidOffline == true
          and .delivery.restartBehavior.carolBanned.receipt
            == "rejected=delivery-publish;preflight=sender-banned"
          and .delivery.restartBehavior.propStory
            == $gate4.plan.propStory
          and (
            if $prop_requested
            then .delivery.restartBehavior.propBanned.receipt
              == "rejected=delivery-publish;preflight=invalid-or-banned-payload"
            else (.delivery.restartBehavior | has("propBanned") | not)
            end
          )
          and .moderation.unauthorized.status == "passed"
          and .moderation.unauthorized.caller == "c"
          and .moderation.unauthorized.callerAccountId
            == .identities.c.accountId
          and .moderation.unauthorized.moduleRejected.receipt
            == "rejected=lez-submit;reason=module-rejected"
          and .moderation.userBan.status == "passed"
          and .moderation.userBan.staleCheckpoint == 7
          and .moderation.userBan.finalizedCheckpoint == 8
          and .moderation.userBan.projectionMutation == false
          and .moderation.userBan.rejectionClass == "other"
          and (
            if $prop_requested
            then (
              .moderation.assetBan.status == "passed"
              and .moderation.assetBan.staleCheckpoint == 8
              and .moderation.assetBan.finalizedCheckpoint == 9
              and .moderation.assetBan.projectionMutation == false
              and .moderation.assetBan.rejectionClass == "payload"
              and .moderation.humanUi.propBanMethod == "gate4BanProp"
            )
            else (
              .moderation.assetBan == {
                status: "not-requested",
                propStory: "not-requested"
              }
              and .moderation.humanUi.propStory == "not-requested"
              and .moderation.humanUi.propBanMethod == null
            )
            end
          )
          and .gate5.preview.status == "passed"
          and .gate5.preview.exactSameReceipt == true
          and .gate5.preview.exactSameStateRoot == true
          and .gate5.preview.navigationBeforeFinality == 0
          and .gate5.preview.a.fields.action == $door_action_id
          and .gate5.preview.b.fields.action == $door_action_id
          and .gate5.preview.a.fields.navigation == "0"
          and .gate5.preview.b.fields.navigation == "0"
          and .gate5.preview.a.fields.receipt_sha256
            == .gate5.preview.b.fields.receipt_sha256
          and (.gate5.preview.a.fields.receipt_sha256 | valid_sha256)
          and .gate5.finality.status == "passed"
          and (.gate5.finality.pendingEvidence | type) == "array"
          and (.gate5.finality.pendingEvidence | length) >= 2
          and (
            .gate5.finality.final.receipt
            | type == "string"
              and contains("durable=finalized")
              and contains("navigation=1")
          )
          and .gate5.convergence.status == "passed"
          and .gate5.convergence.checkpoint
            == ($door_action_id | tonumber)
          and (.gate5.convergence.sharedStateRoot | valid_sha256)
          and (.gate5.convergence.authorityProjectionDigest
            | valid_sha256)
          and .gate6.status == "passed"
          and .gate6.creator.offline == true
          and .gate6.creator.offlineAtCompletion == true
          and .gate6.assets.status == "passed"
          and .gate6.assets.holders == ["b", "c"]
          and .failureEvidence.missingStorageObject.status == "passed"
          and .failureEvidence.missingStorageObject.objectId
            == "background-atrium"
          and .failureEvidence.missingStorageObject.missingSourceCid
            == "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx"
          and all(
            .catalog.byId[];
            .cid
              != "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx"
          )
          and .failureEvidence.missingStorageObject.derivativeCid
            == .catalog.byId["background-atrium"].cid
          and .failureEvidence.missingStorageObject.derivativeCid
            != .failureEvidence.missingStorageObject.missingSourceCid
          and .failureEvidence.missingStorageObject
            .expectedContentSha256
            == .catalog.byId["background-atrium"].contentSha256
          and (
            .failureEvidence.missingStorageObject
              .expectedContentSha256
            | valid_sha256
          )
          and .failureEvidence.missingStorageObject.states
            == ["missing", "fetching", "degraded"]
          and .failureEvidence.missingStorageObject.before.receipt
            == "missing"
          and (
            .failureEvidence.missingStorageObject.dispatched.receipt
            | test(
                "^ok;asset=fetching;operation=palace-asset-(0|[1-9][0-9]{0,19})$"
              )
          )
          and (
            .failureEvidence.missingStorageObject.degraded.receipt
            | test(
                "^degraded;reason=storage-download-[a-z0-9][a-z0-9-]{0,127}$"
              )
          )
          and .failureEvidence.delayedLezUpdate.status == "passed"
          and .failureEvidence.delayedLezUpdate.actionId
            == $door_action_id
          and .failureEvidence.delayedLezUpdate.observationPaused == true
          and (
            .failureEvidence.delayedLezUpdate.pauseMs
            | valid_nonnegative_integer
          )
          and .failureEvidence.delayedLezUpdate.pauseMs >= 1000
          and .failureEvidence.clientOffline.status == "passed"
          and .failureEvidence.clientOffline.label == "b"
          and .failureEvidence.clientOffline.creatorOffline == true
          and .failureEvidence.coldClientRebuild.status == "passed"
          and .failureEvidence.coldClientRebuild.label == "b"
          and .failureEvidence.coldClientRebuild.storageRecoveryMode
            == "network"
          and .failureEvidence.coldClientRebuild.preservationProof
            .deliveryIdentityUnchangedByDeletion == true
          and .failureEvidence.coldClientRebuild.preservationProof
            .lezWalletUnchangedByDeletion == true
          and .failureEvidence.coldClientRebuild
            .openedExistingLezWallet == true
          and .failureEvidence.coldClientRebuild
            .exactFinalizedProjection == true
          and (.uiEvidence.pending | type) == "array"
          and (.uiEvidence.pending | length) >= 2
          and (.uiEvidence.finalized | type) == "array"
          and (.uiEvidence.finalized | length) >= 1
          and (.uiEvidence.degraded | type) == "array"
          and (.uiEvidence.degraded | length) >= 1
          and (.uiEvidence.offline | type) == "array"
          and (.uiEvidence.offline | length) >= 1
          and (.actions | type) == "array"
          and (.actions | length) == (.plan.actions | length)
          and all(
            .actions[];
            . as $action
            | (
                all(
                  ["submitMs", "observeMs", "finalityMs", "totalMs"][];
                  . as $field
                  | ($field | sub("Ms$"; "")) as $phase
                  | ($phase + "StartedAtUnixMs") as $started
                  | ($phase + "CompletedAtUnixMs") as $completed
                  | (
                      $action.timingMeasurement[$field] == "measured"
                      or (
                        $field == "finalityMs"
                        and $action.timingMeasurement[$field]
                          == "measured-coalesced"
                        and $action.timings[$field] == 0
                      )
                    )
                    and (
                      $action.timings[$field]
                      | valid_nonnegative_integer
                    )
                    and (
                      $action.timingBoundaries[$started]
                      | valid_nonnegative_integer
                    )
                    and $action.timingBoundaries[$started] > 0
                    and (
                      $action.timingBoundaries[$completed]
                      | valid_nonnegative_integer
                    )
                    and $action.timingBoundaries[$completed]
                      >= $action.timingBoundaries[$started]
                    and (
                      $action.timingBoundaries[$completed]
                      - $action.timingBoundaries[$started]
                    ) == $action.timings[$field]
                  )
                and $action.timingBoundaries.submitStartedAtUnixMs
                  == $action.timingBoundaries.totalStartedAtUnixMs
                and $action.timingBoundaries.submitCompletedAtUnixMs
                  == $action.timingBoundaries.observeStartedAtUnixMs
                and $action.timingBoundaries.observeCompletedAtUnixMs
                  == $action.timingBoundaries.finalityStartedAtUnixMs
                and $action.timingBoundaries.finalityCompletedAtUnixMs
                  == $action.timingBoundaries.totalCompletedAtUnixMs
              )
          )
          and all(
            ["b", "c"][];
            . as $label
            | $gate4.restart[$label].identity.accountId
                == $gate4.identities[$label].accountId
              and $gate4.restart[$label].identity.deliveryKey
                == $gate4.identities[$label].deliveryKey
              and $gate4.restart[$label].identity.display
                == $gate4.identities[$label].display
              and (
                $gate4.restart[$label].checkpoint.status.receipt
                | type == "string" and contains("action=10")
              )
          )
          and .processModel.standalonePalaceServer == false
          and (
            .processModel
            | exact_object_keys([
                "standalonePalaceServer",
                "loaderSelection",
                "runtimeArtifacts",
                "observationMethod",
                "observations"
              ])
          )
          and (
            $process_artifacts
            | exact_object_keys([
                "basecampBundlePrograms",
                "basecampBundleModules",
                "installedLgxModules",
                "wrapperExecution"
              ])
          )
          and (
            $loader_selection
            | exact_object_keys([
                "mode",
                "fallbackIndex",
                "candidatePath",
                "canonicalPath",
                "argumentBasename",
                "executableBasename",
                "sha256",
                "device",
                "inode"
              ])
          )
          and (
            $wrapper_execution
            | exact_object_keys([
                "mode",
                "fallbackIndex",
                "argumentBasename",
                "executableBasename",
                "sha256"
              ])
          )
          and $wrapper_execution == (
            $loader_selection
            | {
                mode,
                fallbackIndex,
                argumentBasename,
                executableBasename,
                sha256
              }
          )
          and (
            {
              device: $loader_selection.device,
              inode: $loader_selection.inode
            }
            | valid_file_identity
          )
          and (
            (
              $loader_selection.mode == "direct"
              and $loader_selection.fallbackIndex == null
              and $loader_selection.candidatePath
                == "/lib/ld-linux-x86-64.so.2"
              and (
                $loader_selection.canonicalPath
                | type == "string"
                  and startswith("/")
                  and (
                    split("/")
                    | all(. != "." and . != "..")
                  )
              )
              and $loader_selection.argumentBasename == null
              and (
                $loader_selection.canonicalPath
                | split("/")
                | .[-1]
              ) == $loader_selection.executableBasename
              and (
                $loader_selection.executableBasename
                | type == "string"
                  and length > 0
                  and (contains("/") | not)
              )
              and ($loader_selection.sha256 | valid_sha256)
            )
            or
            (
              $loader_selection.mode == "fallback"
              and (
                $loader_selection.fallbackIndex
                | valid_nonnegative_integer
              )
              and $loader_selection.fallbackIndex < 6
              and $loader_selection.candidatePath == (
                [
                  "/lib64/ld-linux-x86-64.so.2",
                  "/lib/ld-linux-x86-64.so.2",
                  "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
                  "/lib/aarch64-linux-gnu/ld-linux-x86-64.so.2",
                  "/usr/lib64/ld-linux-x86-64.so.2",
                  "/usr/lib/ld-linux-x86-64.so.2"
                ][$loader_selection.fallbackIndex]
              )
              and (
                $loader_selection.canonicalPath
                | type == "string"
                  and startswith("/")
                  and (
                    split("/")
                    | all(. != "." and . != "..")
                  )
              )
              and (
                $loader_selection.candidatePath
                | split("/")
                | .[-1]
              ) == $loader_selection.argumentBasename
              and (
                $loader_selection.canonicalPath
                | split("/")
                | .[-1]
              ) == $loader_selection.executableBasename
              and (
                $loader_selection.argumentBasename
                | test("^ld(?:-[a-z0-9_-]+)?-linux[^/]*\\.so(?:\\.[0-9]+)*$"; "i")
              )
              and (
                $loader_selection.executableBasename
                | type == "string"
                  and length > 0
                  and (contains("/") | not)
              )
              and ($loader_selection.sha256 | valid_sha256)
            )
          )
          and (
            $process_artifacts.basecampBundlePrograms
            | map(del(.sha256))
          ) == [
            {
              role: "basecamp-main",
              program: "LogosBasecamp",
              argument: ".LogosBasecamp.elf",
              relativePath: "bin/.LogosBasecamp.elf"
            },
            {
              role: "core-module-host",
              program: "logos_host",
              argument: ".logos_host.elf",
              relativePath: "bin/.logos_host.elf"
            },
            {
              role: "ui-module-host",
              program: "ui-host",
              argument: ".ui-host.elf",
              relativePath: "bin/.ui-host.elf"
            }
          ]
          and all(
            $process_artifacts.basecampBundlePrograms[];
            exact_object_keys([
              "role",
              "program",
              "argument",
              "relativePath",
              "sha256"
            ])
              and (.sha256 | valid_sha256)
          )
          and (
            $process_artifacts.basecampBundleModules
            | map(del(.sha256))
          ) == [
            {
              moduleName: "capability_module",
              relativePath:
                "modules/capability_module/capability_module_plugin.so"
            },
            {
              moduleName: "package_downloader",
              relativePath:
                "modules/package_downloader/package_downloader_plugin.so"
            },
            {
              moduleName: "package_manager",
              relativePath:
                "modules/package_manager/package_manager_plugin.so"
            }
          ]
          and all(
            $process_artifacts.basecampBundleModules[];
            exact_object_keys(["moduleName", "relativePath", "sha256"])
              and (.sha256 | valid_sha256)
          )
          and (
            $process_artifacts.installedLgxModules
            | map(del(
                .packageSha256,
                .installedRootSha256,
                .mainFileSha256
              ))
          ) == [
            {
              moduleName: "delivery_module",
              packageFile: "logos-delivery_module-module-lib.lgx",
              mainFile: "delivery_module_plugin.so"
            },
            {
              moduleName: "lez_core",
              packageFile: "logos-lez_core-module-lib.lgx",
              mainFile: "lez_core_plugin.so"
            },
            {
              moduleName: "logos_palace_ui",
              packageFile: "logos-logos_palace_ui-module.lgx",
              mainFile: "logos_palace_ui_plugin.so"
            },
            {
              moduleName: "palace_core",
              packageFile: "logos-palace_core-module-lib.lgx",
              mainFile: "palace_core_plugin.so"
            },
            {
              moduleName: "palace_vm",
              packageFile: "logos-palace_vm-module-lib.lgx",
              mainFile: "palace_vm_plugin.so"
            },
            {
              moduleName: "storage_module",
              packageFile: "logos-storage_module-module-lib.lgx",
              mainFile: "storage_module_plugin.so"
            }
          ]
          and all(
            $process_artifacts.installedLgxModules[];
            . as $module
            | exact_object_keys([
                "moduleName",
                "packageFile",
                "packageSha256",
                "installedRootSha256",
                "mainFile",
                "mainFileSha256"
              ])
              and (.packageSha256 | valid_sha256)
              and (.installedRootSha256 | valid_sha256)
              and (.mainFileSha256 | valid_sha256)
              and any(
                $process_package_hashes[];
                .file == $module.packageFile
                  and .sha256 == $module.packageSha256
              )
          )
          and .processModel.observationMethod
            == "bounded /proc exact process inventory, pinned runtime artifacts, and owned TCP LISTEN proof"
          and (.processModel.observations | type) == "array"
          and (.processModel.observations | length) > 0
          and all(
            .processModel.observations[];
            . as $observation
            | .scope
              == "recursive descendants plus matching Basecamp process group/session"
              and .standalonePalaceServerScanScope
                == "all same-effective-UID processes visible in bounded /proc scan"
              and .inventoryContract == {
                basecampMain: "LogosBasecamp",
                coreModuleHosts: [
                  "capability_module",
                  "delivery_module",
                  "lez_core",
                  "package_downloader",
                  "package_manager",
                  "palace_core",
                  "palace_vm",
                  "storage_module"
                ],
                uiModuleHosts: ["logos_palace_ui"]
              }
              and .standalonePalaceServerMatches == []
              and .processCount == 10
              and (.processes | length) == .processCount
              and (
                .processes
                | map({role, moduleName, program})
                | sort_by(.role, .moduleName)
              ) == (
                [
                  {
                    role: "basecamp-main",
                    moduleName: null,
                    program: "LogosBasecamp"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "capability_module",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "delivery_module",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "lez_core",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "package_downloader",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "package_manager",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "palace_core",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "palace_vm",
                    program: "logos_host"
                  },
                  {
                    role: "core-module-host",
                    moduleName: "storage_module",
                    program: "logos_host"
                  },
                  {
                    role: "ui-module-host",
                    moduleName: "logos_palace_ui",
                    program: "ui-host"
                  }
                ]
                | sort_by(.role, .moduleName)
              )
              and (
                .processes
                | map(.pid)
                | unique
                | length
              ) == 10
              and any(
                .processes[];
                .pid == $observation.rootPid
                  and .role == "basecamp-main"
              )
              and all(
                .processes[];
                . as $process
                | (
                    $process_artifacts.basecampBundlePrograms
                    | map(select(.role == $process.role))
                  ) as $program_artifacts
                | (
                    [
                      (
                        $process_artifacts.basecampBundleModules[]
                        | {
                            moduleName,
                            sha256,
                            artifactBasename:
                              (.relativePath | split("/") | .[-1])
                          }
                      ),
                      (
                        $process_artifacts.installedLgxModules[]
                        | {
                            moduleName,
                            sha256: .mainFileSha256,
                            artifactBasename: .mainFile
                          }
                      )
                    ]
                    | map(select(
                        .moduleName == $process.moduleName
                      ))
                  ) as $module_artifacts
                | exact_object_keys([
                    "pid",
                    "name",
                    "executable",
                    "executableArgument",
                    "programArgument",
                    "executableSha256",
                    "executableArgumentSha256",
                    "programArgumentSha256",
                    "executableFileIdentity",
                    "programArgumentFileIdentity",
                    "programExecutableMapping",
                    "executionMode",
                    "loaderPath",
                    "directInterpreterPath",
                    "directInterpreterSha256",
                    "directInterpreterFileIdentity",
                    "directInterpreterMapping",
                    "moduleArgumentPath",
                    "moduleArgumentSha256",
                    "moduleArgumentFileIdentity",
                    "moduleArtifactPath",
                    "moduleArtifactSha256",
                    "moduleExecutableMapping",
                    "moduleName",
                    "role",
                    "program"
                  ])
                  and (.pid | valid_nonnegative_integer)
                  and .pid > 0
                  and (.name | type) == "string"
                  and (.executable | type) == "string"
                  and (.executableArgument | type) == "string"
                  and (.programArgument | type) == "string"
                  and (.executableSha256 | valid_sha256)
                  and (.executableArgumentSha256 | valid_sha256)
                  and (.programArgumentSha256 | valid_sha256)
                  and (.executableFileIdentity | valid_file_identity)
                  and (
                    .programArgumentFileIdentity
                    | valid_file_identity
                  )
                  and (
                    .programExecutableMapping.path
                    | type == "string" and startswith("/")
                  )
                  and (
                    .programExecutableMapping.path
                    | split("/")
                    | .[-1]
                  ) == .programArgument
                  and (
                    .programExecutableMapping
                    | valid_executable_mapping(
                        $process.programExecutableMapping.path;
                        $process.programArgumentFileIdentity
                      )
                  )
                  and (
                    (
                      $loader_selection.mode == "fallback"
                      and .executionMode == "fallback"
                      and .loaderPath
                        == $loader_selection.canonicalPath
                      and .executable
                        == $loader_selection.executableBasename
                      and .executableArgument
                        == $loader_selection.argumentBasename
                      and .executableSha256
                        == $loader_selection.sha256
                      and .executableArgumentSha256
                        == $loader_selection.sha256
                      and .executableFileIdentity == {
                        device: $loader_selection.device,
                        inode: $loader_selection.inode
                      }
                      and .directInterpreterPath == null
                      and .directInterpreterSha256 == null
                      and .directInterpreterFileIdentity == null
                      and .directInterpreterMapping == null
                    )
                    or
                    (
                      $loader_selection.mode == "direct"
                      and .executionMode == "direct"
                      and .loaderPath == null
                      and (
                        .executableArgument
                        | test("^ld(?:-[a-z0-9_-]+)?-linux[^/]*\\.so(?:\\.[0-9]+)*$"; "i")
                        | not
                      )
                      and .executable == .programArgument
                      and .executableArgument == .programArgument
                      and .executableSha256
                        == .programArgumentSha256
                      and .executableArgumentSha256
                        == .programArgumentSha256
                      and .executableFileIdentity
                        == .programArgumentFileIdentity
                      and .directInterpreterPath
                        == $loader_selection.canonicalPath
                      and .directInterpreterSha256
                        == $loader_selection.sha256
                      and .directInterpreterFileIdentity == {
                        device: $loader_selection.device,
                        inode: $loader_selection.inode
                      }
                      and (
                        .directInterpreterMapping
                        | valid_executable_mapping(
                            $process.directInterpreterPath;
                            $process.directInterpreterFileIdentity
                          )
                      )
                    )
                  )
                  and ($program_artifacts | length) == 1
                  and .programArgument == $program_artifacts[0].argument
                  and .programArgumentSha256
                    == $program_artifacts[0].sha256
                  and (
                    (
                      .moduleName == null
                      and .moduleArgumentPath == null
                      and .moduleArgumentSha256 == null
                      and .moduleArgumentFileIdentity == null
                      and .moduleArtifactPath == null
                      and .moduleArtifactSha256 == null
                      and .moduleExecutableMapping == null
                      and ($module_artifacts | length) == 0
                    )
                    or
                    (
                      .moduleName != null
                      and ($module_artifacts | length) == 1
                      and (.moduleArgumentPath | type) == "string"
                      and (.moduleArgumentPath | startswith("/"))
                      and .moduleArgumentPath == .moduleArtifactPath
                      and (
                        .moduleArgumentPath
                        | split("/")
                        | .[-1]
                      ) == $module_artifacts[0].artifactBasename
                      and (.moduleArgumentSha256 | valid_sha256)
                      and .moduleArgumentSha256
                        == $module_artifacts[0].sha256
                      and (
                        .moduleArgumentFileIdentity
                        | valid_file_identity
                      )
                      and (.moduleArtifactSha256 | valid_sha256)
                      and .moduleArtifactSha256
                        == $module_artifacts[0].sha256
                      and (
                        .moduleExecutableMapping
                        | valid_executable_mapping(
                            $process.moduleArtifactPath;
                            $process.moduleArgumentFileIdentity
                          )
                      )
                    )
                  )
              )
              and .tcpListenerProof.scope
                == "TCP LISTEN sockets owned by exact Basecamp process inventory"
              and .tcpListenerProof.expectedOnly == true
              and (.tcpListenerProof.listenerCount
                | valid_nonnegative_integer)
              and .tcpListenerProof.listenerCount > 0
              and (.tcpListenerProof.listeners | type) == "array"
              and (.tcpListenerProof.listeners | length)
                == .tcpListenerProof.listenerCount
              and (
                (.tcpListenerProof.listeners | map(.purpose) | sort)
                  == ["qml-inspector"]
                or
                (.tcpListenerProof.listeners | map(.purpose) | sort)
                  == [
                    "delivery-transport",
                    "qml-inspector",
                    "storage-transport"
                  ]
              )
              and (
                .tcpListenerProof.listeners
                | map([.protocol, .address, .port, .ownerPid])
                | unique
                | length
              ) == .tcpListenerProof.listenerCount
              and all(
                .tcpListenerProof.listeners[];
                . as $listener
                | .protocol == "tcp4"
                  and (
                    (.purpose == "qml-inspector"
                      and .address == "127.0.0.1"
                      and .ownerRole == "basecamp-main"
                      and .moduleName == null)
                    or
                    (.purpose == "delivery-transport"
                      and .address == "127.0.0.1"
                      and .ownerRole == "core-module-host"
                      and .moduleName == "delivery_module")
                    or
                    (.purpose == "storage-transport"
                      and .address == "0.0.0.0"
                      and .ownerRole == "core-module-host"
                      and .moduleName == "storage_module")
                  )
                  and (.port | valid_nonnegative_integer)
                  and .port >= 1024
                  and .port <= 65535
                  and (.ownerPid | valid_nonnegative_integer)
                  and .ownerPid > 0
                  and any(
                    $observation.processes[];
                    .pid == $listener.ownerPid
                      and .role == $listener.ownerRole
                      and .moduleName == $listener.moduleName
                  )
              )
          )
          and (
            .metrics.applicationRoundTrip
            | palace_valid_application_round_trip
          )
          and (.metrics.recovery.fullRebuildMs
            | valid_nonnegative_integer)
          and (.metrics.recovery.coldHistoryStorageVmRebuildMs
            | valid_nonnegative_integer)
          and (.metrics.recovery.deliveryReconnectMs
            | valid_nonnegative_integer)
          and (.metrics.frameTiming.runs | keys | sort)
            == ["aInitial", "bInitial", "bRestart", "cInitial", "cRestart"];

      def valid_gate4_release_evidence($gate3):
        . as $gate4
        | .release as $release
        | (
            $release
            | exact_object_keys([
            "programDeployment",
            "rootAccountBeforeWrites",
            "gate3Preflight",
            "gate3PreflightSha256",
            "revalidation"
          ])
          )
          and $release.gate3Preflight == $gate3.releasePreflight
          and ($release.gate3PreflightSha256 | valid_sha256)
          and $release.programDeployment
            == $gate3.releasePreflight.programDeployment
          and (
            $release.rootAccountBeforeWrites
            | exact_object_keys([
              "status",
              "state",
              "explorerOrigin",
              "path",
              "accountIdBase58",
              "programOwner",
              "balance",
              "nonce",
              "dataBytes",
              "dataSha256",
              "responseSha256"
            ])
          )
          and $release.rootAccountBeforeWrites.status == "passed"
          and (
            $release.rootAccountBeforeWrites.state == "uninitialized"
            or $release.rootAccountBeforeWrites.state == "initialized"
          )
          and $release.rootAccountBeforeWrites.explorerOrigin
            == "https://explorer.testnet.lez.logos.co"
          and $release.rootAccountBeforeWrites.path
            == "/api/get_account3022937127152978530"
          and $release.rootAccountBeforeWrites.accountIdBase58
            == "2H9vVPVToHwyHer6e7dAUGA7ToQmi4HSRscjounkkig9"
          and ($release.rootAccountBeforeWrites.balance
            | valid_nonnegative_integer)
          and ($release.rootAccountBeforeWrites.nonce
            | valid_nonnegative_integer)
          and ($release.rootAccountBeforeWrites.dataBytes
            | valid_nonnegative_integer)
          and ($release.rootAccountBeforeWrites.dataSha256 | valid_sha256)
          and (
            $release.rootAccountBeforeWrites.responseSha256
            | valid_sha256
          )
          and (
            $release.revalidation
            | exact_object_keys([
              "status",
              "gate3CompletedAtUnixMs",
              "gate4CompletedAtUnixMs",
              "gate3AgeAtRevalidationMs",
              "exactDeploymentMatch",
              "exactRootAccountMatch",
              "rootAdvancedByGate4"
            ])
          )
          and $release.revalidation.status == "passed"
          and ($release.revalidation.gate3CompletedAtUnixMs
            | valid_nonnegative_integer)
          and ($release.revalidation.gate4CompletedAtUnixMs
            | valid_nonnegative_integer)
          and ($release.revalidation.gate3AgeAtRevalidationMs
            | valid_nonnegative_integer)
          and $release.revalidation.gate4CompletedAtUnixMs
            - $release.revalidation.gate3CompletedAtUnixMs
            == $release.revalidation.gate3AgeAtRevalidationMs
          and $release.revalidation.exactDeploymentMatch == true
          and (
            (
              $release.revalidation.exactRootAccountMatch == true
              and $release.revalidation.rootAdvancedByGate4 == false
              and $release.rootAccountBeforeWrites
                == $gate3.releasePreflight.rootAccountBeforeWrites
            )
            or (
              $release.revalidation.exactRootAccountMatch == false
              and $release.revalidation.rootAdvancedByGate4 == true
              and $release.rootAccountBeforeWrites.state == "initialized"
              and $release.rootAccountBeforeWrites.accountIdBase58
                == $gate3.releasePreflight
                  .rootAccountBeforeWrites.accountIdBase58
            )
          )
          and $gate4.releaseContract == {
            protocols: {
              palaceSchema: "palace-schema-v3",
              deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
              storageCatalog: "logos-palace-mvp-storage-catalog-v1",
              catalogManifest: "logos-palace-catalog-manifest-v1",
              roomMetadata: "logos-palace-room-v1",
              propMetadata: "logos-palace-prop-v1",
              palaceManifestKind: "palace_manifest",
              vmProfile: "iptscrae_mvp_v1"
            },
            network: {
              productionDeliveryEnvelopeNetworkId:
                "logos-lez-testnet-v0.2.0",
              deliveryTransport: "direct-entry-node-test-topology",
              sharedFleetUsed: false,
              storageTopology: "private-loopback-bootstrap-mesh",
              lezNetworkId: "logos-lez-testnet-v0.2.0",
              lezModuleApiVersion: "0.4.0-alpha.2",
              lezModuleRevision:
                "e8d84103660604b1a6a06ddd66d20da7a2fdeb3f",
              lezRuntimeRevision:
                "e923315c020d4966807849f9db10536b628d5739",
              lezSchemaId: "palace-schema-v3",
              lezPublicContractRevision:
                "2b67563baf590c32dd82e50e3252815ec56bdaec",
              lezProgramIdHex:
                "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61",
              lezProgramBytecodeSha256:
                "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171",
              lezSequencerOrigin: "https://testnet.lez.logos.co",
              lezReadOrigin: "https://explorer.testnet.lez.logos.co"
            }
          }
          and $gate4.releaseContract.network.lezModuleRevision
            == $gate4.dependencyRevisions.lez_core.revision
          and ($gate4 | valid_screenshot_evidence)
          and ($gate4 | valid_gate4_behaviors);

      def expected_actions($prop_requested):
        [
          {actionId: "0", caller: "a", kind: "initialize"},
          {actionId: "1", caller: "b", kind: "register_user"},
          {actionId: "2", caller: "c", kind: "register_user"},
          {actionId: "3", caller: "a", kind: "grant_capability"},
          {actionId: "4", caller: "a", kind: "create_shared_state"},
          {actionId: "5", caller: "b", kind: "set_room_locked"},
          {actionId: "6", caller: "b", kind: "set_room_locked"},
          {actionId: "7", caller: "b", kind: "update_shared_state"},
          {actionId: "8", caller: "b", kind: "create_user_ban"}
        ] + (
          if $prop_requested
          then [
            {actionId: "9", caller: "b", kind: "create_asset_ban"},
            {actionId: "10", caller: "b", kind: "update_shared_state"}
          ]
          else [
            {actionId: "9", caller: "b", kind: "update_shared_state"}
          ]
          end
        );

      def valid_finalized_actions:
        . as $report
        | .plan.actions as $plan
        | .actions as $actions
        | ($report.plan.propStory == "requested") as $prop_requested
        | expected_actions($prop_requested) as $expected_actions
        | ($plan | type) == "array"
          and ($actions | type) == "array"
          and ($plan | length) == ($expected_actions | length)
          and ($actions | length) == ($expected_actions | length)
          and $report.plan.doorActionId
            == (if $prop_requested then "10" else "9" end)
          and (
            $plan | map({actionId, caller, kind})
          ) == $expected_actions
          and (
            $actions | map(.actionId)
          ) == ($expected_actions | map(.actionId))
          and all($plan[]; .transitionSha256 | valid_sha256)
          and ($actions | map(.transactionHash) | unique | length)
            == ($expected_actions | length)
          and all(
            $actions[];
            (.transactionHash | valid_sha256)
              and .transactionHash != ("0" * 64)
          )
          and (
            [range(0; ($expected_actions | length))]
            | all(
                .[];
                . as $index
                | $actions[$index] as $action
                | $plan[$index] as $planned
                | $report.actionJournals[$action.caller]
                    as $final_journal
                | $final_journal.records[$action.actionId]
                    as $final_record
                | $action.actionId == $planned.actionId
                  and $action.caller == $planned.caller
                  and $action.kind == $planned.kind
                  and $action.transitionSha256
                    == $planned.transitionSha256
                  and $action.status == "finalized"
                  and $action.durableStatus == "finalized"
                  and (
                    $action.finalStatus | type == "string"
                    and contains("durable=finalized")
                  )
                  and $action.callerAccountId
                    == $report.identities[$action.caller].accountId
                  and $action.journal.file == "action-journal-v2"
                  and $action.journal.stage == 4
                  and $action.journal.durableStage == "finalized"
                  and $action.journal.deliveryPublished == false
                  and $action.journal.transactionHash
                    == $action.transactionHash
                  and ($action.journal.recordSha256 | valid_sha256)
                  and $final_journal.file == "action-journal-v2"
                  and ($final_journal.journalFileSha256 | valid_sha256)
                  and $final_record.stage == 4
                  and $final_record.durableStage == "finalized"
                  and $final_record.deliveryPublished == false
                  and $final_record.transactionHash
                    == $action.transactionHash
                  and ($final_record.recordSha256 | valid_sha256)
              )
          );

      def valid_gate1_core:
        .rooms.atriumBackground
          == {state: "placeholder", source: ""}
          and .rooms.loungeBackground
            == {state: "placeholder", source: ""}
          and .rooms.restoredBackground == .rooms.loungeBackground
          and (.persistedProjection.checksum | valid_sha256)
          and (
            .persistedProjection.state
            | type == "string"
              and contains("version=1;room=lounge;")
          )
          and all(
            [.timings.initialRenderMs,
             .timings.roomTransitionMs,
             .timings.restartRenderMs][];
            valid_nonnegative_integer
          )
          and (
            .screenshots | map(.file)
            == ["atrium.png", "lounge.png", "lounge-restored.png"]
          )
          and all(
            .screenshots[];
            (.width | valid_nonnegative_integer)
              and .width > 0
              and (.height | valid_nonnegative_integer)
              and .height > 0
              and (.sha256 | valid_sha256)
          );

      def valid_gate2_core:
        . as $report
        | (
            $report.lgxPackages
            | map(select(
                .file == "logos-palace_core-module-lib.lgx"
              ))
            | .[0]
          ) as $installed_core
        | (
            $report.productionLgxPackages
            | map(select(
                .file == "logos-palace_core-module-lib.lgx"
              ))
            | .[0]
          ) as $production_core
        | $report.runtimeVariants == {
            palaceCore: {
              kind: "test-only-acceptance-fixtures",
              file: "logos-palace_core-module-lib.lgx",
              runtimeOutput: "palace-core-acceptance-lgx",
              productionSha256: $production_core.sha256,
              installedSha256: $installed_core.sha256
            }
          }
          and ($production_core.sha256 | valid_sha256)
          and ($installed_core.sha256 | valid_sha256)
          and $production_core.sha256
            != $installed_core.sha256
          and (
            $report.lgxPackages
            | map(select(
                .file != "logos-palace_core-module-lib.lgx"
              ))
          ) == (
            $report.productionLgxPackages
            | map(select(
                .file != "logos-palace_core-module-lib.lgx"
              ))
          )
          and $report.acceptanceContract == {
            deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
            deliveryNetworkId: "logos.test"
          }
          and .orderedSpeech.count == 300
          and .orderedSpeech.perSenderCount
            == {a: 100, b: 100, c: 100}
          and (.orderedSpeech.receipts | length) == 300
          and (
            .orderedSpeech.sendToReceiveLatency.allNodes.sampleCount
            == 300
          )
          and (
            .orderedSpeech.sendToReceiveLatency.observations | length
          ) == 300
          and (
            .orderedSpeech.baselineStability
            | exact_object_keys([
                "minimumQuietWindowMs",
                "observedQuietWindowMs",
                "statusProbe",
                "sessionProbe"
              ])
          )
          and .orderedSpeech.baselineStability.minimumQuietWindowMs
            == 2000
          and (
            .orderedSpeech.baselineStability.observedQuietWindowMs
            | valid_nonnegative_integer
          )
          and .orderedSpeech.baselineStability.observedQuietWindowMs
            >= .orderedSpeech.baselineStability.minimumQuietWindowMs
          and (
            .orderedSpeech.baselineStability.statusProbe
            | exact_object_keys(["a", "b", "c"])
          )
          and (
            .orderedSpeech.baselineStability.sessionProbe
            | exact_object_keys(["a", "b", "c"])
          )
          and (
            ["a", "b", "c"] as $labels
            | (
                [
                  $labels[] as $sender
                  | $report.orderedSpeech.sequenceEvidence[$sender]
                      .delta
                ]
                | add
              ) as $accepted_delta
            | ($accepted_delta | valid_nonnegative_integer)
              and (
                $report.orderedSpeech.sequenceEvidence
                | exact_object_keys($labels)
              )
              and (
                [
                  $labels[] as $sender
                  | $report.orderedSpeech.sessionAfter[$sender]
                      .senderKey
                ]
                | unique
                | length
              ) == 3
              and all(
                $labels[];
                . as $sender
                | (
                    $report.orderedSpeech.sessionBefore[$sender]
                  ) as $before
                | (
                    $report.orderedSpeech.sessionAfter[$sender]
                  ) as $after
                | (
                    $report.orderedSpeech.baselineStability
                      .sessionProbe[$sender]
                  ) as $probe
                | (
                    $after.egressSequence - $before.egressSequence
                  ) as $delta
                | ($before.senderKey | type) == "string"
                  and ($before.senderKey | length) > 0
                  and $after.senderKey == $before.senderKey
                  and $probe.senderKey == $before.senderKey
                  and $probe.egressSequence == $before.egressSequence
                  and $probe.ingressSequences == $before.ingressSequences
                  and (
                    $before.egressSequence
                    | valid_nonnegative_integer
                  )
                  and (
                    $after.egressSequence
                    | valid_nonnegative_integer
                  )
                  and ($delta | valid_nonnegative_integer)
                  and $delta
                    >= $report.orderedSpeech.perSenderCount[$sender]
                  and (
                    $report.orderedSpeech.sequenceEvidence[$sender]
                    == {
                      delta: $delta,
                      speechCount:
                        $report.orderedSpeech.perSenderCount[$sender],
                      interleavedPresenceCount:
                        (
                          $delta
                          - $report.orderedSpeech
                              .perSenderCount[$sender]
                        )
                    }
                  )
              )
              and all(
                $labels[];
                . as $receiver
                | (
                    $report.orderedSpeech.statusBefore[$receiver]
                  ) as $before
                | (
                    $report.orderedSpeech.snapshots[$receiver].status
                  ) as $after
                | (
                    $report.orderedSpeech.baselineStability
                      .statusProbe[$receiver]
                  ) as $probe
                | all(
                    [
                      $probe.received_accepted,
                      $probe.received_rejected,
                      $probe.rejected_scope,
                      $probe.rejected_expired,
                      $probe.rejected_signature,
                      $probe.rejected_replay,
                      $probe.rejected_payload,
                      $probe.rejected_other,
                      $probe.outbox,
                      $probe.correlated,
                      $before.received_accepted,
                      $before.received_rejected,
                      $before.rejected_scope,
                      $before.rejected_expired,
                      $before.rejected_signature,
                      $before.rejected_replay,
                      $before.rejected_payload,
                      $before.rejected_other,
                      $before.outbox,
                      $before.correlated,
                      $after.received_accepted,
                      $after.received_rejected,
                      $after.rejected_scope,
                      $after.rejected_expired,
                      $after.rejected_signature,
                      $after.rejected_replay,
                      $after.rejected_payload,
                      $after.rejected_other,
                      $after.outbox,
                      $after.correlated
                    ][];
                    valid_nonnegative_integer
                  )
                  and (
                    ($probe | {
                      received_accepted,
                      received_rejected,
                      rejected_scope,
                      rejected_expired,
                      rejected_signature,
                      rejected_replay,
                      rejected_payload,
                      rejected_other,
                      outbox,
                      correlated
                    })
                    == ($before | {
                      received_accepted,
                      received_rejected,
                      rejected_scope,
                      rejected_expired,
                      rejected_signature,
                      rejected_replay,
                      rejected_payload,
                      rejected_other,
                      outbox,
                      correlated
                    })
                  )
                  and $before.outbox == 0
                  and $before.correlated == 0
                  and (
                    $after.received_accepted
                    == $before.received_accepted + $accepted_delta
                  )
                  and $after.received_rejected
                    == $before.received_rejected
                  and $after.rejected_scope == $before.rejected_scope
                  and $after.rejected_expired == $before.rejected_expired
                  and $after.rejected_signature
                    == $before.rejected_signature
                  and $after.rejected_replay == $before.rejected_replay
                  and $after.rejected_payload == $before.rejected_payload
                  and $after.rejected_other == $before.rejected_other
                  and $after.outbox == 0
                  and $after.correlated == 0
                  and all(
                    $labels[];
                    . as $sender
                    | (
                        (
                          $report.orderedSpeech.sessionBefore[$receiver]
                            .ingressSequences[
                              $report.orderedSpeech
                                .sessionBefore[$sender].senderKey
                            ]
                          == $report.orderedSpeech.sessionBefore[$sender]
                            .egressSequence
                        )
                        and (
                          $report.orderedSpeech.sessionAfter[$receiver]
                            .ingressSequences[
                              $report.orderedSpeech
                                .sessionAfter[$sender].senderKey
                            ]
                          == $report.orderedSpeech.sessionAfter[$sender]
                            .egressSequence
                        )
                      )
                  )
              )
          )
          and all(
            [.orderedSpeech.sendToReceiveLatency.allNodes.p50Ms,
             .orderedSpeech.sendToReceiveLatency.allNodes.p95Ms,
             .orderedSpeech.sendToReceiveLatency.allNodes.maxMs,
             .timings.restartRecoveryMs][];
            valid_nonnegative_integer
          )
          and .orderedSpeech.sendToReceiveLatency.allNodes.p50Ms
            <= .orderedSpeech.sendToReceiveLatency.allNodes.p95Ms
          and .orderedSpeech.sendToReceiveLatency.allNodes.p95Ms
            <= .orderedSpeech.sendToReceiveLatency.allNodes.maxMs
          and .renderProbe.local.location == "local"
          and .renderProbe.remote.b.location == "remote"
          and .renderProbe.remote.c.location == "remote"
          and all(
            [.renderProbe.local,
             .renderProbe.remote.b,
             .renderProbe.remote.c][];
            (.actionToFramebufferCaptureMs
              | valid_nonnegative_integer)
              and (.screenshot.sha256 | valid_sha256)
              and .screenshot.width > 0
              and .screenshot.height > 0
          )
          and .negativeCoverage
            == {
              coreContractSeams: "executed",
              rawNetworkPackets: "executed-compiled-canned-sender"
            }
          and all(
            [.exactUiBeforeRestart.a,
             .exactUiBeforeRestart.b,
             .exactUiBeforeRestart.c,
             .restartRecovery.exactUi.a,
             .restartRecovery.exactUi.b,
             .restartRecovery.exactUi.c][];
            .participantIds == ["alice", "bob", "carol"]
          )
          and .restartRecovery.sessionAfter.egressSequence
            > .restartRecovery.sessionBefore.egressSequence
          and .restartRecovery.launch.crash.signal == "SIGKILL"
          and .restartRecovery.launch.crash.exitCode == null
          and .restartRecovery.launch.crash.graceful == false
          and (
            .restartRecovery.launch.crash.previousPid
            | valid_nonnegative_integer
          )
          and (
            .restartRecovery.launch.basecampPid
            | valid_nonnegative_integer
          )
          and .restartRecovery.launch.crash.previousPid
            != .restartRecovery.launch.basecampPid;

      .[0] as $candidate
      | (.[1] // null) as $reference
      | (.[2] // null) as $gate3_evidence
      | ($candidate | basecamp) as $candidate_basecamp
      | ($candidate | packages) as $candidate_packages
      | (
          if $gate == "gate1" then
            $candidate.schema == "logos-palace-basecamp-gate1-report-v1"
              and $candidate.result == "PASS"
              and $candidate.cleanup == {
                status: "passed",
                failures: []
              }
              and ($candidate | valid_gate1_core)
          elif $gate == "gate2" then
            $candidate.schema == "logos-palace-basecamp-gate2-report-v1"
              and $candidate.result == "PASS"
              and $candidate.cleanup == {
                status: "passed",
                failures: []
              }
              and ($candidate | valid_gate2_core)
          elif $gate == "gate3" then
            $candidate.schema == "logos.palace.basecamp-gate3-report"
              and $candidate.version == 1
              and $candidate.status == "passed"
              and $candidate.fullGate3 == "passed"
              and $candidate.cleanup == {
                status: "passed",
                failures: []
              }
              and $candidate.productionIdentityMode == true
              and ($candidate | valid_gate3_release_evidence)
          elif $gate == "gate4" then
            $candidate.schema == "logos.palace.basecamp-gate4-6-report"
              and $candidate.version == 2
              and $candidate.status == "passed"
              and $candidate.fullGate4 == "passed"
              and $candidate.fullGate5 == "passed"
              and $candidate.fullGate6 == "passed"
              and $candidate.noPalaceServer == "passed"
              and $candidate.cleanup == {
                status: "passed",
                failures: []
              }
              and $candidate.gate3.reportSha256 == $gate3_sha256
              and ($gate3_evidence | valid_gate3_release_evidence)
              and (
                $candidate
                | valid_gate4_release_evidence($gate3_evidence)
              )
              and $candidate.metrics.lezMeasurementBoundaries == {
                submitMs:
                  "worker Date.now at first submit-capable client invocation start to worker Date.now at first response proving durable submitted_to_lez acceptance or a later durable stage",
                observeMs:
                  "durable submission acceptance response to first client response proving observed or finalized, using worker Date.now timestamps",
                finalityMs:
                  "first client response proving observed to first client response proving finalized, using worker Date.now timestamps; measured-coalesced means one response first proved both and duration is exactly zero",
                totalMs:
                  "first submit-capable client invocation start to first client response proving finalized, using worker Date.now timestamps"
              }
              and ($candidate | valid_finalized_actions)
          else
            false
          end
        )
        and $candidate.productSnapshot == $snapshot
        and $candidate.sourceCommit == $source_commit
        and $candidate.productSnapshotNarHash == $snapshot_nar_hash
        and $candidate.productSnapshotNarSize == $snapshot_nar_size
        and $candidate.snapshotRunnerSha256 == $runner_sha256
        and $candidate.runtimeOutputManifestSha256
          == $runtime_manifest_sha256
        and (
          $gate != "gate4"
          or $candidate.dependencyRevisions == $dependencies
        )
        and $candidate_basecamp.revision == $basecamp_revision
        and ($candidate_basecamp.sha256 | valid_sha256)
        and ($candidate | valid_packages)
        and (
          $reference == null
          or (
            $candidate_basecamp == ($reference | basecamp)
            and $candidate_packages == ($reference | packages)
          )
        )
    ' "${reports[@]}" >/dev/null 2>&1; then
    return 1
  fi
  if ! process_scope_evidence_passes "${gate}" "${report}"; then
    return 1
  fi
  if [ "${gate}" = "gate4" ]; then
    "${death_coupled_node[@]}" \
      "${product_snapshot}/tests/validate_gate4_artifacts.mjs" \
      "${report}" "$(dirname "${report}")" >/dev/null 2>&1
  fi
}

run_or_skip_gate() {
  local gate="$1"
  local report="$2"
  shift 2
  if gate_report_passes "${gate}" "${report}"; then
    printf 'Skipping validated compiled Basecamp %s\n' "${gate}"
    return
  fi
  if [ -L "${report}" ] \
    || { [ -e "${report}" ] && [ ! -f "${report}" ]; }; then
    fail_run "${gate}" "gate report path is not a secure regular file"
  fi
  run_gate "${gate}" "$@"
  if ! gate_report_passes "${gate}" "${report}"; then
    fail_run "${gate}" "gate report failed strict validation"
  fi
}

prior_run_complete() {
  local prior_run="$1"
  local prior_marker="${prior_run}/product-snapshot"
  local prior_gate0="${prior_run}/gate0/gate0-report.json"
  local prior_gate1="${prior_run}/gate1/gate1-report.json"
  local prior_gate2="${prior_run}/gate2/gate2-report.json"
  local prior_gate3="${prior_run}/gate3/gate3-report.json"
  local prior_gate4="${prior_run}/gate4/gate4-report.json"
  local prior_gate1_scope="${prior_run}/gate1/process-scope.json"
  local prior_gate2_scope="${prior_run}/gate2/process-scope.json"
  local prior_gate3_scope="${prior_run}/gate3/process-scope.json"
  local prior_gate4_scope="${prior_run}/gate4/process-scope.json"
  local prior_compiled="${prior_run}/compiled-mvp-report.json"
  local prior_runtime_manifest="${prior_run}/runtime-output-manifest.json"
  local prior_basecamp_revision
  local prior_sandbox_output
  local prior_snapshot
  local prior_gate0_sha256
  local prior_gate1_sha256
  local prior_gate2_sha256
  local prior_gate3_sha256
  local prior_gate4_sha256
  local prior_gate1_scope_sha256
  local prior_gate2_scope_sha256
  local prior_gate3_scope_sha256
  local prior_gate4_scope_sha256
  local prior_runtime_manifest_sha256
  local prior_dir
  local prior_file
  local gate1_report
  local gate3_report
  local product_snapshot
  local basecamp_rev
  local -a prior_snapshots

  if [ -L "${prior_run}" ] \
    || [ ! -d "${prior_run}" ] \
    || [ "$(realpath -e -- "${prior_run}")" != "${prior_run}" ] \
    || [ "$(stat -c '%a' "${prior_run}")" != "700" ] \
    || [ -L "${prior_marker}" ] \
    || [ ! -f "${prior_marker}" ] \
    || [ "$(stat -c '%a' "${prior_marker}")" != "600" ]; then
    return 1
  fi
  for prior_dir in \
    "${prior_run}/gate0" \
    "${prior_run}/gate1" \
    "${prior_run}/gate2" \
    "${prior_run}/gate3" \
    "${prior_run}/gate4"; do
    if [ -L "${prior_dir}" ] \
      || [ ! -d "${prior_dir}" ] \
      || [ "$(realpath -e -- "${prior_dir}")" != "${prior_dir}" ]; then
      return 1
    fi
  done
  for prior_file in \
    "${prior_gate0}" \
    "${prior_gate1}" \
    "${prior_gate2}" \
    "${prior_gate3}" \
    "${prior_gate4}" \
    "${prior_gate1_scope}" \
    "${prior_gate2_scope}" \
    "${prior_gate3_scope}" \
    "${prior_gate4_scope}" \
    "${prior_compiled}" \
    "${prior_runtime_manifest}"; do
    if [ -L "${prior_file}" ] || [ ! -f "${prior_file}" ]; then
      return 1
    fi
  done
  mapfile -t prior_snapshots <"${prior_marker}"
  if [ "${#prior_snapshots[@]}" -ne 1 ]; then
    return 1
  fi
  prior_snapshot="${prior_snapshots[0]}"
  if ! valid_product_snapshot "${prior_snapshot}"; then
    return 1
  fi
  prior_basecamp_revision="$(
    "${jq_bin}" -er '.nodes.basecamp.locked.rev' \
      "${prior_snapshot}/flake.lock" 2>/dev/null
  )" || return 1
  prior_sandbox_output="$(
    "${jq_bin}" -er \
      '.sandboxTestOutput | select(type == "string")' \
      "${prior_gate0}" 2>/dev/null
  )" || return 1
  if ! valid_store_path "${prior_sandbox_output}"; then
    return 1
  fi

  gate1_report="${prior_gate1}"
  gate3_report="${prior_gate3}"
  product_snapshot="${prior_snapshot}"
  basecamp_rev="${prior_basecamp_revision}"
  if ! process_scope_evidence_passes "gate1" "${prior_gate1}" \
    || ! process_scope_evidence_passes "gate2" "${prior_gate2}" \
    || ! gate_report_passes "gate3" "${prior_gate3}" \
    || ! gate_report_passes "gate4" "${prior_gate4}"; then
    return 1
  fi

  prior_gate0_sha256="$(report_sha256 "${prior_gate0}")"
  prior_gate1_sha256="$(report_sha256 "${prior_gate1}")"
  prior_gate2_sha256="$(report_sha256 "${prior_gate2}")"
  prior_gate3_sha256="$(report_sha256 "${prior_gate3}")"
  prior_gate4_sha256="$(report_sha256 "${prior_gate4}")"
  prior_gate1_scope_sha256="$(report_sha256 "${prior_gate1_scope}")"
  prior_gate2_scope_sha256="$(report_sha256 "${prior_gate2_scope}")"
  prior_gate3_scope_sha256="$(report_sha256 "${prior_gate3_scope}")"
  prior_gate4_scope_sha256="$(report_sha256 "${prior_gate4_scope}")"
  prior_runtime_manifest_sha256="$(
    report_sha256 "${prior_runtime_manifest}"
  )"
  "${jq_bin}" -s -e \
    --arg snapshot "${prior_snapshot}" \
    --arg basecamp_revision "${prior_basecamp_revision}" \
    --arg sandbox_output "${prior_sandbox_output}" \
    --arg gate0_sha256 "${prior_gate0_sha256}" \
    --arg gate1_sha256 "${prior_gate1_sha256}" \
    --arg gate2_sha256 "${prior_gate2_sha256}" \
    --arg gate3_sha256 "${prior_gate3_sha256}" \
    --arg gate4_sha256 "${prior_gate4_sha256}" \
    --arg gate1_scope_sha256 "${prior_gate1_scope_sha256}" \
    --arg gate2_scope_sha256 "${prior_gate2_scope_sha256}" \
    --arg gate3_scope_sha256 "${prior_gate3_scope_sha256}" \
    --arg gate4_scope_sha256 "${prior_gate4_scope_sha256}" \
    --arg runtime_manifest_sha256 "${prior_runtime_manifest_sha256}" \
    --slurpfile runtime_manifest "${prior_runtime_manifest}" \
    '
      def basecamp:
        if (.basecamp | type) == "object" then
          {
            revision: .basecamp.revision,
            sha256: .basecamp.sha256
          }
        else
          {
            revision: .basecampRevision,
            sha256: .basecampBinarySha256
          }
        end;

      def packages:
        (.productionLgxPackages
          // .lgxPackages
          // .packageHashes
          // [])
        | map({file: .file, sha256: .sha256})
        | sort_by(.file);

      def valid_sha256:
        type == "string" and test("^[0-9a-f]{64}$");

      def valid_packages:
        packages as $packages
        | ($packages | length) == 6
          and ($packages | map(.file) | unique | length) == 6
          and (
            ($packages | map(.file))
            == [
              "logos-delivery_module-module-lib.lgx",
              "logos-lez_core-module-lib.lgx",
              "logos-logos_palace_ui-module.lgx",
              "logos-palace_core-module-lib.lgx",
              "logos-palace_vm-module-lib.lgx",
              "logos-storage_module-module-lib.lgx"
            ]
          )
          and all($packages[]; .sha256 | valid_sha256);

      def expected_actions($prop_requested):
        [
          {actionId: "0", caller: "a", kind: "initialize"},
          {actionId: "1", caller: "b", kind: "register_user"},
          {actionId: "2", caller: "c", kind: "register_user"},
          {actionId: "3", caller: "a", kind: "grant_capability"},
          {actionId: "4", caller: "a", kind: "create_shared_state"},
          {actionId: "5", caller: "b", kind: "set_room_locked"},
          {actionId: "6", caller: "b", kind: "set_room_locked"},
          {actionId: "7", caller: "b", kind: "update_shared_state"},
          {actionId: "8", caller: "b", kind: "create_user_ban"}
        ] + (
          if $prop_requested
          then [
            {actionId: "9", caller: "b", kind: "create_asset_ban"},
            {actionId: "10", caller: "b", kind: "update_shared_state"}
          ]
          else [
            {actionId: "9", caller: "b", kind: "update_shared_state"}
          ]
          end
        );

      def valid_finalized_actions:
        . as $report
        | .plan.actions as $plan
        | .actions as $actions
        | ($report.plan.propStory == "requested") as $prop_requested
        | expected_actions($prop_requested) as $expected_actions
        | ($plan | type) == "array"
          and ($actions | type) == "array"
          and ($plan | length) == ($expected_actions | length)
          and ($actions | length) == ($expected_actions | length)
          and $report.plan.doorActionId
            == (if $prop_requested then "10" else "9" end)
          and (
            ($plan | map({actionId, caller, kind}))
            == $expected_actions
          )
          and (
            ($actions | map(.actionId))
            == ($expected_actions | map(.actionId))
          )
          and all($plan[]; .transitionSha256 | valid_sha256)
          and ($actions | map(.transactionHash) | unique | length)
            == ($expected_actions | length)
          and all(
            $actions[];
            (.transactionHash | valid_sha256)
              and .transactionHash != ("0" * 64)
          )
          and (
            [range(0; ($expected_actions | length))]
            | all(
                .[];
                . as $index
                | $actions[$index] as $action
                | $plan[$index] as $planned
                | $report.actionJournals[$action.caller]
                    as $final_journal
                | $final_journal.records[$action.actionId]
                    as $final_record
                | $action.actionId == $planned.actionId
                  and $action.caller == $planned.caller
                  and $action.kind == $planned.kind
                  and $action.transitionSha256
                    == $planned.transitionSha256
                  and $action.status == "finalized"
                  and $action.durableStatus == "finalized"
                  and (
                    $action.finalStatus | type == "string"
                    and contains("durable=finalized")
                  )
                  and $action.callerAccountId
                    == $report.identities[$action.caller].accountId
                  and $action.journal.file == "action-journal-v2"
                  and $action.journal.stage == 4
                  and $action.journal.durableStage == "finalized"
                  and $action.journal.deliveryPublished == false
                  and $action.journal.transactionHash
                    == $action.transactionHash
                  and ($action.journal.recordSha256 | valid_sha256)
                  and $final_journal.file == "action-journal-v2"
                  and ($final_journal.journalFileSha256 | valid_sha256)
                  and $final_record.stage == 4
                  and $final_record.durableStage == "finalized"
                  and $final_record.deliveryPublished == false
                  and $final_record.transactionHash
                    == $action.transactionHash
                  and ($final_record.recordSha256 | valid_sha256)
              )
          );

      .[0] as $gate0
      | .[1] as $gate1
      | .[2] as $gate2
      | .[3] as $gate3
      | .[4] as $gate4
      | .[5] as $compiled
      | $runtime_manifest[0] as $runtime_outputs
      | ($gate1 | basecamp) as $expected_basecamp
      | ($gate1 | packages) as $expected_packages
      | $gate0.schema == "logos.palace.basecamp-gate0-report"
        and $gate0.version == 1
        and $gate0.status == "passed"
        and $gate0.check == "sandbox-test"
        and $gate0.productSnapshot == $snapshot
        and $gate0.basecampRevision == $basecamp_revision
        and $gate0.basecampRuntimeOutput == "basecamp"
        and {
          narHash: $gate0.basecampNarHash,
          narSize: $gate0.basecampNarSize
        } == (
          $runtime_outputs.outputs[]
          | select(.name == "basecamp")
          | {narHash, narSize}
        )
        and ($gate0.sandboxTestNarHash | type) == "string"
        and (
          $gate0.sandboxTestNarHash
          | test("^sha256-[A-Za-z0-9+/]{43}=$")
        )
        and ($gate0.sandboxTestNarSize | type) == "number"
        and $gate0.sandboxTestNarSize > 0
        and $gate0.sandboxTestNarSize <= 9007199254740991
        and ($gate0.sandboxTestNarSize | floor)
          == $gate0.sandboxTestNarSize
        and $gate0.sandboxTestOutput == $sandbox_output
        and $gate1.schema == "logos-palace-basecamp-gate1-report-v1"
        and $gate1.result == "PASS"
        and $gate1.cleanup == {
          status: "passed",
          failures: []
        }
        and $gate2.schema == "logos-palace-basecamp-gate2-report-v1"
        and $gate2.result == "PASS"
        and $gate2.cleanup == {
          status: "passed",
          failures: []
        }
        and $gate3.schema == "logos.palace.basecamp-gate3-report"
        and $gate3.version == 1
        and $gate3.status == "passed"
        and $gate3.fullGate3 == "passed"
        and $gate3.cleanup == {
          status: "passed",
          failures: []
        }
        and $gate3.productionIdentityMode == true
        and ($gate3.identities | type) == "object"
        and all(
          [$gate3.identities.a, $gate3.identities.b, $gate3.identities.c][];
          (.accountId | valid_sha256)
            and (.deliveryKey | valid_sha256)
            and (.registrationTransaction | valid_sha256)
        )
        and (
          [$gate3.identities.a.accountId,
           $gate3.identities.b.accountId,
           $gate3.identities.c.accountId]
          | unique | length
        ) == 3
        and (
          [$gate3.identities.a.deliveryKey,
           $gate3.identities.b.deliveryKey,
           $gate3.identities.c.deliveryKey]
          | unique | length
        ) == 3
        and (
          [$gate3.identities.a.registrationTransaction,
           $gate3.identities.b.registrationTransaction,
           $gate3.identities.c.registrationTransaction]
          | unique | length
        ) == 3
        and ($gate3.storageConfigs | type) == "object"
        and $gate4.schema == "logos.palace.basecamp-gate4-6-report"
        and $gate4.version == 2
        and $gate4.status == "passed"
        and $gate4.fullGate4 == "passed"
        and $gate4.fullGate5 == "passed"
        and $gate4.fullGate6 == "passed"
        and $gate4.noPalaceServer == "passed"
        and $gate4.cleanup == {
          status: "passed",
          failures: []
        }
        and $gate4.gate3.reportSha256 == $gate3_sha256
        and $gate4.metrics.lezMeasurementBoundaries == {
          submitMs:
            "worker Date.now at first submit-capable client invocation start to worker Date.now at first response proving durable submitted_to_lez acceptance or a later durable stage",
          observeMs:
            "durable submission acceptance response to first client response proving observed or finalized, using worker Date.now timestamps",
          finalityMs:
            "first client response proving observed to first client response proving finalized, using worker Date.now timestamps; measured-coalesced means one response first proved both and duration is exactly zero",
          totalMs:
            "first submit-capable client invocation start to first client response proving finalized, using worker Date.now timestamps"
        }
        and ($gate4 | valid_finalized_actions)
        and all(
          [$gate1, $gate2, $gate3, $gate4][];
          .productSnapshot == $snapshot
            and .sourceCommit == $gate1.sourceCommit
            and .productSnapshotNarHash
              == $gate1.productSnapshotNarHash
            and .productSnapshotNarSize
              == $gate1.productSnapshotNarSize
            and .snapshotRunnerSha256
              == $gate1.snapshotRunnerSha256
            and .runtimeOutputManifestSha256
              == $gate1.runtimeOutputManifestSha256
            and ((. | basecamp) == $expected_basecamp)
            and ((. | packages) == $expected_packages)
            and (. | valid_packages)
        )
        and $gate0.sourceCommit == $gate1.sourceCommit
        and $gate0.productSnapshotNarHash
          == $gate1.productSnapshotNarHash
        and $gate0.productSnapshotNarSize
          == $gate1.productSnapshotNarSize
        and $gate0.snapshotRunnerSha256
          == $gate1.snapshotRunnerSha256
        and $gate0.runtimeOutputManifestSha256
          == $gate1.runtimeOutputManifestSha256
        and ($gate1.sourceCommit | type) == "string"
        and ($gate1.sourceCommit | test("^[0-9a-f]{40}$"))
        and ($gate1.productSnapshotNarHash | type) == "string"
        and (
          $gate1.productSnapshotNarHash
          | test("^sha256-[A-Za-z0-9+/]{43}=$")
        )
        and ($gate1.productSnapshotNarSize | type) == "number"
        and $gate1.productSnapshotNarSize > 0
        and $gate1.productSnapshotNarSize <= 67108864
        and ($gate1.productSnapshotNarSize | floor)
          == $gate1.productSnapshotNarSize
        and ($gate1.snapshotRunnerSha256 | valid_sha256)
        and ($gate1.runtimeOutputManifestSha256 | valid_sha256)
        and $expected_basecamp.revision == $basecamp_revision
        and ($expected_basecamp.sha256 | valid_sha256)
        and $compiled.schema
          == "logos.palace.basecamp-mvp-compiled-report"
        and $compiled.version == 1
        and $compiled.status == "passed"
        and $compiled.fullMvp == "passed"
        and $compiled.productSnapshot == $snapshot
        and $compiled.sourceCommit == $gate1.sourceCommit
        and $compiled.productSnapshotNarHash
          == $gate1.productSnapshotNarHash
        and $compiled.productSnapshotNarSize
          == $gate1.productSnapshotNarSize
        and $compiled.snapshotRunnerSha256
          == $gate1.snapshotRunnerSha256
        and $compiled.runtimeOutputs.manifestSha256
          == $gate1.runtimeOutputManifestSha256
        and $compiled.scope.implementedGates
          == ["gate0", "gate1", "gate2", "gate3", "gate4", "gate5", "gate6"]
        and $compiled.scope.pendingGates == []
        and $compiled.basecamp.revision == $expected_basecamp.revision
        and $compiled.basecamp.sha256 == $expected_basecamp.sha256
        and $compiled.basecamp.sandboxTestOutput == $sandbox_output
        and $compiled.basecamp.runtimeOutput == "basecamp"
        and (
          $compiled.basecamp | {narHash, narSize}
        ) == (
          $runtime_outputs.outputs[]
          | select(.name == "basecamp")
          | {narHash, narSize}
        )
        and $compiled.basecamp.sandboxTestNarHash
          == $gate0.sandboxTestNarHash
        and $compiled.basecamp.sandboxTestNarSize
          == $gate0.sandboxTestNarSize
        and $compiled.packageHashes == $expected_packages
        and $compiled.dependencyRevisions
          == $gate4.dependencyRevisions
        and (
          $compiled.dependencyRevisions | keys | sort
        ) == ["basecamp", "delivery_module", "lez_core", "storage_module"]
        and all(
          $compiled.dependencyRevisions[];
          (.revision | type) == "string"
            and (.revision | test("^[0-9a-f]{40}$"))
            and (.narHash | type) == "string"
            and (.narHash | test("^sha256-[A-Za-z0-9+/]{43}=$"))
        )
        and $compiled.runtimeOutputs.manifest
          == "runtime-output-manifest.json"
        and $compiled.runtimeOutputs.manifestSha256
          == $runtime_manifest_sha256
        and $runtime_outputs.schema
          == "logos.palace.runtime-output-manifest"
        and $runtime_outputs.version == 1
        and ($runtime_outputs.outputs | type) == "array"
        and ($runtime_outputs.outputs | length) == 13
        and (
          $runtime_outputs.outputs | map(.name) | sort
        ) == [
          "acceptance-tools",
          "basecamp",
          "delivery-module-lgx",
          "lez-core-lgx",
          "palace-core-acceptance-lgx",
          "palace-core-contracts",
          "palace-core-lgx",
          "palace-ui-lgx",
          "palace-vm-contracts",
          "palace-vm-lgx",
          "qt-mcp",
          "release-verifier",
          "storage-module-lgx"
        ]
        and all(
          $runtime_outputs.outputs[];
          (keys | sort) == (["name", "narHash", "narSize"] | sort)
            and (.narHash | type) == "string"
            and (.narHash | test("^sha256-[A-Za-z0-9+/]{43}=$"))
            and (.narSize | type) == "number"
            and .narSize > 0
            and .narSize <= 9007199254740991
            and (.narSize | floor) == .narSize
        )
        and (
          $runtime_outputs.outputs[]
          | select(.name == "release-verifier")
          | {narHash, narSize}
        ) == $compiled.releaseVerifier
        and $compiled.contractProofs
          .acceptedSubmissionCrashRecovery.status == "passed"
        and $compiled.contractProofs
          .acceptedSubmissionCrashRecovery.tests == [
            "terminal_recovery_states_require_a_nonfinal_durable_action",
            "core_lez_repairs_durable_coordinator_after_accept_to_journal_crash"
          ]
        and $compiled.contractProofs
          .acceptedSubmissionCrashRecovery.runtimeOutput
          == "palace-core-contracts"
        and (
          $compiled.contractProofs.acceptedSubmissionCrashRecovery
          | {narHash, narSize}
        ) == (
          $runtime_outputs.outputs[]
          | select(.name == "palace-core-contracts")
          | {narHash, narSize}
        )
        and $compiled.contractProofs.coldReplay.status == "passed"
        and $compiled.contractProofs.coldReplay.tests == [
          "core_vm_finalized_replay_builds_one_exact_idempotent_plan",
          "core_vm_finalized_replay_fails_closed_without_exact_evidence"
        ]
        and $compiled.contractProofs.coldReplay.runtimeOutput
          == "palace-core-contracts"
        and (
          $compiled.contractProofs.coldReplay
          | {narHash, narSize}
        ) == (
          $runtime_outputs.outputs[]
          | select(.name == "palace-core-contracts")
          | {narHash, narSize}
        )
        and $compiled.contractProofs.palaceVm.status == "passed"
        and $compiled.contractProofs.palaceVm.tests == [
          "shared_door_navigation_emits_only_after_finalized_replay",
          "tracked_finality_rejects_wrong_action_receipt_script_and_state",
          "exact_finality_promotion_is_one_shot_and_restart_safe",
          "untracked_finalized_compatibility_api_cannot_navigate"
        ]
        and $compiled.contractProofs.palaceVm.runtimeOutput
          == "palace-vm-contracts"
        and (
          $compiled.contractProofs.palaceVm
          | {narHash, narSize}
        ) == (
          $runtime_outputs.outputs[]
          | select(.name == "palace-vm-contracts")
          | {narHash, narSize}
        )
        and (
          [
            $compiled.gates.gate0,
            $compiled.gates.gate1,
            $compiled.gates.gate2,
            $compiled.gates.gate3,
            $compiled.gates.gate4,
            $compiled.gates.gate5,
            $compiled.gates.gate6
          ]
          | all(.[]; .status == "passed")
        )
        and $compiled.gates.gate0.check == "sandbox-test"
        and $compiled.gates.gate0.output == $sandbox_output
        and $compiled.gates.gate0.report == "gate0/gate0-report.json"
        and $compiled.gates.gate0.reportSha256 == $gate0_sha256
        and $compiled.gates.gate1.report == "gate1/gate1-report.json"
        and $compiled.gates.gate1.reportSha256 == $gate1_sha256
        and $compiled.gates.gate1.processScope == {
          evidence: "gate1/process-scope.json",
          evidenceSha256: $gate1_scope_sha256
        }
        and $compiled.gates.gate2.report == "gate2/gate2-report.json"
        and $compiled.gates.gate2.reportSha256 == $gate2_sha256
        and $compiled.gates.gate2.processScope == {
          evidence: "gate2/process-scope.json",
          evidenceSha256: $gate2_scope_sha256
        }
        and $compiled.gates.gate3.report == "gate3/gate3-report.json"
        and $compiled.gates.gate3.reportSha256 == $gate3_sha256
        and $compiled.gates.gate3.processScope == {
          evidence: "gate3/process-scope.json",
          evidenceSha256: $gate3_scope_sha256
        }
        and $compiled.gates.gate4.report == "gate4/gate4-report.json"
        and $compiled.gates.gate5.report == "gate4/gate4-report.json"
        and $compiled.gates.gate6.report == "gate4/gate4-report.json"
        and $compiled.gates.gate4.reportSha256 == $gate4_sha256
        and $compiled.gates.gate5.reportSha256 == $gate4_sha256
        and $compiled.gates.gate6.reportSha256 == $gate4_sha256
        and $compiled.gates.gate4.processScope == {
          evidence: "gate4/process-scope.json",
          evidenceSha256: $gate4_scope_sha256
        }
        and $compiled.gates.gate5.processScope
          == $compiled.gates.gate4.processScope
        and $compiled.gates.gate6.processScope
          == $compiled.gates.gate4.processScope
    ' \
    "${prior_gate0}" \
    "${prior_gate1}" \
    "${prior_gate2}" \
    "${prior_gate3}" \
    "${prior_gate4}" \
    "${prior_compiled}" >/dev/null 2>&1
}

finalize_completed_run() {
  local completion_path
  local completion_status
  local public_status

  invalidate_public_evidence
  if ! prior_run_complete "${run_dir}"; then
    printf '%s\n' \
      'Completed claim report or referenced evidence failed reopening validation' \
      >&2
    exit 1
  fi

  set +e
  completion_path="$(
    "${death_coupled_node[@]}" "${claim_tool}" completion \
      "${run_dir}" "${product_snapshot}" "${snapshot_gc_root}" \
      "${source_commit}" "${snapshot_nar_hash}" "${snapshot_nar_size}" \
      "${snapshot_runner_sha256}" "${runtime_manifest}" \
      "${runtime_manifest_sha256}" \
      "${process_scope_slice}" "${process_scope_prefix}"
  )"
  completion_status=$?
  set -e
  if [ "${completion_status}" -ne 0 ] \
    || [ "${completion_path}" != "${claim_completion}" ]; then
    invalidate_public_evidence
    printf 'Completed claim attestation failed\n' >&2
    exit 1
  fi

  set +e
  "${death_coupled_node[@]}" \
    "${product_snapshot}/tests/build_public_evidence.mjs" \
    "${run_dir}" "${public_evidence}"
  public_status=$?
  set -e
  if [ "${public_status}" -ne 0 ]; then
    invalidate_public_evidence
    printf 'Public MVP evidence finalization failed\n' >&2
    exit 1
  fi

  printf 'Compiled MVP passed\n'
  printf 'Compiled MVP report: %s\n' "${compiled_report}"
  printf 'Public MVP evidence: %s\n' "${public_evidence}"
  exit 0
}

if [ "${claim_completed}" -eq 1 ]; then
  finalize_completed_run
fi

if [ "${resuming}" -eq 0 ]; then
  shopt -s nullglob
  prior_runs=("${runs_root}"/run.????????)
  shopt -u nullglob
  for prior_run in "${prior_runs[@]}"; do
    if [ "${prior_run}" = "${run_dir}" ]; then
      continue
    fi
    prior_gate3="${prior_run}/gate3"
    prior_gate4="${prior_run}/gate4"
    if [ -L "${prior_gate3}" ] || [ -e "${prior_gate3}" ]; then
      if prior_run_complete "${prior_run}"; then
        prior_state="completed reuse-only"
      else
        prior_state="unfinished"
      fi
      fail_run "resume-required" \
        "${prior_state} production Gate 3 evidence requires explicit resume: ${prior_run}" \
        "${prior_run}"
    fi
    if [ -L "${prior_gate4}" ] || [ -e "${prior_gate4}" ]; then
      fail_run "resume-required" \
        "Gate 4 evidence without Gate 3 requires explicit inspection: ${prior_run}" \
        "${prior_run}"
    fi
  done
fi

gate0_dir="${run_dir}/gate0"
gate0_report="${gate0_dir}/gate0-report.json"
gate0_output_file="${gate0_dir}/sandbox-output.txt"
gate1_dir="${run_dir}/gate1"
gate1_report="${gate1_dir}/gate1-report.json"
gate2_dir="${run_dir}/gate2"
gate2_report="${gate2_dir}/gate2-report.json"
gate3_dir="${run_dir}/gate3"
gate3_report="${gate3_dir}/gate3-report.json"
gate4_dir="${run_dir}/gate4"
gate4_report="${gate4_dir}/gate4-report.json"

secure_gate_dir "${gate0_dir}"
if [ -L "${gate0_report}" ] \
  || { [ -e "${gate0_report}" ] && [ ! -f "${gate0_report}" ]; } \
  || [ -L "${gate0_output_file}" ] \
  || { [ -e "${gate0_output_file}" ] \
    && [ ! -f "${gate0_output_file}" ]; }; then
  fail_run "gate0" "Gate 0 artifact path is not a secure regular file"
fi
temporary_gate0_output="$(
  mktemp "${gate0_dir}/.sandbox-output.XXXXXXXX"
)"
set +e
nix build --no-link --print-out-paths \
  "${basecamp_ref}#sandbox-test" >"${temporary_gate0_output}"
gate0_status=$?
set -e
chmod 600 "${temporary_gate0_output}"
mv -- "${temporary_gate0_output}" "${gate0_output_file}"
if [ "${gate0_status}" -ne 0 ]; then
  fail_run "gate0" \
    "pinned Basecamp sandbox-test exited with status ${gate0_status}"
fi
mapfile -t gate0_outputs <"${gate0_output_file}"
if [ "${#gate0_outputs[@]}" -ne 1 ] \
  || ! valid_store_path "${gate0_outputs[0]}"; then
  fail_run "gate0" "sandbox-test did not produce one canonical Nix output"
fi
expected_sandbox_test_output="${gate0_outputs[0]}"
if [ -e "${sandbox_test_gc_root}" ] \
  || [ -L "${sandbox_test_gc_root}" ]; then
  if [ ! -L "${sandbox_test_gc_root}" ] \
    || [ "$(realpath -e -- "${sandbox_test_gc_root}" 2>/dev/null || true)" \
      != "${expected_sandbox_test_output}" ]; then
    fail_run "gate0" \
      "sandbox-test GC root differs from pinned derivation output"
  fi
else
  nix-store --add-root "${sandbox_test_gc_root}" --indirect \
    -r "${expected_sandbox_test_output}" >/dev/null
fi
if [ ! -L "${sandbox_test_gc_root}" ] \
  || [ "$(realpath -e -- "${sandbox_test_gc_root}" 2>/dev/null || true)" \
    != "${expected_sandbox_test_output}" ]; then
  fail_run "gate0" \
    "sandbox-test output was not retained by exact per-run GC root"
fi
sandbox_test_info="$(
  nix path-info --json --json-format 1 "${expected_sandbox_test_output}"
)"
sandbox_test_nar_hash="$(
  "${jq_bin}" -er --arg output "${expected_sandbox_test_output}" \
    '.[$output].narHash
      | select(type == "string")
      | select(test("^sha256-[A-Za-z0-9+/]{43}=$"))' \
    <<<"${sandbox_test_info}"
)"
sandbox_test_nar_size="$(
  "${jq_bin}" -er --arg output "${expected_sandbox_test_output}" \
    '.[$output].narSize
      | select(type == "number")
      | select(. > 0 and . <= 9007199254740991 and floor == .)' \
    <<<"${sandbox_test_info}"
)"
if gate0_report_passes "${gate0_report}"; then
  printf 'Skipping validated compiled Basecamp gate0\n'
else
  "${acceptance_tools}/bin/sync" -f "${gate0_dir}"
  temporary_gate0_report="$(
    mktemp "${gate0_dir}/.gate0-report.XXXXXXXX"
  )"
  "${jq_bin}" -n \
    --arg snapshot "${product_snapshot}" \
    --arg source_commit "${source_commit}" \
    --arg snapshot_nar_hash "${snapshot_nar_hash}" \
    --argjson snapshot_nar_size "${snapshot_nar_size}" \
    --arg runner_sha256 "${snapshot_runner_sha256}" \
    --arg runtime_manifest_sha256 "${runtime_manifest_sha256}" \
    --arg basecamp_revision "${basecamp_rev}" \
    --arg basecamp_nar_hash "${basecamp_nar_hash}" \
    --argjson basecamp_nar_size "${basecamp_nar_size}" \
    --arg sandbox_test_nar_hash "${sandbox_test_nar_hash}" \
    --argjson sandbox_test_nar_size "${sandbox_test_nar_size}" \
    --arg output "${expected_sandbox_test_output}" \
    '{
      schema: "logos.palace.basecamp-gate0-report",
      version: 1,
      status: "passed",
      check: "sandbox-test",
      productSnapshot: $snapshot,
      sourceCommit: $source_commit,
      productSnapshotNarHash: $snapshot_nar_hash,
      productSnapshotNarSize: $snapshot_nar_size,
      snapshotRunnerSha256: $runner_sha256,
      runtimeOutputManifestSha256: $runtime_manifest_sha256,
      basecampRevision: $basecamp_revision,
      basecampRuntimeOutput: "basecamp",
      basecampNarHash: $basecamp_nar_hash,
      basecampNarSize: $basecamp_nar_size,
      sandboxTestNarHash: $sandbox_test_nar_hash,
      sandboxTestNarSize: $sandbox_test_nar_size,
      sandboxTestOutput: $output
    }' >"${temporary_gate0_report}"
  chmod 600 "${temporary_gate0_report}"
  mv -- "${temporary_gate0_report}" "${gate0_report}"
  if ! gate0_report_passes "${gate0_report}"; then
    fail_run "gate0" "sandbox-test report failed strict validation"
  fi
fi
sandbox_test_output="${expected_sandbox_test_output}"
sandbox_test_nar_hash="$(
  "${jq_bin}" -er '.sandboxTestNarHash' "${gate0_report}"
)"
sandbox_test_nar_size="$(
  "${jq_bin}" -er '.sandboxTestNarSize' "${gate0_report}"
)"

secure_gate_dir "${gate1_dir}"
run_or_skip_gate "gate1" "${gate1_report}" \
  "PALACE_GATE1_PRODUCT_SNAPSHOT=${product_snapshot}" \
  "${acceptance_tools}/bin/bash" \
  -p \
  "${product_snapshot}/scripts/run-basecamp-gate1.sh" \
  "${gate1_dir}"

secure_gate_dir "${gate2_dir}"
run_or_skip_gate "gate2" "${gate2_report}" \
  "PALACE_GATE2_PRODUCT_SNAPSHOT=${product_snapshot}" \
  "PALACE_GATE2_ACCEPTANCE_CORE_LGX=${palace_core_acceptance_lgx}" \
  "${acceptance_tools}/bin/bash" \
  -p \
  "${product_snapshot}/scripts/run-basecamp-gate2.sh" \
  "${gate2_dir}"

set +e
entered_gate3_claim="$(
  "${death_coupled_node[@]}" "${claim_tool}" enter-gate3 \
    "${run_dir}" "${product_snapshot}" "${snapshot_gc_root}" \
    "${source_commit}" "${snapshot_nar_hash}" "${snapshot_nar_size}" \
    "${snapshot_runner_sha256}" "${runtime_manifest}" \
    "${runtime_manifest_sha256}" \
    "${process_scope_slice}" "${process_scope_prefix}"
)"
entered_gate3_status=$?
set -e
if [ "${entered_gate3_status}" -ne 0 ] \
  || [ "${entered_gate3_claim}" != "${active_claim_path}" ]; then
  fail_run "gate3-claim-transition" \
    "durable active-run claim did not enter Gate 3"
fi

if [ "${resuming}" -eq 1 ] \
  && ! gate_report_passes "gate3" "${gate3_report}"; then
  if [ -L "${gate4_dir}" ] || [ -e "${gate4_dir}" ]; then
    fail_run "resume-integrity" \
      "Gate 3 is invalid but this run already contains Gate 4 evidence"
  fi
  printf 'Resuming Gate 3 with preserved shared state: %s\n' \
    "${shared_state}"
fi

secure_gate_dir "${gate3_dir}"
run_or_skip_gate "gate3" "${gate3_report}" \
  "PALACE_GATE3_PRODUCT_SNAPSHOT=${product_snapshot}" \
  "PALACE_GATE3_STATE_DIR=${shared_state}" \
  "PALACE_GATE3_PRODUCTION_IDENTITIES=1" \
  "PALACE_E2E_ASSET_INPUT_ROOT=${canonical_asset_root}" \
  "PALACE_E2E_ASSET_MANIFEST=${canonical_asset_manifest}" \
  "${acceptance_tools}/bin/bash" \
  -p \
  "${product_snapshot}/scripts/run-basecamp-gate3.sh" \
  "${gate3_dir}"

secure_gate_dir "${gate4_dir}"
run_or_skip_gate "gate4" "${gate4_report}" \
  "PALACE_GATE4_PRODUCT_SNAPSHOT=${product_snapshot}" \
  "PALACE_GATE4_STATE_DIR=${shared_state}" \
  "${acceptance_tools}/bin/bash" \
  -p \
  "${product_snapshot}/scripts/run-basecamp-gate4.sh" \
  "${gate4_dir}" \
  "${gate3_report}"

if ! gate_report_passes "gate1" "${gate1_report}" \
  || ! gate_report_passes "gate2" "${gate2_report}" \
  || ! gate_report_passes "gate3" "${gate3_report}" \
  || ! gate_report_passes "gate4" "${gate4_report}"; then
  fail_run "report-validation" \
    "release preflight/revalidation or screenshot evidence is invalid"
fi

gate3_input_sha256="$(report_sha256 "${gate3_report}")"
if ! validation="$(
  "${jq_bin}" -s -e \
    --arg snapshot "${product_snapshot}" \
    --arg source_commit "${source_commit}" \
    --arg snapshot_nar_hash "${snapshot_nar_hash}" \
    --argjson snapshot_nar_size "${snapshot_nar_size}" \
    --arg runner_sha256 "${snapshot_runner_sha256}" \
    --arg runtime_manifest_sha256 "${runtime_manifest_sha256}" \
    --arg basecamp_revision "${basecamp_rev}" \
    --arg gate3_sha256 "${gate3_input_sha256}" \
    --argjson dependencies "${dependency_revisions}" \
    '
      def basecamp:
        if (.basecamp | type) == "object" then
          {
            revision: .basecamp.revision,
            sha256: .basecamp.sha256
          }
        else
          {
            revision: .basecampRevision,
            sha256: .basecampBinarySha256
          }
        end;

      def packages:
        (.productionLgxPackages
          // .lgxPackages
          // .packageHashes
          // [])
        | map({file: .file, sha256: .sha256})
        | sort_by(.file);

      def valid_sha256:
        type == "string" and test("^[0-9a-f]{64}$");

      def valid_packages:
        packages as $packages
        | ($packages | length) == 6
          and ($packages | map(.file) | unique | length) == 6
          and (
            ($packages | map(.file))
            == [
              "logos-delivery_module-module-lib.lgx",
              "logos-lez_core-module-lib.lgx",
              "logos-logos_palace_ui-module.lgx",
              "logos-palace_core-module-lib.lgx",
              "logos-palace_vm-module-lib.lgx",
              "logos-storage_module-module-lib.lgx"
            ]
          )
          and all(
            $packages[];
            (.file | type) == "string"
              and (.file | endswith(".lgx"))
              and (.sha256 | valid_sha256)
          );

      def expected_actions($prop_requested):
        [
          {actionId: "0", caller: "a", kind: "initialize"},
          {actionId: "1", caller: "b", kind: "register_user"},
          {actionId: "2", caller: "c", kind: "register_user"},
          {actionId: "3", caller: "a", kind: "grant_capability"},
          {actionId: "4", caller: "a", kind: "create_shared_state"},
          {actionId: "5", caller: "b", kind: "set_room_locked"},
          {actionId: "6", caller: "b", kind: "set_room_locked"},
          {actionId: "7", caller: "b", kind: "update_shared_state"},
          {actionId: "8", caller: "b", kind: "create_user_ban"}
        ] + (
          if $prop_requested
          then [
            {actionId: "9", caller: "b", kind: "create_asset_ban"},
            {actionId: "10", caller: "b", kind: "update_shared_state"}
          ]
          else [
            {actionId: "9", caller: "b", kind: "update_shared_state"}
          ]
          end
        );

      def valid_finalized_actions:
        . as $report
        | .plan.actions as $plan
        | .actions as $actions
        | ($report.plan.propStory == "requested") as $prop_requested
        | expected_actions($prop_requested) as $expected_actions
        | ($plan | type) == "array"
          and ($actions | type) == "array"
          and ($plan | length) == ($expected_actions | length)
          and ($actions | length) == ($expected_actions | length)
          and $report.plan.doorActionId
            == (if $prop_requested then "10" else "9" end)
          and (
            ($plan | map({actionId, caller, kind}))
            == $expected_actions
          )
          and (
            ($actions | map(.actionId))
            == ($expected_actions | map(.actionId))
          )
          and all($plan[]; .transitionSha256 | valid_sha256)
          and ($actions | map(.transactionHash) | unique | length)
            == ($expected_actions | length)
          and all(
            $actions[];
            (.transactionHash | valid_sha256)
              and .transactionHash != ("0" * 64)
          )
          and (
            [range(0; ($expected_actions | length))]
            | all(
                .[];
                . as $index
                | $actions[$index] as $action
                | $plan[$index] as $planned
                | $report.actionJournals[$action.caller]
                    as $final_journal
                | $final_journal.records[$action.actionId]
                    as $final_record
                | $action.actionId == $planned.actionId
                  and $action.caller == $planned.caller
                  and $action.kind == $planned.kind
                  and $action.transitionSha256
                    == $planned.transitionSha256
                  and $action.status == "finalized"
                  and $action.durableStatus == "finalized"
                  and (
                    $action.finalStatus | type == "string"
                    and contains("durable=finalized")
                  )
                  and $action.callerAccountId
                    == $report.identities[$action.caller].accountId
                  and $action.journal.file == "action-journal-v2"
                  and $action.journal.stage == 4
                  and $action.journal.durableStage == "finalized"
                  and $action.journal.deliveryPublished == false
                  and $action.journal.transactionHash
                    == $action.transactionHash
                  and ($action.journal.recordSha256 | valid_sha256)
                  and $final_journal.file == "action-journal-v2"
                  and ($final_journal.journalFileSha256 | valid_sha256)
                  and $final_record.stage == 4
                  and $final_record.durableStage == "finalized"
                  and $final_record.deliveryPublished == false
                  and $final_record.transactionHash
                    == $action.transactionHash
                  and ($final_record.recordSha256 | valid_sha256)
              )
          );

      .[0] as $gate1
      | .[1] as $gate2
      | .[2] as $gate3
      | .[3] as $gate4
      | [
          ($gate1 | basecamp),
          ($gate2 | basecamp),
          ($gate3 | basecamp),
          ($gate4 | basecamp)
        ] as $basecamps
      | [
          ($gate1 | packages),
          ($gate2 | packages),
          ($gate3 | packages),
          ($gate4 | packages)
        ] as $package_sets
      | if
          ($gate1.schema == "logos-palace-basecamp-gate1-report-v1")
          and ($gate1.result == "PASS")
          and ($gate1.cleanup == {
            status: "passed",
            failures: []
          })
          and ($gate2.schema == "logos-palace-basecamp-gate2-report-v1")
          and ($gate2.result == "PASS")
          and ($gate2.cleanup == {
            status: "passed",
            failures: []
          })
          and ($gate3.schema == "logos.palace.basecamp-gate3-report")
          and ($gate3.version == 1)
          and ($gate3.status == "passed")
          and ($gate3.fullGate3 == "passed")
          and ($gate3.cleanup == {
            status: "passed",
            failures: []
          })
          and ($gate3.productionIdentityMode == true)
          and ($gate4.schema == "logos.palace.basecamp-gate4-6-report")
          and ($gate4.version == 2)
          and ($gate4.status == "passed")
          and ($gate4.fullGate4 == "passed")
          and ($gate4.fullGate5 == "passed")
          and ($gate4.fullGate6 == "passed")
          and ($gate4.noPalaceServer == "passed")
          and ($gate4.cleanup == {
            status: "passed",
            failures: []
          })
          and ($gate4.gate3.reportSha256 == $gate3_sha256)
          and ($gate4.dependencyRevisions == $dependencies)
          and ($gate4.metrics.lezMeasurementBoundaries == {
            submitMs:
              "worker Date.now at first submit-capable client invocation start to worker Date.now at first response proving durable submitted_to_lez acceptance or a later durable stage",
            observeMs:
              "durable submission acceptance response to first client response proving observed or finalized, using worker Date.now timestamps",
            finalityMs:
              "first client response proving observed to first client response proving finalized, using worker Date.now timestamps; measured-coalesced means one response first proved both and duration is exactly zero",
            totalMs:
              "first submit-capable client invocation start to first client response proving finalized, using worker Date.now timestamps"
          })
          and ($gate4 | valid_finalized_actions)
          and all(
            [$gate1, $gate2, $gate3, $gate4][];
            .productSnapshot == $snapshot
              and .sourceCommit == $source_commit
              and .productSnapshotNarHash == $snapshot_nar_hash
              and .productSnapshotNarSize == $snapshot_nar_size
              and .snapshotRunnerSha256 == $runner_sha256
              and .runtimeOutputManifestSha256
                == $runtime_manifest_sha256
          )
          and all(
            $basecamps[];
            .revision == $basecamp_revision
              and (.sha256 | valid_sha256)
              and . == $basecamps[0]
          )
          and ($gate1 | valid_packages)
          and ($gate2 | valid_packages)
          and ($gate3 | valid_packages)
          and ($gate4 | valid_packages)
          and all($package_sets[]; . == $package_sets[0])
        then
          {
            basecamp: $basecamps[0],
            packageHashes: $package_sets[0]
          }
        else
          error(
            "gate schema/status/snapshot/Basecamp/package validation failed"
          )
        end
    ' \
    "${gate1_report}" \
    "${gate2_report}" \
    "${gate3_report}" \
    "${gate4_report}"
)"; then
  fail_run "report-validation" \
    "gate schema/status/snapshot/Basecamp/package validation failed"
fi

gate0_report_sha256="$(report_sha256 "${gate0_report}")"
gate1_report_sha256="$(report_sha256 "${gate1_report}")"
gate2_report_sha256="$(report_sha256 "${gate2_report}")"
gate3_report_sha256="$(report_sha256 "${gate3_report}")"
gate4_report_sha256="$(report_sha256 "${gate4_report}")"
gate1_scope_sha256="$(
  report_sha256 "${gate1_dir}/process-scope.json"
)"
gate2_scope_sha256="$(
  report_sha256 "${gate2_dir}/process-scope.json"
)"
gate3_scope_sha256="$(
  report_sha256 "${gate3_dir}/process-scope.json"
)"
gate4_scope_sha256="$(
  report_sha256 "${gate4_dir}/process-scope.json"
)"

temporary_report="$(mktemp "${run_dir}/.compiled-mvp-report.XXXXXXXX")"
"${jq_bin}" -n \
  --arg snapshot "${product_snapshot}" \
  --arg source_commit "${source_commit}" \
  --arg snapshot_nar_hash "${snapshot_nar_hash}" \
  --argjson snapshot_nar_size "${snapshot_nar_size}" \
  --arg runner_sha256 "${snapshot_runner_sha256}" \
  --arg sandbox_output "${sandbox_test_output}" \
  --arg gate0_sha256 "${gate0_report_sha256}" \
  --arg gate1_sha256 "${gate1_report_sha256}" \
  --arg gate2_sha256 "${gate2_report_sha256}" \
  --arg gate3_sha256 "${gate3_report_sha256}" \
  --arg gate4_sha256 "${gate4_report_sha256}" \
  --arg gate1_scope_sha256 "${gate1_scope_sha256}" \
  --arg gate2_scope_sha256 "${gate2_scope_sha256}" \
  --arg gate3_scope_sha256 "${gate3_scope_sha256}" \
  --arg gate4_scope_sha256 "${gate4_scope_sha256}" \
  --arg active_claim "${active_claim_path}" \
  --arg snapshot_gc_root "${snapshot_gc_root}" \
  --arg runtime_manifest_sha256 "${runtime_manifest_sha256}" \
  --arg release_verifier_nar_hash "${release_verifier_nar_hash}" \
  --argjson release_verifier_nar_size "${release_verifier_nar_size}" \
  --arg accepted_submission_contract_test_terminal "${accepted_submission_contract_test_terminal}" \
  --arg accepted_submission_contract_test_repair "${accepted_submission_contract_test_repair}" \
  --arg cold_replay_contract_test_builds "${cold_replay_contract_test_builds}" \
  --arg cold_replay_contract_test_fails_closed "${cold_replay_contract_test_fails_closed}" \
  --arg palace_vm_contract_test_navigation "${palace_vm_contract_test_navigation}" \
  --arg palace_vm_contract_test_rejection "${palace_vm_contract_test_rejection}" \
  --arg palace_vm_contract_test_idempotence "${palace_vm_contract_test_idempotence}" \
  --arg palace_vm_contract_test_compatibility "${palace_vm_contract_test_compatibility}" \
  --arg palace_core_contracts_nar_hash "${palace_core_contracts_nar_hash}" \
  --argjson palace_core_contracts_nar_size "${palace_core_contracts_nar_size}" \
  --arg palace_vm_contracts_nar_hash "${palace_vm_contracts_nar_hash}" \
  --argjson palace_vm_contracts_nar_size "${palace_vm_contracts_nar_size}" \
  --arg basecamp_nar_hash "${basecamp_nar_hash}" \
  --argjson basecamp_nar_size "${basecamp_nar_size}" \
  --arg sandbox_test_nar_hash "${sandbox_test_nar_hash}" \
  --argjson sandbox_test_nar_size "${sandbox_test_nar_size}" \
  --argjson dependencies "${dependency_revisions}" \
  --argjson validation "${validation}" \
  --slurpfile gate1_evidence "${gate1_report}" \
  --slurpfile gate2_evidence "${gate2_report}" \
  --slurpfile gate3_evidence "${gate3_report}" \
  --slurpfile gate4_evidence "${gate4_report}" \
  '{
    schema: "logos.palace.basecamp-mvp-compiled-report",
    version: 1,
    status: "passed",
    fullMvp: "passed",
    productSnapshot: $snapshot,
    sourceCommit: $source_commit,
    productSnapshotNarHash: $snapshot_nar_hash,
    productSnapshotNarSize: $snapshot_nar_size,
    snapshotRunnerSha256: $runner_sha256,
    scope: {
      implementedGates:
        ["gate0", "gate1", "gate2", "gate3", "gate4", "gate5", "gate6"],
      pendingGates: []
    },
    dependencyRevisions: $dependencies,
    releaseVerifier: {
      narHash: $release_verifier_nar_hash,
      narSize: $release_verifier_nar_size
    },
    runtimeOutputs: {
      manifest: "runtime-output-manifest.json",
      manifestSha256: $runtime_manifest_sha256
    },
    contractProofs: {
      acceptedSubmissionCrashRecovery: {
        status: "passed",
        tests: [
          $accepted_submission_contract_test_terminal,
          $accepted_submission_contract_test_repair
        ],
        runtimeOutput: "palace-core-contracts",
        narHash: $palace_core_contracts_nar_hash,
        narSize: $palace_core_contracts_nar_size
      },
      coldReplay: {
        status: "passed",
        tests: [
          $cold_replay_contract_test_builds,
          $cold_replay_contract_test_fails_closed
        ],
        runtimeOutput: "palace-core-contracts",
        narHash: $palace_core_contracts_nar_hash,
        narSize: $palace_core_contracts_nar_size
      },
      palaceVm: {
        status: "passed",
        tests: [
          $palace_vm_contract_test_navigation,
          $palace_vm_contract_test_rejection,
          $palace_vm_contract_test_idempotence,
          $palace_vm_contract_test_compatibility
        ],
        runtimeOutput: "palace-vm-contracts",
        narHash: $palace_vm_contracts_nar_hash,
        narSize: $palace_vm_contracts_nar_size
      }
    },
    basecamp: (
      $validation.basecamp
      + {
          sandboxTestOutput: $sandbox_output,
          runtimeOutput: "basecamp",
          narHash: $basecamp_nar_hash,
          narSize: $basecamp_nar_size,
          sandboxTestNarHash: $sandbox_test_nar_hash,
          sandboxTestNarSize: $sandbox_test_nar_size
        }
    ),
    packageHashes: $validation.packageHashes,
    sharedState: {
      users: "shared-state/users",
      gates: ["gate3", "gate4"],
      activeRunClaim: $active_claim,
      productSnapshotGcRoot: $snapshot_gc_root,
      runtimeGcRoots: "durable-claim-roots"
    },
    gates: {
      gate0: {
        status: "passed",
        check: "sandbox-test",
        output: $sandbox_output,
        report: "gate0/gate0-report.json",
        reportSha256: $gate0_sha256
      },
      gate1: {
        status: "passed",
        report: "gate1/gate1-report.json",
        reportSha256: $gate1_sha256,
        processScope: {
          evidence: "gate1/process-scope.json",
          evidenceSha256: $gate1_scope_sha256
        }
      },
      gate2: {
        status: "passed",
        report: "gate2/gate2-report.json",
        reportSha256: $gate2_sha256,
        processScope: {
          evidence: "gate2/process-scope.json",
          evidenceSha256: $gate2_scope_sha256
        }
      },
      gate3: {
        status: "passed",
        report: "gate3/gate3-report.json",
        reportSha256: $gate3_sha256,
        processScope: {
          evidence: "gate3/process-scope.json",
          evidenceSha256: $gate3_scope_sha256
        }
      },
      gate4: {
        status: "passed",
        report: "gate4/gate4-report.json",
        reportSha256: $gate4_sha256,
        processScope: {
          evidence: "gate4/process-scope.json",
          evidenceSha256: $gate4_scope_sha256
        }
      },
      gate5: {
        status: "passed",
        report: "gate4/gate4-report.json",
        reportSha256: $gate4_sha256,
        processScope: {
          evidence: "gate4/process-scope.json",
          evidenceSha256: $gate4_scope_sha256
        }
      },
      gate6: {
        status: "passed",
        report: "gate4/gate4-report.json",
        reportSha256: $gate4_sha256,
        processScope: {
          evidence: "gate4/process-scope.json",
          evidenceSha256: $gate4_scope_sha256
        }
      }
    },
    metrics: {
      render: {
        gate1QmlReadyMs: $gate1_evidence[0].timings,
        markedActionToFramebufferCapture: {
          localMs:
            $gate2_evidence[0].renderProbe.local
              .actionToFramebufferCaptureMs,
          remoteMs: {
            b:
              $gate2_evidence[0].renderProbe.remote.b
                .actionToFramebufferCaptureMs,
            c:
              $gate2_evidence[0].renderProbe.remote.c
                .actionToFramebufferCaptureMs
          },
          screenshots: {
            local: $gate2_evidence[0].renderProbe.local.screenshot,
            remoteB: $gate2_evidence[0].renderProbe.remote.b.screenshot,
            remoteC: $gate2_evidence[0].renderProbe.remote.c.screenshot
          }
        }
      },
      delivery: {
        orderedMessages: {
          total: $gate2_evidence[0].orderedSpeech.count,
          perSender: $gate2_evidence[0].orderedSpeech.perSenderCount
        },
        sendToReceive:
          $gate2_evidence[0].orderedSpeech.sendToReceiveLatency.allNodes,
        restartRecoveryMs: $gate2_evidence[0].timings.restartRecoveryMs
      },
      lez: {
        report: "gate4/gate4-report.json",
        jsonPointer: "/metrics/lezActions",
        measurementBoundaries:
          $gate4_evidence[0].metrics.lezMeasurementBoundaries,
        actions:
          ($gate4_evidence[0].metrics.lezActions
            | map({actionId, timings, timingMeasurement}))
      },
      storage: $gate4_evidence[0].metrics.storage,
      applicationRoundTrip: (
        $gate4_evidence[0].metrics.applicationRoundTrip
        | {
            clock,
            payloadSemantics,
            measurements:
              (.measurements
                | map_values({
                    payloadUtf8Bytes,
                    requestUtf8Bytes,
                    responseUtf8Bytes,
                    latency
                  }))
          }
      ),
      gate5Vm: $gate4_evidence[0].metrics.gate5Vm,
      recovery: $gate4_evidence[0].metrics.recovery,
      frameTiming: $gate4_evidence[0].metrics.frameTiming,
      palaceVmPeakMemoryKiB: {
        b:
          $gate4_evidence[0].metrics.processMemory.b
            .palaceVmHost.vmHwmKiB,
        c:
          $gate4_evidence[0].metrics.processMemory.c
            .palaceVmHost.vmHwmKiB
      }
    },
    metricEvidence: {
      gate1: {
        report: "gate1/gate1-report.json",
        reportSha256: $gate1_sha256
      },
      gate2: {
        report: "gate2/gate2-report.json",
        reportSha256: $gate2_sha256
      },
      gate3: {
        report: "gate3/gate3-report.json",
        reportSha256: $gate3_sha256
      },
      gate4To6: {
        report: "gate4/gate4-report.json",
        reportSha256: $gate4_sha256
      }
    },
    validation: {
      sameProductSnapshot: true,
      sameBasecamp: true,
      samePackageHashes: true,
      fullGate4To6: true,
      noPalaceServer: true,
      reportWrite: "durable-atomic"
    }
  }' >"${temporary_report}"
chmod 600 "${temporary_report}"
"${acceptance_tools}/bin/sync" -f "${temporary_report}"
mv -- "${temporary_report}" "${compiled_report}"
"${acceptance_tools}/bin/sync" -f "${run_dir}"

if ! prior_run_complete "${run_dir}"; then
  fail_run "compiled-report-validation" \
    "compiled report or referenced evidence failed reopening validation"
fi

invalidate_public_evidence
claim_completed=1
set +e
completed_claim="$(
  "${death_coupled_node[@]}" "${claim_tool}" complete \
    "${run_dir}" "${product_snapshot}" "${snapshot_gc_root}" \
    "${source_commit}" "${snapshot_nar_hash}" "${snapshot_nar_size}" \
    "${snapshot_runner_sha256}" "${runtime_manifest}" \
    "${runtime_manifest_sha256}" \
    "${process_scope_slice}" "${process_scope_prefix}"
)"
claim_completion_status=$?
set -e
if [ "${claim_completion_status}" -ne 0 ] \
  || [ "${completed_claim}" != "${active_claim_path}" ]; then
  invalidate_public_evidence
  printf 'MVP active-run claim completion failed\n' >&2
  exit 1
fi

finalize_completed_run
