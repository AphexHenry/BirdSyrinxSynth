#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

preset="${1:-release}"
case "$preset" in
    debug|release) ;;
    *)
        err "usage: scripts/build.sh [debug|release]"
        exit 2
        ;;
esac

need_cmd cmake
need_cmd ninja
need_cmd uv

log "configure preset=${preset}"
cmake --preset "${preset}"
log "build preset=${preset}"
cmake --build --preset "${preset}"

log "python: uv sync"
(
    cd "${ROOT}/python"
    uv sync --python 3.12
)

log "BUILD OK (${preset})"
