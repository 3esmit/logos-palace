#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifacts_dir="${1:-${repo_root}/.artifacts/basecamp-gate3}"
mkdir -p "${artifacts_dir}"
artifacts_dir="$(cd "${artifacts_dir}" && pwd)"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/logos-palace-gate3.XXXXXX")"
active_swap_user=""
recover_gate3_package_swap() {
  local user_dir="$1"
  local backup_dir="${user_dir}/.gate3-package-backup"
  local component
  local unsafe_backup_entry

  if [ ! -e "${backup_dir}" ] && [ ! -L "${backup_dir}" ]; then
    return
  fi
  if [ -L "${backup_dir}" ] \
    || [ ! -d "${backup_dir}" ] \
    || [ "$(realpath -e -- "${backup_dir}" 2>/dev/null || true)" \
      != "${backup_dir}" ]; then
    printf 'Unsafe Gate 3 package backup: %s\n' "${backup_dir}" >&2
    return 1
  fi
  unsafe_backup_entry="$(
    find "${backup_dir}" -mindepth 1 -maxdepth 1 \
      ! -name modules ! -name plugins -print -quit
  )"
  if [ -n "${unsafe_backup_entry}" ]; then
    printf 'Gate 3 package backup contains unexpected entry: %s\n' \
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
      printf 'Unsafe Gate 3 package backup component: %s\n' \
        "${backup_dir}/${component}" >&2
      return 1
    fi
    unsafe_backup_entry="$(
      find "${backup_dir}/${component}" -mindepth 1 \
        ! -type d ! -type f -print -quit
    )"
    if [ -n "${unsafe_backup_entry}" ]; then
      printf 'Unsafe Gate 3 package backup tree entry: %s\n' \
        "${unsafe_backup_entry}" >&2
      return 1
    fi
    if [ -L "${user_dir}/${component}" ] \
      || { [ -e "${user_dir}/${component}" ] \
        && { [ ! -d "${user_dir}/${component}" ] \
          || [ "$(realpath -e -- "${user_dir}/${component}" \
            2>/dev/null || true)" != "${user_dir}/${component}" ]; }; }; then
      printf 'Unsafe Gate 3 live package component: %s\n' \
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
    printf 'Gate 3 package backup contains unexpected entries: %s\n' \
      "${backup_dir}" >&2
    return 1
  fi
}
cleanup() {
  if [ -n "${active_swap_user}" ]; then
    recover_gate3_package_swap "${active_swap_user}" || true
  fi
  if [ "${PALACE_KEEP_GATE3_WORK:-0}" = "1" ]; then
    printf 'Gate 3 work directory: %s\n' "${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

product_snapshot="${PALACE_GATE3_PRODUCT_SNAPSHOT:-}"
if [ -n "${product_snapshot}" ]; then
  canonical_snapshot="$(realpath -e -- "${product_snapshot}" 2>/dev/null || true)"
  if [ "${product_snapshot}" != "${canonical_snapshot}" ] \
    || [[ ! "${canonical_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    || [ ! -d "${canonical_snapshot}" ] \
    || ! nix path-info "${canonical_snapshot}" >/dev/null 2>&1; then
    printf 'PALACE_GATE3_PRODUCT_SNAPSHOT is not a Nix store directory\n' >&2
    exit 1
  fi
  product_snapshot="${canonical_snapshot}"
  product_ref="path:${product_snapshot}"
  acceptance_tools="$(
    nix build --no-link --print-out-paths \
      "${product_ref}#acceptance-tools"
  )"
else
  if [ -n "$(git -C "${repo_root}" status --porcelain=v1 \
    --untracked-files=all)" ]; then
    printf 'Gate 3 source must be a clean Git HEAD\n' >&2
    exit 1
  fi
  source_commit="$(git -C "${repo_root}" rev-parse --verify HEAD)"
  if [[ ! "${source_commit}" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'Gate 3 source HEAD is invalid\n' >&2
    exit 1
  fi
  live_product_ref="git+file://${repo_root}?rev=${source_commit}"
  acceptance_tools="$(
    nix build --no-link --print-out-paths \
      "${live_product_ref}#acceptance-tools"
  )"
  product_snapshot="$(
    nix flake archive --json "${live_product_ref}" |
      "${acceptance_tools}/bin/jq" -r '.path'
  )"
  canonical_snapshot="$(realpath -e -- "${product_snapshot}" 2>/dev/null || true)"
  if [ "${product_snapshot}" != "${canonical_snapshot}" ] \
    || [[ ! "${canonical_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    || [ ! -d "${canonical_snapshot}" ] \
    || ! nix path-info "${canonical_snapshot}" >/dev/null 2>&1; then
    printf 'Could not archive immutable product source\n' >&2
    exit 1
  fi
  product_snapshot="${canonical_snapshot}"
  product_ref="path:${product_snapshot}"
fi
printf 'Gate 3 product snapshot secured: %s\n' "${product_snapshot}"
lock_file="${product_snapshot}/flake.lock"
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
  printf 'Gate 3 release artifact is not snapshot-built\n' >&2
  exit 1
fi
export PALACE_RELEASE_ARTIFACT="${canonical_release_artifact}"

require_inherited_mvp_lock() {
  local current_uid
  local runtime_dir
  local canonical_runtime_dir
  local release_program_id
  local release_root_id
  local expected_lock_path
  local canonical_lock_path
  local inherited_lock_target
  local lock_fd

  current_uid="$("${acceptance_tools}/bin/id" -u)"
  runtime_dir="/run/user/${current_uid}"
  canonical_runtime_dir="$(
    "${acceptance_tools}/bin/realpath" -e -- "${runtime_dir}" \
      2>/dev/null || true
  )"
  if [ "${runtime_dir}" != "${canonical_runtime_dir}" ] \
    || [ -L "${runtime_dir}" ] \
    || [ ! -d "${runtime_dir}" ] \
    || [ "$("${acceptance_tools}/bin/stat" -c '%u' "${runtime_dir}")" \
      != "${current_uid}" ] \
    || [ "$("${acceptance_tools}/bin/stat" -c '%a' "${runtime_dir}")" \
      != "700" ]; then
    runtime_dir="/tmp/logos-palace-runtime-${current_uid}"
    canonical_runtime_dir="$(
      "${acceptance_tools}/bin/realpath" -e -- "${runtime_dir}" \
        2>/dev/null || true
    )"
    if [ "${runtime_dir}" != "${canonical_runtime_dir}" ] \
      || [ -L "${runtime_dir}" ] \
      || [ ! -d "${runtime_dir}" ] \
      || [ "$("${acceptance_tools}/bin/stat" -c '%u' "${runtime_dir}")" \
        != "${current_uid}" ] \
      || [ "$("${acceptance_tools}/bin/stat" -c '%a' "${runtime_dir}")" \
        != "700" ]; then
      printf 'Inherited MVP lock directory is not deterministic/secure\n' >&2
      return 1
    fi
  fi

  release_program_id="e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61"
  release_root_id="12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a"
  expected_lock_path="${runtime_dir}/logos-palace-${release_program_id}-${release_root_id}.lock"
  lock_fd="${PALACE_MVP_LOCK_FD:-}"
  if [ "${PALACE_MVP_LOCK_PATH:-}" != "${expected_lock_path}" ] \
    || [[ ! "${lock_fd}" =~ ^([3-9]|[1-9][0-9]+)$ ]] \
    || [ ! -e "/proc/$$/fd/${lock_fd}" ] \
    || [ -L "${expected_lock_path}" ] \
    || [ ! -f "${expected_lock_path}" ]; then
    printf 'Production Gate 3 requires inherited MVP release lock\n' >&2
    return 1
  fi
  canonical_lock_path="$(
    "${acceptance_tools}/bin/realpath" -e -- "${expected_lock_path}" \
      2>/dev/null || true
  )"
  inherited_lock_target="$(
    "${acceptance_tools}/bin/realpath" -e -- "/proc/$$/fd/${lock_fd}" \
      2>/dev/null || true
  )"
  if [ "${canonical_lock_path}" != "${expected_lock_path}" ] \
    || [ "${inherited_lock_target}" != "${expected_lock_path}" ] \
    || [ "$("${acceptance_tools}/bin/stat" -c '%u' \
      "${expected_lock_path}")" != "${current_uid}" ] \
    || [ "$("${acceptance_tools}/bin/stat" -c '%a' \
      "${expected_lock_path}")" != "600" ] \
    || ! "${acceptance_tools}/bin/flock" -n "${lock_fd}"; then
    printf 'Inherited MVP release lock failed validation\n' >&2
    return 1
  fi
}

if [ "${PALACE_GATE3_PRODUCTION_IDENTITIES:-0}" = "1" ]; then
  require_inherited_mvp_lock
  if [ -n "${PALACE_GATE3_STORAGE_CONFIG_BASE+x}" ]; then
    printf 'Production Gate 3 forbids Storage config override\n' >&2
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
      "${PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256:-}"
  )"
  if [ "${verified_claim}" != "${PALACE_MVP_CLAIM_PATH:-}" ]; then
    printf 'Production Gate 3 active-run claim differs\n' >&2
    exit 1
  fi
fi

if [ "${PALACE_GATE3_PRODUCTION_IDENTITIES:-0}" = "1" ]; then
  palace_core_fixture_audit="palace-core-production-fixture-audit"
else
  palace_core_fixture_audit="palace-core-acceptance-fixture-audit"
fi
nix build --no-link \
  "${product_ref}#checks.x86_64-linux.palace-core-contracts" \
  "${product_ref}#checks.x86_64-linux.${palace_core_fixture_audit}"

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
  if [ "${PALACE_GATE3_PRODUCTION_IDENTITIES:-0}" = "1" ]; then
    palace_core_output="${product_ref}#palace-core-lgx-portable"
  else
    palace_core_output="${product_ref}#palace-core-acceptance-lgx-portable"
  fi
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-vm-lgx-portable" \
    "${palace_core_output}" \
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

state_dir="${PALACE_GATE3_STATE_DIR:-}"
if [ -n "${state_dir}" ]; then
  if [ -L "${state_dir}" ] \
    || { [ -e "${state_dir}" ] && [ ! -d "${state_dir}" ]; }; then
    printf 'Gate 3 state path must be a directory, not a symlink\n' >&2
    exit 1
  fi
  mkdir -p "${state_dir}"
  canonical_state_dir="$(realpath -e -- "${state_dir}" 2>/dev/null || true)"
  if [ "${state_dir}" != "${canonical_state_dir}" ]; then
    printf 'Gate 3 state directory must be a canonical absolute path\n' >&2
    exit 1
  fi
  state_dir="${canonical_state_dir}"
  chmod 700 "${state_dir}"
  users_dir="${state_dir}/users"
  if [ -L "${users_dir}" ] \
    || { [ -e "${users_dir}" ] && [ ! -d "${users_dir}" ]; }; then
    printf 'Gate 3 users path must be a directory, not a symlink\n' >&2
    exit 1
  fi
  mkdir -p "${users_dir}"
  if [ "$(realpath -e -- "${users_dir}")" != "${users_dir}" ]; then
    printf 'Gate 3 users directory is not canonical\n' >&2
    exit 1
  fi
  chmod 700 "${users_dir}"
  printf 'Gate 3 persistent state: %s\n' "${state_dir}"
else
  users_dir="${work_dir}/users"
  mkdir -p "${users_dir}"
fi

for output in "${product_lgx_outputs[@]}"; do
  find "${output}" -maxdepth 1 -type f -name '*.lgx' \
    -exec cp '{}' "${lgx_dir}/" ';'
done

for label in a b c; do
  user_dir="${users_dir}/${label}"
  if [ -L "${user_dir}" ] \
    || { [ -e "${user_dir}" ] && [ ! -d "${user_dir}" ]; }; then
    printf 'Gate 3 user path %s must be a directory, not a symlink\n' \
      "${label}" >&2
    exit 1
  fi
  mkdir -p "${user_dir}"
  if [ "$(realpath -e -- "${user_dir}")" != "${user_dir}" ]; then
    printf 'Gate 3 user directory %s is not canonical\n' "${label}" >&2
    exit 1
  fi
  if [ -L "${user_dir}/modules" ] \
    || [ -L "${user_dir}/plugins" ] \
    || { [ -e "${user_dir}/modules" ] && [ ! -d "${user_dir}/modules" ]; } \
    || { [ -e "${user_dir}/plugins" ] && [ ! -d "${user_dir}/plugins" ]; }; then
    printf 'Gate 3 package paths for %s must be directories, not symlinks\n' \
      "${label}" >&2
    exit 1
  fi
  mkdir -p "${user_dir}/modules" "${user_dir}/plugins"
  if [ "$(realpath -e -- "${user_dir}/modules")" \
      != "${user_dir}/modules" ] \
    || [ "$(realpath -e -- "${user_dir}/plugins")" \
      != "${user_dir}/plugins" ]; then
    printf 'Gate 3 package directories for %s are not canonical\n' \
      "${label}" >&2
    exit 1
  fi
  chmod 700 "${user_dir}" "${user_dir}/modules" "${user_dir}/plugins"

  stage_dir="${user_dir}/.gate3-package-stage"
  backup_dir="${user_dir}/.gate3-package-backup"
  if [ -e "${backup_dir}" ] || [ -L "${backup_dir}" ]; then
    active_swap_user="${user_dir}"
    recover_gate3_package_swap "${user_dir}"
    active_swap_user=""
  fi
  if [ -L "${stage_dir}" ] \
    || { [ -e "${stage_dir}" ] && [ ! -d "${stage_dir}" ]; } \
    || [ -L "${backup_dir}" ] \
    || { [ -e "${backup_dir}" ] && [ ! -d "${backup_dir}" ]; }; then
    printf 'Unsafe Gate 3 package staging path for %s\n' "${label}" >&2
    exit 1
  fi
  rm -rf -- "${stage_dir}"
  mkdir -p "${stage_dir}/modules" "${stage_dir}/plugins"
  if [ "$(realpath -e -- "${stage_dir}")" != "${stage_dir}" ] \
    || [ "$(realpath -e -- "${stage_dir}/modules")" \
      != "${stage_dir}/modules" ] \
    || [ "$(realpath -e -- "${stage_dir}/plugins")" \
      != "${stage_dir}/plugins" ]; then
    printf 'Gate 3 package staging directories for %s are not canonical\n' \
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
    printf 'Gate 3 staged package tree contains unsafe entry: %s\n' \
      "${unsafe_entry}" >&2
    exit 1
  fi
  staged_modules_hash="$(nix hash path "${stage_dir}/modules")"
  staged_plugins_hash="$(nix hash path "${stage_dir}/plugins")"

  mkdir -- "${backup_dir}"
  chmod 700 "${backup_dir}"
  if [ "$(realpath -e -- "${backup_dir}")" != "${backup_dir}" ]; then
    printf 'Gate 3 package backup directory for %s is not canonical\n' \
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
    printf 'Gate 3 installed package roots changed during swap for %s\n' \
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
    printf 'Gate 3 installed package list changed during swap for %s\n' \
      "${label}" >&2
    exit 1
  fi

  active_swap_user=""
  rm -rf -- "${backup_dir}"
done

export LOGOS_QT_MCP="${qt_mcp}"
export PALACE_BASECAMP_REV="${basecamp_rev}"
export PALACE_PRODUCT_SNAPSHOT="${product_snapshot}"
set +e
"${acceptance_tools}/bin/node" \
  "${product_snapshot}/tests/basecamp_gate3.mjs" \
  "${basecamp_bundle}/bin/LogosBasecamp" \
  "${users_dir}" \
  "${artifacts_dir}" \
  "${lgx_dir}" \
  "${product_snapshot}/tests/basecamp_gate3_worker.mjs"
gate3_status=$?
set -e

printf 'Gate 3 report: %s\n' "${artifacts_dir}/gate3-report.json"
exit "${gate3_status}"
