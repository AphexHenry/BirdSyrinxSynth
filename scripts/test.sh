#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

need_cmd ctest
need_cmd uv

log "ctest --preset release"
ctest --preset release --output-on-failure

log "pytest"
(
    cd "${ROOT}/python"
    uv run pytest -q
)

log "TEST OK"
