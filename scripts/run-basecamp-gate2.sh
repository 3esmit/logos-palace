#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifacts_dir="${1:-${repo_root}/.artifacts/basecamp-gate2}"
mkdir -p "${artifacts_dir}"
artifacts_dir="$(cd "${artifacts_dir}" && pwd)"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/logos-palace-gate2.XXXXXX")"
work_dir="$(cd "${work_dir}" && pwd -P)"
chmod 700 "${work_dir}"
cleanup() {
  if [ "${PALACE_KEEP_GATE2_WORK:-0}" = "1" ]; then
    printf 'Gate 2 work directory: %s\n' "${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

product_snapshot="${PALACE_GATE2_PRODUCT_SNAPSHOT:-}"
if [ -n "${product_snapshot}" ]; then
  canonical_snapshot="$(realpath -e -- "${product_snapshot}" 2>/dev/null || true)"
  if [ "${product_snapshot}" != "${canonical_snapshot}" ] \
    || [[ ! "${canonical_snapshot}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    || [ ! -d "${canonical_snapshot}" ] \
    || ! nix path-info "${canonical_snapshot}" >/dev/null 2>&1; then
    printf 'PALACE_GATE2_PRODUCT_SNAPSHOT is not a Nix store directory\n' >&2
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
    printf 'Gate 2 source must be a clean Git HEAD\n' >&2
    exit 1
  fi
  source_commit="$(git -C "${repo_root}" rev-parse --verify HEAD)"
  if [[ ! "${source_commit}" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'Gate 2 source HEAD is invalid\n' >&2
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
printf 'Gate 2 product snapshot secured: %s\n' "${product_snapshot}"
lock_file="${product_snapshot}/flake.lock"
nix build --no-link \
  "${product_ref}#checks.x86_64-linux.palace-core-contracts" \
  "${product_ref}#checks.x86_64-linux.palace-core-production-fixture-audit" \
  "${product_ref}#checks.x86_64-linux.palace-core-acceptance-fixture-audit"
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

mapfile -t common_lgx_outputs < <(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-vm-lgx-portable" \
    "${product_ref}#logos-palace-ui-lgx-portable" \
    "${product_ref}#delivery-module-lgx-portable" \
    "${product_ref}#storage-module-lgx-portable" \
    "${product_ref}#lez-core-lgx-portable"
)
production_core_lgx_output="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-core-lgx-portable"
)"
expected_acceptance_core_lgx_output="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-core-acceptance-lgx-portable"
)"
acceptance_core_lgx_output="${PALACE_GATE2_ACCEPTANCE_CORE_LGX:-}"
if [ -n "${acceptance_core_lgx_output}" ]; then
  canonical_acceptance_core="$(
    realpath -e -- "${acceptance_core_lgx_output}" 2>/dev/null || true
  )"
  if [ "${acceptance_core_lgx_output}" \
      != "${canonical_acceptance_core}" ] \
    || [[ ! "${canonical_acceptance_core}" =~ ^/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$ ]] \
    || ! nix path-info "${canonical_acceptance_core}" >/dev/null 2>&1 \
    || [ "${canonical_acceptance_core}" \
      != "${expected_acceptance_core_lgx_output}" ]; then
    printf 'PALACE_GATE2_ACCEPTANCE_CORE_LGX is not the snapshot-built acceptance Core\n' >&2
    exit 1
  fi
  acceptance_core_lgx_output="${canonical_acceptance_core}"
else
  acceptance_core_lgx_output="${expected_acceptance_core_lgx_output}"
fi
product_lgx_outputs=(
  "${common_lgx_outputs[@]}"
  "${acceptance_core_lgx_output}"
)
production_lgx_outputs=(
  "${common_lgx_outputs[@]}"
  "${production_core_lgx_output}"
)
acceptance_lgx_output="$(
  nix build --no-link --print-out-paths \
    "${product_ref}#palace-delivery-acceptance-lgx-portable"
)"
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
production_lgx_dir="${work_dir}/production-lgx"
acceptance_lgx_dir="${work_dir}/acceptance-lgx"
users_dir="${work_dir}/users"
mkdir -p \
  "${lgx_dir}" \
  "${production_lgx_dir}" \
  "${acceptance_lgx_dir}" \
  "${users_dir}"
for output in "${product_lgx_outputs[@]}"; do
  find "${output}" -maxdepth 1 -type f -name '*.lgx' \
    -exec cp '{}' "${lgx_dir}/" ';'
done
for output in "${production_lgx_outputs[@]}"; do
  find "${output}" -maxdepth 1 -type f -name '*.lgx' \
    -exec cp '{}' "${production_lgx_dir}/" ';'
done
if [ "$(find "${lgx_dir}" -maxdepth 1 -type f -name '*.lgx' | wc -l)" \
      -ne 6 ] \
  || [ "$(find "${production_lgx_dir}" -maxdepth 1 -type f -name '*.lgx' | wc -l)" \
      -ne 6 ]; then
  printf 'Gate 2 production or acceptance-replaced package set is not exact\n' >&2
  exit 1
fi

for label in a b c; do
  user_dir="${users_dir}/${label}"
  mkdir -p "${user_dir}/modules" "${user_dir}/plugins"
  "${lgpm}" \
    --modules-dir "${user_dir}/modules" \
    --ui-plugins-dir "${user_dir}/plugins" \
    --allow-unsigned \
    install --dir "${lgx_dir}"
  "${lgpm}" \
    --modules-dir "${user_dir}/modules" \
    --ui-plugins-dir "${user_dir}/plugins" \
    --json list >"${artifacts_dir}/installed-packages-${label}.json"

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
      "${artifacts_dir}/installed-packages-${label}.json"
  )"
  if [ "${installed_exact}" != "true" ]; then
    printf 'Expected each exact Palace/runtime package once for %s\n' \
      "${label}" >&2
    exit 1
  fi
done

find "${lgx_dir}" -maxdepth 1 -type f -name '*delivery_module*.lgx' \
  -exec cp '{}' "${acceptance_lgx_dir}/" ';'
find "${acceptance_lgx_output}" -maxdepth 1 -type f -name '*.lgx' \
  -exec cp '{}' "${acceptance_lgx_dir}/" ';'
acceptance_lgx_count="$(
  find "${acceptance_lgx_dir}" -maxdepth 1 -type f -name '*.lgx' |
    wc -l
)"
if [ "${acceptance_lgx_count}" -ne 2 ]; then
  printf 'Expected Delivery and acceptance LGXs, found %s\n' \
    "${acceptance_lgx_count}" >&2
  exit 1
fi

acceptance_user_dir="${users_dir}/acceptance"
mkdir -p \
  "${acceptance_user_dir}/modules" \
  "${acceptance_user_dir}/plugins"
"${lgpm}" \
  --modules-dir "${acceptance_user_dir}/modules" \
  --ui-plugins-dir "${acceptance_user_dir}/plugins" \
  --allow-unsigned \
  install --dir "${acceptance_lgx_dir}"
"${lgpm}" \
  --modules-dir "${acceptance_user_dir}/modules" \
  --ui-plugins-dir "${acceptance_user_dir}/plugins" \
  --json list >"${artifacts_dir}/installed-packages-acceptance.json"
acceptance_installed="$(
  "${acceptance_tools}/bin/jq" \
    'length == 2
      and any(.[]; .name == "delivery_module")
      and any(.[]; .name == "palace_delivery_acceptance")' \
    "${artifacts_dir}/installed-packages-acceptance.json"
)"
if [ "${acceptance_installed}" != "true" ]; then
  printf 'Acceptance user package set is not exact\n' >&2
  exit 1
fi

export LOGOS_QT_MCP="${qt_mcp}"
export PALACE_BASECAMP_REV="${basecamp_rev}"
export PALACE_PRODUCT_SNAPSHOT="${product_snapshot}"
if [ "${PALACE_MVP_CLAIM_PATH+x}" != "${PALACE_MVP_LOCK_FD+x}" ]; then
  printf 'PALACE_MVP_CLAIM_PATH and PALACE_MVP_LOCK_FD must be provided together\n' >&2
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
  "${product_snapshot}/tests/basecamp_gate2.mjs" \
  "${basecamp_bundle}/bin/LogosBasecamp" \
  "${users_dir}" \
  "${artifacts_dir}" \
  "${lgx_dir}" \
  "${production_lgx_dir}" \
  "${acceptance_lgx_dir}" \
  "${product_snapshot}/tests/basecamp_gate2_worker.mjs"

printf 'Gate 2 report: %s\n' "${artifacts_dir}/gate2-report.json"
