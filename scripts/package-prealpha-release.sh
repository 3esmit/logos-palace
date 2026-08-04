#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
output_root="${1:-${repo_root}/.artifacts/prealpha-release}"
release_version="${PALACE_RELEASE_VERSION:-v0.1.0-pre-alpha.1}"
platform="${PALACE_RELEASE_PLATFORM:-x86_64-linux}"

if [[ ! "${release_version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-pre-alpha\.[0-9]+$ ]]; then
  printf 'PALACE_RELEASE_VERSION must match vMAJOR.MINOR.PATCH-pre-alpha.N\n' >&2
  exit 2
fi
if [[ "${platform}" != "x86_64-linux" ]]; then
  printf 'PALACE_RELEASE_PLATFORM is pinned to x86_64-linux\n' >&2
  exit 2
fi
if [[ -e "${output_root}" ]]; then
  printf 'Output path already exists: %s\n' "${output_root}" >&2
  exit 2
fi

for command_name in cargo git install jq nix sha256sum stat tar; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    printf 'Required command is missing: %s\n' "${command_name}" >&2
    exit 2
  fi
done

output_parent="$(dirname "${output_root}")"
output_name="$(basename "${output_root}")"
mkdir -p -- "${output_parent}"
stage_root="$(mktemp -d "${output_parent}/.${output_name}.tmp.XXXXXXXX")"
cleanup() {
  if [[ -d "${stage_root}" ]]; then
    rm -rf -- "${stage_root}"
  fi
}
trap cleanup EXIT

package_root="${stage_root}/package"
mkdir -p -- "${package_root}/lgx" "${package_root}/bin"

build_nix_output() {
  nix build --quiet --no-link --print-out-paths "${repo_root}#$1"
}

vm_output="$(build_nix_output palace-vm-lgx-portable)"
core_output="$(build_nix_output palace-core-lgx-portable)"
ui_output="$(build_nix_output logos-palace-ui-lgx-portable)"

package_names=(
  palace_vm
  palace_core
  logos_palace_ui
)
package_files=(
  "${vm_output}/logos-palace_vm-module-lib.lgx"
  "${core_output}/logos-palace_core-module-lib.lgx"
  "${ui_output}/logos-logos_palace_ui-module.lgx"
)

for index in "${!package_names[@]}"; do
  source_file="${package_files[${index}]}"
  if [[ ! -f "${source_file}" || -L "${source_file}" ]]; then
    printf 'Portable LGX output is not a regular file: %s\n' "${source_file}" >&2
    exit 1
  fi
  install -m 0444 "${source_file}" "${package_root}/lgx/$(basename "${source_file}")"
done

cargo build \
  --manifest-path "${repo_root}/program/palace_program/methods/Cargo.toml" \
  --release
program_file="$(find "${repo_root}/program/palace_program/methods/target/riscv-guest" \
  -type f -name palace.bin -size +0c -print -quit 2>/dev/null || true)"
if [[ -z "${program_file}" ]]; then
  printf 'RISC Zero Palace program binary was not produced\n' >&2
  exit 1
fi
install -m 0555 "${program_file}" "${package_root}/bin/palace.bin"

image_id_output="$(build_nix_output palace-image-id)"
image_id_file="${image_id_output}/bin/palace-image-id"
if [[ ! -f "${image_id_file}" || -L "${image_id_file}" || ! -x "${image_id_file}" ]]; then
  printf 'RISC Zero image verifier is incomplete: %s\n' "${image_id_file}" >&2
  exit 1
fi
install -m 0555 "${image_id_file}" "${package_root}/bin/palace-image-id"

source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
program_sha256="$(sha256sum "${package_root}/bin/palace.bin" | cut -d' ' -f1)"
program_byte_length="$(stat -c '%s' "${package_root}/bin/palace.bin")"
program_image_id="$("${package_root}/bin/palace-image-id" "${package_root}/bin/palace.bin")"
verifier_sha256="$(sha256sum "${package_root}/bin/palace-image-id" | cut -d' ' -f1)"

packages_json="$(
  for index in "${!package_names[@]}"; do
    artifact_file="${package_root}/lgx/$(basename "${package_files[${index}]}")"
    jq -n \
      --arg name "${package_names[${index}]}" \
      --arg artifact "lgx/$(basename "${artifact_file}")" \
      --arg sha256 "$(sha256sum "${artifact_file}" | cut -d' ' -f1)" \
      --argjson byteLength "$(stat -c '%s' "${artifact_file}")" \
      '{name: $name, artifact: $artifact, byteLength: $byteLength, sha256: $sha256}'
  done | jq -s .
)"

jq -n \
  --arg schema "logos.palace.prealpha-release" \
  --argjson manifestVersion 1 \
  --arg releaseVersion "${release_version}" \
  --arg platform "${platform}" \
  --arg sourceRevision "${source_revision}" \
  --arg programArtifact "bin/palace.bin" \
  --arg programSha256 "${program_sha256}" \
  --argjson programByteLength "${program_byte_length}" \
  --arg programImageIdHex "${program_image_id}" \
  --arg verifierArtifact "bin/palace-image-id" \
  --arg verifierSha256 "${verifier_sha256}" \
  --argjson packages "${packages_json}" \
  '{
    schema: $schema,
    version: $manifestVersion,
    releaseVersion: $releaseVersion,
    platform: $platform,
    sourceRevision: $sourceRevision,
    packages: $packages,
    binaries: [
      {
        name: "palace_program",
        artifact: $programArtifact,
        byteLength: $programByteLength,
        sha256: $programSha256,
        risc0ImageIdHex: $programImageIdHex
      },
      {
        name: "palace_image_id",
        artifact: $verifierArtifact,
        executable: true,
        sha256: $verifierSha256
      }
    ],
    releaseIndex: "https://raw.githubusercontent.com/3esmit/logos-3esmit-release/main/logos-repo.json",
    externalDependencies: ["delivery_module", "storage_module", "lez_core"],
    operatorAddOn: "Logos Control UI is installed from the 3esmit release index and is not bundled in the Palace archive."
  }' >"${package_root}/release.json"

install -m 0444 "${repo_root}/README.md" "${package_root}/README.md"
install -m 0444 "${repo_root}/LICENSE" "${package_root}/LICENSE"
install -m 0444 "${repo_root}/SECURITY.md" "${package_root}/SECURITY.md"

archive_name="logos-palace-mvp-${release_version#v}-${platform}.tar.gz"
tar \
  --sort=name \
  --mtime='UTC 1970-01-01' \
  --owner=0 \
  --group=0 \
  --numeric-owner \
  -czf "${stage_root}/${archive_name}" \
  -C "${package_root}" .

mv -- "${stage_root}" "${output_root}"
stage_root=""
trap - EXIT

printf 'Pre-alpha release archive: %s\n' "${output_root}/${archive_name}"
