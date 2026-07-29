#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifacts_dir="${1:-${repo_root}/.artifacts/basecamp-gate1}"
mkdir -p "${artifacts_dir}"
artifacts_dir="$(cd "${artifacts_dir}" && pwd)"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/logos-palace-gate1.XXXXXX")"
work_dir="$(cd "${work_dir}" && pwd -P)"
chmod 700 "${work_dir}"
cleanup() {
  if [ "${PALACE_KEEP_GATE1_WORK:-0}" = "1" ]; then
    printf 'Gate 1 work directory: %s\n' "${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

product_snapshot="${PALACE_GATE1_PRODUCT_SNAPSHOT:-}"
if [ -n "${product_snapshot}" ]; then
  canonical_snapshot="$(realpath -e -- "${product_snapshot}" 2>/dev/null || true)"
  if [ "${product_snapshot}" != "${canonical_snapshot}" ] \
    || [[ ! "${canonical_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    || [ ! -d "${canonical_snapshot}" ] \
    || ! nix path-info "${canonical_snapshot}" >/dev/null 2>&1; then
    printf 'PALACE_GATE1_PRODUCT_SNAPSHOT is not a Nix store directory\n' >&2
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
    printf 'Gate 1 source must be a clean Git HEAD\n' >&2
    exit 1
  fi
  source_commit="$(git -C "${repo_root}" rev-parse --verify HEAD)"
  if [[ ! "${source_commit}" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'Gate 1 source HEAD is invalid\n' >&2
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
    || ! nix path-info "${canonical_snapshot}" >/dev/null 2>&1; then
    printf 'Could not archive immutable product source\n' >&2
    exit 1
  fi
  product_snapshot="${canonical_snapshot}"
  product_ref="path:${product_snapshot}"
fi
printf 'Gate 1 product snapshot secured: %s\n' "${product_snapshot}"
if [ "${PALACE_MVP_PROCESS_SCOPE_UNIT+x}" \
    != "${PALACE_MVP_PROCESS_SCOPE_SLICE+x}" ]; then
  printf 'Gate 1 process scope unit and slice must be provided together\n' >&2
  exit 1
fi
if [ "${PALACE_MVP_PROCESS_SCOPE_UNIT+x}" != "x" ]; then
  set +e
  "${acceptance_tools}/bin/bash" \
    -p \
    "${product_snapshot}/scripts/run-basecamp-standalone-scoped.sh" \
    gate1 \
    "${product_snapshot}" \
    "${acceptance_tools}" \
    "${artifacts_dir}" \
    "${product_snapshot}/scripts/run-basecamp-gate1.sh"
  standalone_status=$?
  set -e
  exit "${standalone_status}"
fi
PALACE_MVP_PROCESS_CGROUP="$(
  "${acceptance_tools}/bin/node" \
    "${product_snapshot}/tests/basecamp_scope_control.mjs" current \
    "${PALACE_MVP_PROCESS_SCOPE_UNIT}" \
    "${PALACE_MVP_PROCESS_SCOPE_SLICE}"
)"
export PALACE_MVP_PROCESS_CGROUP
lock_file="${product_snapshot}/flake.lock"
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
user_dir="${work_dir}/user"
mkdir -p "${lgx_dir}" "${user_dir}/modules" "${user_dir}/plugins"
for output in "${product_lgx_outputs[@]}"; do
  find "${output}" -maxdepth 1 -type f -name '*.lgx' \
    -exec cp '{}' "${lgx_dir}/" ';'
done

"${lgpm}" \
  --modules-dir "${user_dir}/modules" \
  --ui-plugins-dir "${user_dir}/plugins" \
  --allow-unsigned \
  install --dir "${lgx_dir}"
"${lgpm}" \
  --modules-dir "${user_dir}/modules" \
  --ui-plugins-dir "${user_dir}/plugins" \
  --json list >"${artifacts_dir}/installed-packages.json"

installed_exact="$(
  "${acceptance_tools}/bin/jq" -r \
    'length == 6
      and (
        map(.name) | sort
      ) == [
        "delivery_module",
        "lez_core",
        "logos_palace_ui",
        "palace_core",
        "palace_vm",
        "storage_module"
      ]' \
    "${artifacts_dir}/installed-packages.json"
)"
if [ "${installed_exact}" != "true" ]; then
  printf 'Expected each of the exact six Palace/runtime packages once\n' >&2
  exit 1
fi

export LOGOS_QT_MCP="${qt_mcp}"
export PALACE_BASECAMP_REV="${basecamp_rev}"
export PALACE_PIDFD_SIGNAL="${acceptance_tools}/bin/palace-pidfd-signal"
export PALACE_PRODUCT_SNAPSHOT="${product_snapshot}"
export QML_INSPECTOR_PORT="${PALACE_GATE1_INSPECTOR_PORT:-4768}"
if [ ! -x "${PALACE_PIDFD_SIGNAL}" ]; then
  printf 'Gate 1 pidfd signal helper is unavailable\n' >&2
  exit 1
fi
if [ "${PALACE_MVP_LOCK_FD+x}" = "x" ]; then
  printf 'Gate 1 must not inherit the MVP release lock FD\n' >&2
  exit 1
fi
if [ "${PALACE_MVP_CLAIM_PATH+x}" = "x" ]; then
  canonical_claim="$(
    realpath -e -- "${PALACE_MVP_CLAIM_PATH}" 2>/dev/null || true
  )"
  if [ "${PALACE_MVP_CLAIM_PATH}" != "${canonical_claim}" ] \
    || [ -L "${PALACE_MVP_CLAIM_PATH}" ] \
    || [ ! -f "${PALACE_MVP_CLAIM_PATH}" ] \
    || [ "$(stat -c '%u:%a' -- "${PALACE_MVP_CLAIM_PATH}")" \
      != "$(id -u):600" ]; then
    printf 'PALACE_MVP_CLAIM_PATH is not an owner-only regular file\n' >&2
    exit 1
  fi
else
  PALACE_MVP_CLAIM_PATH="${work_dir}/standalone-cleanup-claim"
  (
    umask 077
    : >"${PALACE_MVP_CLAIM_PATH}"
  )
  chmod 600 "${PALACE_MVP_CLAIM_PATH}"
  export PALACE_MVP_CLAIM_PATH
fi
"${acceptance_tools}/bin/node" \
  "${product_snapshot}/tests/basecamp_gate1.mjs" \
  "${basecamp_bundle}/bin/LogosBasecamp" \
  "${user_dir}" \
  "${artifacts_dir}" \
  "${lgx_dir}"

printf 'Gate 1 report: %s\n' "${artifacts_dir}/gate1-report.json"
