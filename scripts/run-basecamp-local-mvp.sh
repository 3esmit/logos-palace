#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
artifacts_root="${1:-${repo_root}/.artifacts/local-mvp}"

required_env=(
  PALACE_LOCAL_MVP_SEQUENCER
  PALACE_LOCAL_MVP_SEQUENCER_CONFIG
  PALACE_LOCAL_MVP_BASECAMP
  LOGOS_QT_MCP
  PALACE_LOCAL_MVP_LGPM
  PALACE_LOCAL_MVP_DEPLOY_TOOL
  PALACE_LOCAL_MVP_PROGRAM
  PALACE_LOCAL_MVP_LEZ_LGX
  PALACE_LOCAL_MVP_STORAGE_LGX
  PALACE_LOCAL_MVP_DELIVERY_LGX
  PALACE_LOCAL_MVP_VM_LGX
  PALACE_LOCAL_MVP_CONTROL_LGX
  PALACE_LOCAL_MVP_CORE_LGX
  PALACE_LOCAL_MVP_UI_LGX
  PALACE_E2E_ASSET_INPUT_ROOT
  PALACE_E2E_ASSET_MANIFEST
)

for variable in "${required_env[@]}"; do
  if [ -z "${!variable:-}" ]; then
    printf 'Required environment variable is missing: %s\n' "${variable}" >&2
    exit 2
  fi
done

profile="${PALACE_LEZ_PROFILE:-local-development}"
if [ "${profile}" != "local-development" ]; then
  printf 'Local MVP runner requires PALACE_LEZ_PROFILE=local-development\n' >&2
  exit 2
fi

if [ -L "${artifacts_root}" ]; then
  printf 'Local MVP artifacts root must not be a symlink\n' >&2
  exit 2
fi
mkdir -p -- "${artifacts_root}"
artifacts_root="$(realpath -e -- "${artifacts_root}")"
chmod 700 -- "${artifacts_root}"
run_root="$(mktemp -d "${artifacts_root}/run.XXXXXXXX")"
chmod 700 -- "${run_root}"

canonical_file() {
  local variable="$1"
  local path="${!variable}"
  local canonical
  canonical="$(realpath -e -- "${path}" 2>/dev/null || true)"
  if [ -z "${canonical}" ] || [ "${path}" != "${canonical}" ] \
    || [ -L "${path}" ] || [ ! -f "${path}" ] || [ ! -x "${path}" ]; then
    printf '%s must be a canonical executable file\n' "${variable}" >&2
    exit 2
  fi
  printf -v "${variable}" '%s' "${canonical}"
}

canonical_config() {
  local variable="$1"
  local path="${!variable}"
  local canonical
  canonical="$(realpath -e -- "${path}" 2>/dev/null || true)"
  if [ -z "${canonical}" ] || [ "${path}" != "${canonical}" ] \
    || [ -L "${path}" ] || [ ! -f "${path}" ]; then
    printf '%s must be a canonical regular file\n' "${variable}" >&2
    exit 2
  fi
  printf -v "${variable}" '%s' "${canonical}"
}

canonical_regular() {
  local variable="$1"
  local path="${!variable}"
  local canonical
  canonical="$(realpath -e -- "${path}" 2>/dev/null || true)"
  if [ -z "${canonical}" ] || [ "${path}" != "${canonical}" ] \
    || [ -L "${path}" ] || [ ! -f "${path}" ]; then
    printf '%s must be a canonical regular file\n' "${variable}" >&2
    exit 2
  fi
  printf -v "${variable}" '%s' "${canonical}"
}

canonical_file PALACE_LOCAL_MVP_SEQUENCER
canonical_config PALACE_LOCAL_MVP_SEQUENCER_CONFIG
canonical_file PALACE_LOCAL_MVP_BASECAMP
canonical_file PALACE_LOCAL_MVP_LGPM
canonical_file PALACE_LOCAL_MVP_DEPLOY_TOOL
canonical_regular PALACE_LOCAL_MVP_PROGRAM
canonical_regular PALACE_LOCAL_MVP_LEZ_LGX
canonical_regular PALACE_LOCAL_MVP_STORAGE_LGX
canonical_regular PALACE_LOCAL_MVP_DELIVERY_LGX
canonical_regular PALACE_LOCAL_MVP_VM_LGX
canonical_regular PALACE_LOCAL_MVP_CONTROL_LGX
canonical_regular PALACE_LOCAL_MVP_CORE_LGX
canonical_regular PALACE_LOCAL_MVP_UI_LGX

qt_mcp="$(realpath -e -- "${LOGOS_QT_MCP}" 2>/dev/null || true)"
if [ -z "${qt_mcp}" ] || [ "${LOGOS_QT_MCP}" != "${qt_mcp}" ] \
  || [ -L "${LOGOS_QT_MCP}" ] || [ ! -d "${qt_mcp}/test-framework" ]; then
  printf 'LOGOS_QT_MCP must be a canonical directory with test-framework\n' >&2
  exit 2
fi
LOGOS_QT_MCP="${qt_mcp}"

asset_root="$(realpath -e -- "${PALACE_E2E_ASSET_INPUT_ROOT}" 2>/dev/null || true)"
asset_manifest="$(realpath -e -- "${PALACE_E2E_ASSET_MANIFEST}" 2>/dev/null || true)"
if [ -z "${asset_root}" ] || [ "${PALACE_E2E_ASSET_INPUT_ROOT}" != "${asset_root}" ] \
  || [ -L "${PALACE_E2E_ASSET_INPUT_ROOT}" ] || [ ! -d "${asset_root}" ]; then
  printf 'PALACE_E2E_ASSET_INPUT_ROOT must be a canonical directory\n' >&2
  exit 2
fi
case "${asset_manifest}" in
  "${asset_root}"/*) ;;
  *) printf 'PALACE_E2E_ASSET_MANIFEST must be inside the input root\n' >&2; exit 2 ;;
esac
if [ -z "${asset_manifest}" ] || [ "${PALACE_E2E_ASSET_MANIFEST}" != "${asset_manifest}" ] \
  || [ -L "${PALACE_E2E_ASSET_MANIFEST}" ] || [ ! -f "${asset_manifest}" ]; then
  printf 'PALACE_E2E_ASSET_MANIFEST must be a canonical regular file\n' >&2
  exit 2
fi

node_flow="${PALACE_LOCAL_MVP_NODE_FLOW:-${repo_root}/tests/basecamp_local_mvp_user_flow.mjs}"
node_flow="$(realpath -e -- "${node_flow}" 2>/dev/null || true)"
if [ -z "${node_flow}" ] || [ -L "${node_flow}" ] || [ ! -f "${node_flow}" ]; then
  printf 'PALACE_LOCAL_MVP_NODE_FLOW must be a canonical regular file\n' >&2
  exit 2
fi

sequencer_port="${PALACE_LOCAL_MVP_SEQUENCER_PORT:-3040}"
if [[ ! "${sequencer_port}" =~ ^[0-9]+$ ]] || [ "${sequencer_port}" -lt 1024 ] || [ "${sequencer_port}" -gt 65535 ]; then
  printf 'PALACE_LOCAL_MVP_SEQUENCER_PORT must be an unprivileged TCP port\n' >&2
  exit 2
fi

mkdir -p -- "${run_root}/sequencer-home" "${run_root}/deployer" \
  "${run_root}/creator/modules" "${run_root}/creator/plugins" \
  "${run_root}/bob/modules" "${run_root}/bob/plugins" \
  "${run_root}/carol/modules" "${run_root}/carol/plugins" \
  "${run_root}/evidence" "${run_root}/lgx"
chmod 700 -- "${run_root}/sequencer-home" "${run_root}/deployer" \
  "${run_root}/creator" "${run_root}/bob" "${run_root}/carol" "${run_root}/evidence"

for package in \
  "${PALACE_LOCAL_MVP_LEZ_LGX}" \
  "${PALACE_LOCAL_MVP_STORAGE_LGX}" \
  "${PALACE_LOCAL_MVP_DELIVERY_LGX}" \
  "${PALACE_LOCAL_MVP_VM_LGX}" \
  "${PALACE_LOCAL_MVP_CONTROL_LGX}" \
  "${PALACE_LOCAL_MVP_CORE_LGX}" \
  "${PALACE_LOCAL_MVP_UI_LGX}"; do
  ln -s -- "${package}" "${run_root}/lgx/$(basename "${package}")"
done
for user in creator bob carol; do
  "${PALACE_LOCAL_MVP_LGPM}" \
    --modules-dir "${run_root}/${user}/modules" \
    --ui-plugins-dir "${run_root}/${user}/plugins" \
    --allow-unsigned install --dir "${run_root}/lgx" \
    >"${run_root}/${user}-lgpm-install.log" 2>&1
  "${PALACE_LOCAL_MVP_LGPM}" \
    --modules-dir "${run_root}/${user}/modules" \
    --ui-plugins-dir "${run_root}/${user}/plugins" \
    --json list >"${run_root}/${user}-installed-packages.json"
done

printf '%s\n' \
  "{\"sequencer_addr\":\"http://127.0.0.1:${sequencer_port}\",\"seq_poll_timeout\":\"2s\",\"seq_tx_poll_max_blocks\":30,\"seq_poll_max_retries\":10,\"seq_block_poll_max_amount\":100}" \
  >"${run_root}/deployer/wallet_config.json"

(
  cd -- "${run_root}/sequencer-home"
  exec "${PALACE_LOCAL_MVP_SEQUENCER}" "${PALACE_LOCAL_MVP_SEQUENCER_CONFIG}" --port "${sequencer_port}"
) >"${run_root}/sequencer.log" 2>&1 &
sequencer_pid=$!

cleanup() {
  if kill -0 "${sequencer_pid}" 2>/dev/null; then
    kill "${sequencer_pid}" 2>/dev/null || true
    wait "${sequencer_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

ready=0
for _ in $(seq 1 60); do
  if (exec 3<>"/dev/tcp/127.0.0.1/${sequencer_port}") 2>/dev/null; then
    exec 3>&- 3<&-
    ready=1
    break
  fi
  if ! kill -0 "${sequencer_pid}" 2>/dev/null; then
    sed -n '1,240p' "${run_root}/sequencer.log" >&2
    exit 1
  fi
  sleep 1
done
if [ "${ready}" -ne 1 ]; then
  printf 'Local sequencer did not become ready\n' >&2
  sed -n '1,240p' "${run_root}/sequencer.log" >&2
  exit 1
fi

deploy_password="$(openssl rand -hex 32)"
PALACE_LEZ_WALLET_PASSWORD="${deploy_password}" "${PALACE_LOCAL_MVP_DEPLOY_TOOL}" \
  "${run_root}/deployer/wallet_config.json" \
  "${run_root}/deployer/wallet.json" \
  "${PALACE_LOCAL_MVP_PROGRAM}" >"${run_root}/deployment.json"
unset deploy_password

LOGOS_QT_MCP="${LOGOS_QT_MCP}" \
PALACE_E2E_ASSET_INPUT_ROOT="${asset_root}" \
PALACE_E2E_ASSET_MANIFEST="${asset_manifest}" \
PALACE_LEZ_PROFILE="${profile}" \
node "${node_flow}" \
  "${PALACE_LOCAL_MVP_BASECAMP}" \
  "${run_root}/creator" "${run_root}/bob" "${run_root}/carol" \
  "${run_root}/evidence" >"${run_root}/e2e-result.json"

host_root="$(find "${run_root}/creator/module_data/palace_core" -mindepth 1 -maxdepth 1 -type d -print -quit 2>/dev/null || true)"
if [ -z "${host_root}" ]; then
  printf 'Palace host persistence root missing\n' >&2
  exit 1
fi
profile_root="${host_root}/palace-profiles/${profile}"
if [ ! -d "${profile_root}" ]; then
  printf 'Local profile persistence root missing\n' >&2
  exit 1
fi
for path in verified_assets asset_downloads storage_publications storage_catalog_downloads; do
  if [ -e "${host_root}/${path}" ]; then
    printf 'Forbidden direct host-root path: %s\n' "${path}" >&2
    exit 1
  fi
done

node --input-type=module - "${run_root}/e2e-result.json" "${run_root}/local-mvp-report.json" <<'NODE'
import { readFile, writeFile } from "node:fs/promises";
const [input, output] = process.argv.slice(2);
const result = JSON.parse(await readFile(input, "utf8"));
const report = {
  schema: "logos-palace.local-mvp-report",
  version: 1,
  profile: "local-development",
  publicFinalityAvailable: false,
  palaceId: result.creator?.palace?.palaceId ?? result.recovery?.palaceId ?? null,
  assetCount: (result.creator?.palace?.imported?.length ?? 0)
    + (result.creator?.palace?.propId ? 1 : 0),
  orderedMessages: result.orderedMessaging?.total ?? 0,
  participants: Object.keys(result.identities ?? {}).length,
  doorFinalized: Boolean(result.door?.finalizedState),
  moderation: Boolean(result.moderation?.rawDeliveryRejected),
  providerRestarted: result.recovery?.providerAOffline === true,
};
await writeFile(output, `${JSON.stringify(report, null, 2)}\n`, { mode: 0o600 });
process.stdout.write(`${output}\n`);
NODE

printf 'Local MVP run complete: %s\n' "${run_root}"
