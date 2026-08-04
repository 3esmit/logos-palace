#!/usr/bin/env bash

set -euo pipefail

package_root="${1:?package root is required}"
release_repository="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
release_target="${GITHUB_SHA:-HEAD}"

command -v gh >/dev/null 2>&1 || {
  printf 'Required command is missing: gh\n' >&2
  exit 2
}
command -v jq >/dev/null 2>&1 || {
  printf 'Required command is missing: jq\n' >&2
  exit 2
}
command -v sha256sum >/dev/null 2>&1 || {
  printf 'Required command is missing: sha256sum\n' >&2
  exit 2
}
command -v stat >/dev/null 2>&1 || {
  printf 'Required command is missing: stat\n' >&2
  exit 2
}
command -v tar >/dev/null 2>&1 || {
  printf 'Required command is missing: tar\n' >&2
  exit 2
}

work_root="$(mktemp -d)"
trap 'rm -rf -- "$work_root"' EXIT

publish_module() {
  local module_name="$1"
  local source_artifact="$2"
  local source_file="${package_root}/lgx/${source_artifact}"
  local manifest_file="${work_root}/${module_name}.manifest.json"

  if [[ ! -f "${source_file}" || -L "${source_file}" ]]; then
    printf 'Palace package is not a regular file: %s\n' "${source_file}" >&2
    exit 1
  fi

  tar -xOf "${source_file}" manifest.json >"${manifest_file}"
  jq -e --arg name "${module_name}" '
    .name == $name
    and (.version | type == "string" and test("^[0-9]+\\.[0-9]+\\.[0-9]+$"))
    and (.hashes.root | type == "string" and test("^[0-9a-f]{64}$"))
  ' "${manifest_file}" >/dev/null

  local version
  version="$(jq -r '.version' "${manifest_file}")"
  local tag="${module_name}-v${version}"
  local asset_name="${module_name}-${version}.lgx"
  local asset_file="${work_root}/${asset_name}"
  local sidecar_file="${work_root}/sidecar.json"
  local released_at
  released_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

  install -m 0444 "${source_file}" "${asset_file}"
  local sha256
  sha256="$(sha256sum "${asset_file}" | cut -d' ' -f1)"
  local size
  size="$(stat -c '%s' "${asset_file}")"
  jq -n \
    --arg releasedAt "${released_at}" \
    --arg publisherRef "${tag}" \
    --arg url "https://github.com/${release_repository}/releases/download/${tag}/${asset_name}" \
    --arg sha256 "${sha256}" \
    --arg rootHash "$(jq -r '.hashes.root' "${manifest_file}")" \
    --argjson size "${size}" \
    --slurpfile manifest "${manifest_file}" \
    '{
      releasedAt: $releasedAt,
      publisherRef: $publisherRef,
      url: $url,
      size: $size,
      sha256: $sha256,
      rootHash: $rootHash,
      manifest: $manifest[0]
    }' >"${sidecar_file}"

  if gh release view "${tag}" --repo "${release_repository}" >/dev/null 2>&1; then
    gh release upload "${tag}" "${asset_file}" \
      "${sidecar_file}" \
      --repo "${release_repository}" --clobber
  else
    gh release create "${tag}" \
      "${asset_file}" \
      "${sidecar_file}" \
      --repo "${release_repository}" \
      --target "${release_target}" \
      --title "${module_name} v${version}" \
      --notes "Source-owned Logos Palace package release for ${module_name} v${version}." \
      --prerelease
  fi
}

publish_module palace_vm logos-palace_vm-module-lib.lgx
publish_module palace_core logos-palace_core-module-lib.lgx
publish_module logos_palace_ui logos-logos_palace_ui-module.lgx
