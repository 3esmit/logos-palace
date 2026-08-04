#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifacts_dir="${1:-${repo_root}/.artifacts/basecamp-gate4}"
gate3_report="${2:-${PALACE_GATE4_GATE3_REPORT:-${repo_root}/.artifacts/basecamp-gate3/gate3-report.json}}"

if [ ! -f "${gate3_report}" ] || [ -L "${gate3_report}" ]; then
  printf 'Gate 4 requires a passing Gate 3 report: %s\n' \
    "${gate3_report}" >&2
  exit 1
fi
gate3_report="$(realpath -e -- "${gate3_report}" 2>/dev/null || true)"
if [ -z "${gate3_report}" ] || [ ! -f "${gate3_report}" ]; then
  printf 'Gate 3 report path is not canonical\n' >&2
  exit 1
fi

mkdir -p "${artifacts_dir}"
artifacts_dir="$(cd "${artifacts_dir}" && pwd)"

export PALACE_GATE4_GATE3_REPORT_PATH="${gate3_report}"
gate3_snapshot="$(
  nix eval --raw --impure --expr \
    'let report = builtins.fromJSON (builtins.readFile (builtins.getEnv "PALACE_GATE4_GATE3_REPORT_PATH")); in report.productSnapshot'
)"
unset PALACE_GATE4_GATE3_REPORT_PATH
canonical_gate3_snapshot="$(
  realpath -e -- "${gate3_snapshot}" 2>/dev/null || true
)"
if [ "${gate3_snapshot}" != "${canonical_gate3_snapshot}" ] \
  || [[ ! "${canonical_gate3_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
  || [ ! -d "${canonical_gate3_snapshot}" ] \
  || ! nix path-info "${canonical_gate3_snapshot}" >/dev/null 2>&1; then
  printf 'Gate 3 report does not name an immutable Nix product snapshot\n' >&2
  exit 1
fi
gate3_snapshot="${canonical_gate3_snapshot}"

product_snapshot="${PALACE_GATE4_PRODUCT_SNAPSHOT:-${gate3_snapshot}}"
canonical_product_snapshot="$(
  realpath -e -- "${product_snapshot}" 2>/dev/null || true
)"
if [ "${product_snapshot}" != "${canonical_product_snapshot}" ] \
  || [ "${canonical_product_snapshot}" != "${gate3_snapshot}" ] \
  || ! nix path-info "${canonical_product_snapshot}" >/dev/null 2>&1; then
  printf 'Gate 4 snapshot must exactly match Gate 3 snapshot\n' >&2
  exit 1
fi
product_snapshot="${canonical_product_snapshot}"
product_ref="path:${product_snapshot}"
lock_file="${product_snapshot}/flake.lock"
acceptance_tools="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#acceptance-tools"
)"
if [ -z "${PALACE_MVP_PROCESS_SCOPE_UNIT:-}" ] \
  || [ -z "${PALACE_MVP_PROCESS_SCOPE_SLICE:-}" ]; then
  printf 'Gate 4 requires an attested process scope\n' >&2
  exit 1
fi
PALACE_MVP_PROCESS_CGROUP="$(
  "${acceptance_tools}/bin/node" \
    "${product_snapshot}/tests/basecamp_scope_control.mjs" current \
    "${PALACE_MVP_PROCESS_SCOPE_UNIT}" \
    "${PALACE_MVP_PROCESS_SCOPE_SLICE}"
)"
export PALACE_MVP_PROCESS_CGROUP
release_artifact="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-release-artifact"
)"
canonical_release_artifact="$(
  realpath -e -- "${release_artifact}" 2>/dev/null || true
)"
if [ "${release_artifact}" != "${canonical_release_artifact}" ] \
  || [[ ! "${canonical_release_artifact}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
  || ! nix path-info "${canonical_release_artifact}" >/dev/null 2>&1 \
  || [ ! -x "${canonical_release_artifact}/bin/palace-image-id" ] \
  || [ ! -f "${canonical_release_artifact}/bin/palace-image-id" ] \
  || [ -L "${canonical_release_artifact}/bin/palace-image-id" ] \
  || [ ! -f "${canonical_release_artifact}/share/logos-palace/release.json" ] \
  || [ -L "${canonical_release_artifact}/share/logos-palace/release.json" ] \
  || [ -e "${canonical_release_artifact}/share/logos-palace/palace.bin" ] \
  || [ -L "${canonical_release_artifact}/share/logos-palace/palace.bin" ] \
  || { [ -n "${PALACE_RELEASE_ARTIFACT:-}" ] \
    && [ "${PALACE_RELEASE_ARTIFACT}" != "${canonical_release_artifact}" ]; }; then
  printf 'Gate 4 release artifact is not snapshot-built\n' >&2
  exit 1
fi
export PALACE_RELEASE_ARTIFACT="${canonical_release_artifact}"

if [ "${PALACE_MVP_LOCK_FD+x}" = "x" ]; then
  printf 'Gate 4 must not inherit the MVP release lock FD\n' >&2
  exit 1
fi
run_dir="$("${acceptance_tools}/bin/realpath" -e -- \
  "$(dirname "${artifacts_dir}")")"
verified_claim="$(
  "${acceptance_tools}/bin/node" \
    "${product_snapshot}/tests/basecamp_active_run_claim.mjs" verify \
    "${run_dir}" \
    "${product_snapshot}" \
    "${PALACE_MVP_SNAPSHOT_GC_ROOT:-}" \
    "${PALACE_SOURCE_COMMIT:-}" \
    "${PALACE_PRODUCT_SNAPSHOT_NAR_HASH:-}" \
    "${PALACE_PRODUCT_SNAPSHOT_NAR_SIZE:-}" \
    "${PALACE_MVP_RUNNER_SHA256:-}" \
    "${PALACE_RUNTIME_OUTPUT_MANIFEST:-}" \
    "${PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256:-}" \
    "${PALACE_MVP_PROCESS_SCOPE_SLICE:-}" \
    "${PALACE_MVP_PROCESS_SCOPE_PREFIX:-}"
)"
if [ "${verified_claim}" != "${PALACE_MVP_CLAIM_PATH:-}" ]; then
  printf 'Gate 4 active-run claim differs\n' >&2
  exit 1
fi

gate3_valid="$(
  "${acceptance_tools}/bin/jq" -r \
    --arg snapshot "${product_snapshot}" \
    '.schema == "logos.palace.basecamp-gate3-report"
      and .version == 1
      and .status == "passed"
      and .fullGate3 == "passed"
      and .productionIdentityMode == true
      and (.identities | type == "object")
      and (.storageConfigs | type == "object")
      and .productSnapshot == $snapshot' \
    "${gate3_report}"
)"
if [ "${gate3_valid}" != "true" ]; then
  printf 'Gate 4 requires a newly passing Gate 3 report from the same snapshot\n' >&2
  exit 1
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/logos-palace-gate4.XXXXXX")"
active_swap_user=""
recover_package_swap() {
  local user_dir="$1"
  local backup_dir="${user_dir}/.gate4-package-backup"
  local component
  local unsafe_backup_entry

  if [ ! -e "${backup_dir}" ] && [ ! -L "${backup_dir}" ]; then
    return
  fi
  if [ -L "${backup_dir}" ] \
    || [ ! -d "${backup_dir}" ] \
    || [ "$(realpath -e -- "${backup_dir}" 2>/dev/null || true)" \
      != "${backup_dir}" ]; then
    printf 'Unsafe Gate 4 package backup: %s\n' "${backup_dir}" >&2
    return 1
  fi
  unsafe_backup_entry="$(
    find "${backup_dir}" -mindepth 1 -maxdepth 1 \
      ! -name modules ! -name plugins -print -quit
  )"
  if [ -n "${unsafe_backup_entry}" ]; then
    printf 'Gate 4 package backup contains unexpected entry: %s\n' \
      "${unsafe_backup_entry}" >&2
    return 1
  fi
  for component in modules plugins; do
    if [ ! -e "${backup_dir}/${component}" ] \
      && [ ! -L "${backup_dir}/${component}" ]; then
      continue
    fi
    if [ -L "${backup_dir}/${component}" ] \
      || [ ! -d "${backup_dir}/${component}" ] \
      || [ "$(realpath -e -- "${backup_dir}/${component}" \
        2>/dev/null || true)" != "${backup_dir}/${component}" ]; then
      printf 'Unsafe Gate 4 package backup component: %s\n' \
        "${backup_dir}/${component}" >&2
      return 1
    fi
    unsafe_backup_entry="$(
      find "${backup_dir}/${component}" -mindepth 1 \
        ! -type d ! -type f -print -quit
    )"
    if [ -n "${unsafe_backup_entry}" ]; then
      printf 'Unsafe Gate 4 package backup tree entry: %s\n' \
        "${unsafe_backup_entry}" >&2
      return 1
    fi
    if [ -L "${user_dir}/${component}" ] \
      || { [ -e "${user_dir}/${component}" ] \
        && { [ ! -d "${user_dir}/${component}" ] \
          || [ "$(realpath -e -- "${user_dir}/${component}" \
            2>/dev/null || true)" != "${user_dir}/${component}" ]; }; }; then
      printf 'Unsafe Gate 4 live package component: %s\n' \
        "${user_dir}/${component}" >&2
      return 1
    fi
  done
  for component in modules plugins; do
    if [ ! -e "${backup_dir}/${component}" ] \
      && [ ! -L "${backup_dir}/${component}" ]; then
      continue
    fi
    rm -rf -- "${user_dir:?}/${component}"
    mv -- "${backup_dir}/${component}" "${user_dir}/${component}"
  done
  if ! rmdir -- "${backup_dir}"; then
    printf 'Gate 4 package backup contains unexpected entries: %s\n' \
      "${backup_dir}" >&2
    return 1
  fi
}
cleanup() {
  if [ -n "${active_swap_user}" ]; then
    recover_package_swap "${active_swap_user}" || true
  fi
  if [ "${PALACE_KEEP_GATE4_WORK:-0}" = "1" ]; then
    printf 'Gate 4 package work directory: %s\n' "${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

state_dir="${PALACE_GATE4_STATE_DIR:-${artifacts_dir}/state}"
if [ -L "${state_dir}" ] \
  || { [ -e "${state_dir}" ] && [ ! -d "${state_dir}" ]; }; then
  printf 'Gate 4 state path must be a directory, not a symlink\n' >&2
  exit 1
fi
mkdir -p "${state_dir}"
canonical_state_dir="$(realpath -e -- "${state_dir}" 2>/dev/null || true)"
if [ "${state_dir}" != "${canonical_state_dir}" ]; then
  printf 'Gate 4 state directory must be a canonical absolute path\n' >&2
  exit 1
fi
state_dir="${canonical_state_dir}"
chmod 700 "${state_dir}"
users_dir="${state_dir}/users"
if [ -L "${users_dir}" ] \
  || { [ -e "${users_dir}" ] && [ ! -d "${users_dir}" ]; }; then
  printf 'Gate 4 users path must be a directory, not a symlink\n' >&2
  exit 1
fi
mkdir -p "${users_dir}"
if [ "$(realpath -e -- "${users_dir}" 2>/dev/null || true)" \
  != "${users_dir}" ]; then
  printf 'Gate 4 users directory is not canonical\n' >&2
  exit 1
fi
chmod 700 "${users_dir}"

printf 'Gate 4 product snapshot: %s\n' "${product_snapshot}"
printf 'Gate 4 persistent state: %s\n' "${state_dir}"

nix build --no-link \
  "${product_ref}#checks.x86_64-linux.palace-core-contracts"

basecamp_owner="$(
  "${acceptance_tools}/bin/jq" -r '.nodes.basecamp.locked.owner' "${lock_file}"
)"
basecamp_repo="$(
  "${acceptance_tools}/bin/jq" -r '.nodes.basecamp.locked.repo' "${lock_file}"
)"
basecamp_rev="$(
  "${acceptance_tools}/bin/jq" -r '.nodes.basecamp.locked.rev' "${lock_file}"
)"
basecamp_ref="github:${basecamp_owner}/${basecamp_repo}/${basecamp_rev}"

mapfile -t product_lgx_outputs < <(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-vm-lgx-portable" \
    "${product_ref}#palace-core-lgx-portable" \
    "${product_ref}#logos-palace-ui-lgx-portable" \
    "${product_ref}#delivery-module-lgx-portable" \
    "${product_ref}#storage-module-lgx-portable" \
    "${product_ref}#lez-core-lgx-portable"
)
basecamp_bundle="$(
  nix build --no-link --print-out-paths \
    "${basecamp_ref}#bin-bundle-dir-inspector"
)"
qt_mcp="$(
  nix build --no-link --print-out-paths \
    "${basecamp_ref}#logos-qt-mcp"
)"

basecamp_source="$(
  nix flake archive --json "${basecamp_ref}" |
    "${acceptance_tools}/bin/jq" -r '.path'
)"
package_manager_node="$(
  "${acceptance_tools}/bin/jq" -r \
    '.nodes.root.inputs["logos-package-manager"] | if type == "array" then .[0] else . end' \
    "${basecamp_source}/flake.lock"
)"
package_manager_owner="$(
  "${acceptance_tools}/bin/jq" -r --arg node "${package_manager_node}" \
    '.nodes[$node].locked.owner' "${basecamp_source}/flake.lock"
)"
package_manager_repo="$(
  "${acceptance_tools}/bin/jq" -r --arg node "${package_manager_node}" \
    '.nodes[$node].locked.repo' "${basecamp_source}/flake.lock"
)"
package_manager_rev="$(
  "${acceptance_tools}/bin/jq" -r --arg node "${package_manager_node}" \
    '.nodes[$node].locked.rev' "${basecamp_source}/flake.lock"
)"
lgpm="$(
  nix build --no-link --print-out-paths \
    "github:${package_manager_owner}/${package_manager_repo}/${package_manager_rev}#cli-portable"
)/bin/lgpm"

lgx_dir="${work_dir}/lgx"
mkdir -p "${lgx_dir}"
for output in "${product_lgx_outputs[@]}"; do
  find "${output}" -maxdepth 1 -type f -name '*.lgx' \
    -exec cp '{}' "${lgx_dir}/" ';'
done

for label in a b c; do
  user_dir="${users_dir}/${label}"
  if [ -L "${user_dir}" ] \
    || { [ -e "${user_dir}" ] && [ ! -d "${user_dir}" ]; }; then
    printf 'Gate 4 user path %s must be a directory, not a symlink\n' \
      "${label}" >&2
    exit 1
  fi
  mkdir -p "${user_dir}"
  if [ "$(realpath -e -- "${user_dir}" 2>/dev/null || true)" \
    != "${user_dir}" ]; then
    printf 'Gate 4 user directory %s is not canonical\n' "${label}" >&2
    exit 1
  fi
  chmod 700 "${user_dir}"
  if [ -L "${user_dir}/modules" ] \
    || [ -L "${user_dir}/plugins" ] \
    || { [ -e "${user_dir}/modules" ] \
      && [ ! -d "${user_dir}/modules" ]; } \
    || { [ -e "${user_dir}/plugins" ] \
      && [ ! -d "${user_dir}/plugins" ]; }; then
    printf 'Gate 4 package paths for %s must be directories, not symlinks\n' \
      "${label}" >&2
    exit 1
  fi
  mkdir -p "${user_dir}/modules" "${user_dir}/plugins"
  if [ "$(realpath -e -- "${user_dir}/modules" 2>/dev/null || true)" \
      != "${user_dir}/modules" ] \
    || [ "$(realpath -e -- "${user_dir}/plugins" 2>/dev/null || true)" \
      != "${user_dir}/plugins" ]; then
    printf 'Gate 4 package directories for %s are not canonical\n' \
      "${label}" >&2
    exit 1
  fi
  chmod 700 "${user_dir}/modules" "${user_dir}/plugins"

  stage_dir="${user_dir}/.gate4-package-stage"
  backup_dir="${user_dir}/.gate4-package-backup"
  if [ -e "${backup_dir}" ] || [ -L "${backup_dir}" ]; then
    active_swap_user="${user_dir}"
    recover_package_swap "${user_dir}"
    active_swap_user=""
  fi
  if [ -L "${stage_dir}" ] \
    || { [ -e "${stage_dir}" ] && [ ! -d "${stage_dir}" ]; } \
    || [ -L "${backup_dir}" ] \
    || { [ -e "${backup_dir}" ] && [ ! -d "${backup_dir}" ]; }; then
    printf 'Unsafe Gate 4 package staging path for %s\n' "${label}" >&2
    exit 1
  fi
  rm -rf -- "${stage_dir}"
  mkdir -p "${stage_dir}/modules" "${stage_dir}/plugins"
  if [ "$(realpath -e -- "${stage_dir}" 2>/dev/null || true)" \
      != "${stage_dir}" ] \
    || [ "$(realpath -e -- "${stage_dir}/modules" \
      2>/dev/null || true)" != "${stage_dir}/modules" ] \
    || [ "$(realpath -e -- "${stage_dir}/plugins" \
      2>/dev/null || true)" != "${stage_dir}/plugins" ]; then
    printf 'Gate 4 package staging directories for %s are not canonical\n' \
      "${label}" >&2
    exit 1
  fi
  chmod 700 "${stage_dir}" "${stage_dir}/modules" "${stage_dir}/plugins"

  "${lgpm}" \
    --modules-dir "${stage_dir}/modules" \
    --ui-plugins-dir "${stage_dir}/plugins" \
    --allow-unsigned \
    install --dir "${lgx_dir}"
  "${lgpm}" \
    --modules-dir "${stage_dir}/modules" \
    --ui-plugins-dir "${stage_dir}/plugins" \
    --json list >"${artifacts_dir}/staged-packages-${label}.json"

  installed_exact="$(
    "${acceptance_tools}/bin/jq" -r \
      'type == "array"
        and ([.[].name] | sort) == [
          "delivery_module",
          "lez_core",
          "logos_palace_ui",
          "palace_core",
          "palace_vm",
          "storage_module"
        ]' \
      "${artifacts_dir}/staged-packages-${label}.json"
  )"
  if [ "${installed_exact}" != "true" ]; then
    printf 'Expected exact six Palace/runtime packages for %s\n' \
      "${label}" >&2
    exit 1
  fi

  unsafe_entry="$(
    find "${stage_dir}" -mindepth 1 \
      ! -type d ! -type f -print -quit
  )"
  if [ -n "${unsafe_entry}" ]; then
    printf 'Gate 4 staged package tree contains unsafe entry: %s\n' \
      "${unsafe_entry}" >&2
    exit 1
  fi
  staged_modules_hash="$(nix hash path "${stage_dir}/modules")"
  staged_plugins_hash="$(nix hash path "${stage_dir}/plugins")"

  mkdir -- "${backup_dir}"
  chmod 700 "${backup_dir}"
  if [ "$(realpath -e -- "${backup_dir}" 2>/dev/null || true)" \
    != "${backup_dir}" ]; then
    printf 'Gate 4 package backup directory for %s is not canonical\n' \
      "${label}" >&2
    exit 1
  fi
  active_swap_user="${user_dir}"
  mv -- "${user_dir}/modules" "${backup_dir}/modules"
  mv -- "${user_dir}/plugins" "${backup_dir}/plugins"
  mv -- "${stage_dir}/modules" "${user_dir}/modules"
  mv -- "${stage_dir}/plugins" "${user_dir}/plugins"
  rmdir -- "${stage_dir}"

  installed_modules_hash="$(nix hash path "${user_dir}/modules")"
  installed_plugins_hash="$(nix hash path "${user_dir}/plugins")"
  if [ "${installed_modules_hash}" != "${staged_modules_hash}" ] \
    || [ "${installed_plugins_hash}" != "${staged_plugins_hash}" ]; then
    printf 'Gate 4 installed package roots changed during swap for %s\n' \
      "${label}" >&2
    exit 1
  fi
  "${lgpm}" \
    --modules-dir "${user_dir}/modules" \
    --ui-plugins-dir "${user_dir}/plugins" \
    --json list >"${artifacts_dir}/installed-packages-${label}.json"
  staged_package_contract="$(
    "${acceptance_tools}/bin/jq" -c \
      '[.[] | {
        name,
        version,
        type,
        root: .hashes.root
      }] | sort_by(.name)' \
      "${artifacts_dir}/staged-packages-${label}.json"
  )"
  installed_package_contract="$(
    "${acceptance_tools}/bin/jq" -c \
      '[.[] | {
        name,
        version,
        type,
        root: .hashes.root
      }] | sort_by(.name)' \
      "${artifacts_dir}/installed-packages-${label}.json"
  )"
  if [ "${installed_package_contract}" != "${staged_package_contract}" ]; then
    printf 'Gate 4 installed package list changed during swap for %s\n' \
      "${label}" >&2
    exit 1
  fi
  "${acceptance_tools}/bin/jq" -n \
    --arg modules "${installed_modules_hash}" \
    --arg plugins "${installed_plugins_hash}" \
    --arg source_lgx_set "$(
      "${acceptance_tools}/bin/jq" -c \
        '.packageHashes | sort_by(.file)' "${gate3_report}"
    )" \
    '{
      modulesNarHash: $modules,
      pluginsNarHash: $plugins,
      sourceLgxSet: ($source_lgx_set | fromjson),
      stagedThenSwappedWithRecovery: true
    }' >"${artifacts_dir}/installed-roots-${label}.json"

  active_swap_user=""
  rm -rf -- "${backup_dir}"
done

dependency_revisions="$(
  "${acceptance_tools}/bin/jq" -c \
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

export LOGOS_QT_MCP="${qt_mcp}"
export PALACE_BASECAMP_REV="${basecamp_rev}"
export PALACE_PIDFD_SIGNAL="${acceptance_tools}/bin/palace-pidfd-signal"
export PALACE_PRODUCT_SNAPSHOT="${product_snapshot}"
export PALACE_GATE4_DEPENDENCY_REVISIONS="${dependency_revisions}"
if [ ! -x "${PALACE_PIDFD_SIGNAL}" ]; then
  printf 'Gate 4 pidfd signal helper is unavailable\n' >&2
  exit 1
fi
set +e
"${acceptance_tools}/bin/node" \
  "${product_snapshot}/tests/basecamp_gate4.mjs" \
  "${basecamp_bundle}/bin/LogosBasecamp" \
  "${users_dir}" \
  "${artifacts_dir}" \
  "${lgx_dir}" \
  "${product_snapshot}/tests/basecamp_gate3_worker.mjs" \
  "${gate3_report}"
gate4_status=$?
set -e

printf 'Gate 4 report: %s\n' "${artifacts_dir}/gate4-report.json"
exit "${gate4_status}"
