#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
product_ref="path:${repo_root}"
artifacts_dir="${1:-${repo_root}/.artifacts/basecamp-gate1}"
mkdir -p "${artifacts_dir}"
artifacts_dir="$(cd "${artifacts_dir}" && pwd)"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/logos-palace-gate1.XXXXXX")"
cleanup() {
  if [ "${PALACE_KEEP_GATE1_WORK:-0}" = "1" ]; then
    printf 'Gate 1 work directory: %s\n' "${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

lock_file="${repo_root}/flake.lock"
acceptance_tools="$(
  nix build --no-link --print-out-paths "${product_ref}#acceptance-tools"
)"
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

installed_count="$(
  "${acceptance_tools}/bin/jq" \
    '[.[] | select(.name == "palace_vm" or .name == "palace_core" or .name == "logos_palace_ui" or .name == "delivery_module" or .name == "storage_module" or .name == "lez_core")] | length' \
    "${artifacts_dir}/installed-packages.json"
)"
if [ "${installed_count}" -ne 6 ]; then
  printf 'Expected six Palace/runtime packages, found %s\n' "${installed_count}" >&2
  exit 1
fi

export LOGOS_QT_MCP="${qt_mcp}"
export PALACE_BASECAMP_REV="${basecamp_rev}"
export QML_INSPECTOR_PORT="${PALACE_GATE1_INSPECTOR_PORT:-4768}"
"${acceptance_tools}/bin/node" \
  "${repo_root}/tests/basecamp_gate1.mjs" \
  "${basecamp_bundle}/bin/LogosBasecamp" \
  "${user_dir}" \
  "${artifacts_dir}" \
  "${lgx_dir}"

printf 'Gate 1 report: %s\n' "${artifacts_dir}/gate1-report.json"
