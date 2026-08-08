#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
artifacts_dir="${1:-${repo_root}/.artifacts/e2e}"

# One supported local story. All runtime paths, package outputs, and the
# ignored user-asset manifest remain explicit inputs to the underlying runner.
export PALACE_LOCAL_MVP_NODE_FLOW="${PALACE_LOCAL_MVP_NODE_FLOW:-${repo_root}/tests/palace_e2e_user_story.mjs}"
exec "${repo_root}/scripts/run-basecamp-local-mvp.sh" "${artifacts_dir}"
